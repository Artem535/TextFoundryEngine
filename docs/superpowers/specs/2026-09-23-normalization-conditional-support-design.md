# Conditional Support in Normalization — Design

**Goal:** `Engine::NormalizeComposition` and `Engine::PreviewNormalizeComposition` currently reject any composition containing a `Conditional` fragment outright (`tf/engine.cc:920-925`, `969-973`, `1104-1114`), returning `InvalidParamType` with the message "Normalization does not yet support compositions containing Conditional content". This means a composition that uses if/elif/else — the engine's own flagship recent feature — cannot be AI-normalized at all. This design closes that gap: normalization recurses into `Conditional` branches and `elseContent`, normalizing their nested content (`StaticText` via the LLM normalizer, `BlockRef` via the block normalizer, with the existing derived-block caching) exactly as it does for top-level fragments, and produces a real `Conditional` in the derivative `Composition` that renders correctly per `RenderContext` afterward.

**Non-goal:** changing `Renderer`, `Conditional::validate`, the DTO layer, or anything about how Conditional is authored, stored, or rendered. This is purely about what `NormalizeComposition`/`PreviewNormalizeComposition` do when they encounter one.

## Context

`Conditional` was added as a `Fragment` variant with recursive content (`Branch::content`, `Conditional::elseContent`, both `vector<Fragment>`). Every other engine-wide traversal that used to assume "not Separator, not StaticText => BlockRef" was fixed to recurse into `Conditional` via `VisitBlockRefs` (`tf/fragment.h:296`) — `Composition::publish`'s UseLatest check, `Engine::DeleteBlock`'s usage check, `Engine::PreviewCompositionBlockRewrite`/`ApplyCompositionBlockRewrite`. Normalization is the one remaining engine-wide traversal that still treats `Conditional` as an error instead of recursing.

## Current Behavior (baseline)

Both `NormalizeComposition` (`tf/engine.cc:1033`) and `PreviewNormalizeComposition` (`tf/engine.cc:875`, plus its `reuse_cached_blocks` fast path at `905-943`) run a **flat** loop over `source.fragments()`. Per fragment:

- `Separator` — copied through unchanged.
- `StaticText` — if `request.normalize_static_text`, calls `normalizer_->Normalize(text, style)` (an `INormalizer`, LLM-backed); otherwise passed through.
- `BlockRef` — derives a normalized `Block` via `blockNormalizer_->NormalizeBlock(...)` (an `IBlockNormalizer`, LLM-backed), tagged with a `normalization_key_tag` so a later request with the same `(style, blockNormalizer_->Fingerprint())` can reuse it instead of re-calling the LLM (`request.reuse_cached_blocks`), and rewrites the reference to point at the derived block. `rewritten_blocks` accumulates `(source_id, derived_id)` pairs.
- `Conditional` — **rejected**, `InvalidParamType`.

`PreviewNormalizeComposition` additionally has a **composition-level** cache check (`905-943`): if a prior derivative composition already exists under the same key, it skips the LLM entirely and rebuilds `preview_text` by reading straight from that existing composition's already-normalized blocks. This is separate from the apply path's own composition-level cache check (`1062-1072`), which just returns the existing id/version without touching `preview_text` at all (apply doesn't need one).

## Design

### 1. `Engine::NormalizeFragments` — recursive fragment normalizer

New private method, replacing the body of both flat loops' per-fragment logic:

```cpp
Result<std::vector<Fragment>> Engine::NormalizeFragments(
    const std::vector<Fragment>& fragments,
    const CompositionNormalizationRequest& request,
    const std::string& normalization_key_tag, bool persist_derived_blocks,
    std::vector<std::pair<BlockId, BlockId>>& rewritten_blocks);
```

**Amendment:** the signature above includes `persist_derived_blocks` (added after this design's first implementation, once code review caught that `PreviewNormalizeComposition`'s fresh path was silently publishing new `Block`s via this method). `true` for `NormalizeComposition` (apply, which is meant to persist derived blocks); `false` for `PreviewNormalizeComposition`'s fresh path. When `false`, a freshly-normalized `BlockRef` is not published — it comes back as an inline `Fragment::MakeStaticText(normalized_template.Content())` instead of a `BlockRef`, and `rewritten_blocks` still records the `(source_block.Id(), derived_block_id)` pair that applying would produce. A `BlockRef` satisfied by an existing tagged cached `Block` is unaffected by this flag: referencing something that already exists isn't a new write, so it's still returned as a real `BlockRef` in both modes.

Per fragment, in order:

- `Separator` → copied through unchanged (`Fragment::MakeSeparator`).
- `StaticText` → same as today: LLM-normalize if `request.normalize_static_text`, else pass through. Wrapped back into a `Fragment` via `Fragment::MakeStaticText`.
- `BlockRef` → **exactly today's derive-and-cache logic** (`tf/engine.cc:1117-1189`), unchanged, just relocated into this method. Appends to `rewritten_blocks`. Wrapped via `Fragment::MakeBlockRef`.
- `Conditional` → build a new `Conditional`:
  - For each `Branch`: copy `conditions` unchanged (they're evaluated against `RenderContext` params at render time, not touched by normalization); set `content = NormalizeFragments(branch.content, request, normalization_key_tag, rewritten_blocks).value()` — **recursive call**, same accumulator, same request.
  - If `cond.elseContent.has_value()`: `elseContent = NormalizeFragments(*cond.elseContent, ...)`.
  - Any error from a recursive call propagates immediately (`Result<std::vector<Fragment>>` with the inner error) — same short-circuit-on-first-error behavior the flat loop already has.
  - No recursion-depth limit and no LLM-call budget: normalization walks every branch and every nested `Conditional` unconditionally, same as top-level content (confirmed: acceptable, `reuse_cached_blocks` already amortizes repeated block-level normalization across calls; add a limit later only if a real cost problem shows up).

`NormalizeComposition` becomes: call `NormalizeFragments(source.fragments(), request, NormalizationKeyTag(normalization_key), rewritten_blocks)`, then for each returned top-level `Fragment`, dispatch into the builder with a small local switch (same shape as `cli::AppendFragmentToDraft` in `cli/dto.cc:129-146`, but this is a separate translation unit so it's a small local duplicate, not a shared call — promoting it to a shared helper is explicitly out of scope here, see below):

```cpp
switch (fragment.type()) {
  case FragmentType::BlockRef:    builder.AddBlockRef(std::move(fragment.AsBlockRef())); break;
  case FragmentType::StaticText:  builder.AddStaticText(std::move(fragment.AsStaticText().content)); break;
  case FragmentType::Separator:   builder.AddSeparator(fragment.AsSeparator().type); break;
  case FragmentType::Conditional: builder.AddConditional(std::move(fragment.AsConditional())); break;
}
```

Everything after that (publish-or-bump the derivative composition, build `NormalizedCompositionResult`) is unchanged.

### 2. `Engine::FragmentTreeToPreviewText` — preview rendering over an already-normalized tree

New private method:

```cpp
Result<std::vector<std::string>> Engine::FragmentTreeToPreviewText(
    const std::vector<Fragment>& fragments,
    const std::optional<std::string>& delimiter);
```

Takes a fragment list that is **already normalized** (either just produced by `NormalizeFragments`, or loaded straight from a previously-published derivative composition in the cache-reuse path) and turns it into one text string per top-level fragment — no LLM calls, no caching logic, just reading content. `delimiter` is `style.delimiter` from the composition's `EffectiveStyle`/`GetEffectiveStyle` (the same style `ApplyStructuralStyle` is about to use at the call site) — threaded through so a branch with more than one fragment joins them the same way the real `Renderer` does:

- `Separator` → `.toString()`.
- `StaticText` → `.text()`.
- `BlockRef` → `LoadBlock(block_ref.GetBlockId(), *version)` (version is always set at this point — normalized/derived refs are always versioned) then `.templ().Content()`. Propagates `BlockNotFound` etc. as a real error, same as today.
- `Conditional` → recurse into every branch (`FragmentTreeToPreviewText(branch.content, delimiter)`) and `elseContent` if present. **A branch's own multiple fragments are joined with `delimiter`** (`""` if unset) — not an ad-hoc preview-only rule: `Renderer::ResolveConditionals` (`tf/renderer.cc:200-249`) flattens a selected branch's content into the *same* top-level list as everything else in the composition before `ApplyStructuralStyle` ever runs, so at real render time a multi-fragment branch's pieces are joined by the exact same delimiter as any other pair of sibling fragments. Reusing `delimiter` here keeps the preview honest about what render will actually produce. The three-way-labeled block (`if`/`elif`/`else`, one join per branch) is then wrapped as **one string** for this fragment's position in the *outer* `fragment_texts` list:

  ```
  [if <cond>]
  <branch 1 text>
  [elif <cond>]
  <branch 2 text>
  [else]
  <else text>
  ```

  - A single `Condition` renders as `attribute in {v1, v2}` (values in the set's iteration order — `unordered_set`, so order is not guaranteed stable across runs; acceptable for a human-readable preview, not a machine-parsed format) or `attribute not in {v1, v2}` when `negate`.
  - Multiple `Condition`s on one branch join with ` and `.
  - The first branch is labeled `if`, every subsequent branch `elif`.
  - `else` is emitted only if `elseContent.has_value()` — mirrors the defensive check already in `Renderer::ResolveConditionals` (`tf/renderer.cc:236-239`): storage was loaded, not re-validated, so a value-less `elseContent` on a `Conditional` that reached this code (which requires it having been `Published`, whose `publish()` calls `validate()`) shouldn't happen, but the code doesn't assume it.

This single method replaces:
- The apply-adjacent preview text assembly in the fresh path (`948-1023`, specifically the `fragment_texts.push_back(...)` calls) — call it on the result of `NormalizeFragments`.
- The entire per-fragment loop in the cache-reuse path (`912-935`) — call it directly on `existing.value().fragments()`. This also deletes that path's own hand-rolled `Separator`/`StaticText`/`BlockRef`-only loop (it currently duplicates the fresh path's leaf-handling almost verbatim minus the LLM calls), and — as a side effect of routing through the same method — the cache-reuse preview path automatically gains support for a previously-normalized `Conditional` with zero extra code.

Both call sites need their `EffectiveStyle(...)` call (which today only happens once, right before the final `ApplyStructuralStyle`, at the end of each path) moved earlier, so `style.delimiter` is available to pass into `FragmentTreeToPreviewText`. This is a small reordering, not a behavior change — `EffectiveStyle` is a pure read of `composition.GetStyleProfile()`, so computing it a few lines earlier in the same function changes nothing about its result.

Both `PreviewNormalizeComposition` code paths end the same way they do today: `ApplyStructuralStyle(fragment_texts, style)`, unchanged — it already treats each entry as an opaque string, so a multi-line Conditional block joins/wraps/delimits exactly like any other fragment's text.

**Superseded by the `persist_derived_blocks` amendment above:** this paragraph originally described the fresh path doing one extra `LoadBlock` per derived `BlockRef` (inside `FragmentTreeToPreviewText`) instead of reusing the `normalized_template` string already sitting in a local variable from the derivation step a few lines earlier. That's no longer what happens: with `persist_derived_blocks=false`, a freshly-normalized `BlockRef` never gets far enough to need a `LoadBlock` in the first place — `NormalizeFragments` returns it as an inline `StaticText` holding `normalized_template.Content()` directly, since the underlying `Block` was never published and a `LoadBlock` against its `derived_block_id` would fail. `FragmentTreeToPreviewText` still reads it with zero extra I/O; there is no tradeoff to make.

### 3. `rewritten_blocks` and nested `BlockRef`

Unchanged in shape (`std::vector<std::pair<BlockId, BlockId>>`), but now includes entries for `BlockRef`s found anywhere in the tree, not just top level — consistent with `VisitBlockRefs`'s existing "nested is not special" convention used everywhere else in the engine. No caller currently inspects `rewritten_blocks` positionally (it's an unordered audit list), so this is not a breaking shape change.

## Error Handling

No new error codes. Existing behavior preserved and extended uniformly to nested content:

- A nested `BlockRef` that fails to load, fails LLM normalization, or fails placeholder-preservation validation (`PlaceholderSet` mismatch, `tf/engine.cc:1151-1155`) fails the whole `NormalizeComposition`/`PreviewNormalizeComposition` call, same as a top-level one would today — no partial/best-effort normalization.
- A nested `StaticText` that fails LLM normalization likewise fails the whole call.
- The three existing `InvalidParamType` "does not yet support... Conditional" rejection sites (`920-925`, `969-973`, `1104-1114`) are deleted — `Conditional` is no longer a rejection case anywhere in this code path.

## Testing

- `NormalizeFragments` unit coverage (via `NormalizeComposition`, following the existing `EngineTestFixture` pattern in `tests/test_main.cc`): a `Conditional` with `StaticText` in one branch and a `BlockRef` in another, plus `elseContent` with a second `BlockRef` — assert the resulting derivative composition, when loaded back, has a `Conditional` fragment whose branches/elseContent reference the *derived* blocks (not the originals), and that `rewritten_blocks` contains entries for both nested refs.
- A branch containing a nested `Conditional` (two levels) — confirms recursion, not just one level of special-casing.
- Round-trip: normalize a `Conditional` composition, publish it (already required to reach `Published` for `Render` to work), then `Render` it twice with different `RenderContext` params so two different branches fire — assert the two render outputs differ and each matches its branch's (post-normalization) content. Mirrors the existing pattern in `tests/cli_dto_test.cc`'s "closes the loop" test, but for the normalize step instead of the DTO step.
- `PreviewNormalizeComposition` fresh path: a `Conditional` with two branches + else, assert `preview_text` contains all three labeled segments in order, with the `if`/`elif`/`else` markers and the rendered condition (`attribute in {...}` / `not in {...}`).
- `PreviewNormalizeComposition` cache-reuse path: run normalize-then-preview twice with `reuse_cached_blocks = true`; assert the second call's `preview_text` still correctly shows the `Conditional` branches (proving the cache-reuse path, now routed through `FragmentTreeToPreviewText`, handles `Conditional` too) and that no LLM call happens on the second call (existing test double / call-counting normalizer, following whatever mechanism `tests/test_main.cc`'s `FakeBlockNormalizer`/normalizer test doubles already use).
- `elseContent` defensive-empty case for preview: not expected to be reachable through the public API (a `Conditional` without `elseContent` fails `validate()` before it can be `Published`), so no dedicated test beyond what already covers `Renderer::ResolveConditionals`'s identical defensive branch — noted here only so the parallel is visible to a future reader of `FragmentTreeToPreviewText`.

## Explicitly Out of Scope

- Promoting the small `AppendFragmentToDraft`-shaped switch (fragment → builder call) that now exists in both `cli/dto.cc` and `tf/engine.cc` into one shared helper. Same observation was already flagged as a judgement-call "Message Chains"/duplication item in the CLI branch's Standards review; worth doing eventually, not blocking this feature, and touches a different file boundary (`tf/composition.h`) than this design otherwise needs to.
- A recursion-depth or branch-count limit on `Conditional` normalization (explicitly decided against above; revisit only if a real cost incident occurs).
- Any change to `Renderer`, `Conditional::validate`, the CLI/DTO layer, or `CompositionBlockRewrite` (which already handles nested `Conditional` via `RewriteFragmentBlockRefs`, added in `1f87549`, and is unrelated to normalization).
- Making the `Condition`-set rendering (`attribute in {v1, v2}`) order-stable. `allowedValues` is an `unordered_set<std::string>`; preview output may list values in different orders across runs. Not a concern for a preview string; would need a real type change (`Condition::allowedValues` → an ordered container) to fix, which is out of scope for this design.
