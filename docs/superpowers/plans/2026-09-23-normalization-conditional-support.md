# Conditional Support in Normalization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `Engine::NormalizeComposition` and `Engine::PreviewNormalizeComposition` recurse into `Conditional` fragments (branches and `elseContent`) instead of rejecting them outright, and make `PreviewNormalizeComposition` render a `Conditional` as a labeled if/elif/else text block.

**Architecture:** Extract the existing per-fragment normalization logic (currently three near-duplicate flat loops across two methods) into two new private `Engine` methods: `NormalizeFragments` (recursive, does the actual LLM-backed normalization + derived-block caching, used by the apply path and preview's fresh path) and `FragmentTreeToPreviewText` (recursive, pure content read, no LLM, used by both preview paths). `NormalizeComposition` and `PreviewNormalizeComposition` become thin callers of these two methods plus their existing publish/cache-check logic.

**Tech Stack:** C++23, doctest (existing `tests/test_main.cc`), no new dependencies.

## Global Constraints

- No new `ErrorCode`s. All existing error propagation behavior (first error wins, no partial normalization) is preserved and extended uniformly to nested content.
- No recursion-depth or LLM-call-count limit on `Conditional` normalization.
- No changes to `Renderer`, `Conditional::validate`, the CLI/DTO layer, or `CompositionBlockRewrite`.
- Spec: `docs/superpowers/specs/2026-09-23-normalization-conditional-support-design.md` — read it before starting if anything below is unclear.

---

## File Structure

- **Modify: `tf/engine.h`** — add two private method declarations to `class Engine` (`NormalizeFragments`, `FragmentTreeToPreviewText`).
- **Modify: `tf/engine.cc`** — implement both new methods (in the anonymous-namespace-adjacent private-method section, i.e. as `Engine::` member function definitions like the rest of the file); refactor `NormalizeComposition` and `PreviewNormalizeComposition` to call them; delete the three `Conditional` rejection blocks. Add two small anonymous-namespace helpers: `RenderCondition` and `RenderBranchLabel` (used only by `FragmentTreeToPreviewText`).
- **Modify: `tests/test_main.cc`** — extend `FakeBlockNormalizer` with a call counter (Task 3); replace the now-incorrect "rejects Conditional" test with new coverage in `TEST_SUITE("CompositionNormalization")` (Tasks 1–3).

---

### Task 1: `Engine::NormalizeFragments` — recursive normalizer, wired into `NormalizeComposition`

**Files:**
- Modify: `tf/engine.h` (add private method declaration, near `GetNextVersion` at the end of the private section, ~line 465)
- Modify: `tf/engine.cc:1033-1207` (`Engine::NormalizeComposition`)
- Test: `tests/test_main.cc`, `TEST_SUITE("CompositionNormalization")` (starts at line 310)

**Interfaces:**
- Produces: `Result<std::vector<Fragment>> Engine::NormalizeFragments(const std::vector<Fragment>& fragments, const CompositionNormalizationRequest& request, const std::string& normalization_key_tag, std::vector<std::pair<BlockId, BlockId>>& rewritten_blocks)` — private, non-const (calls `PublishBlock`). Recurses into `Conditional`. On any error from a nested `StaticText`/`BlockRef`/recursive-`Conditional` call, returns immediately with that error.

- [ ] **Step 1: Replace the existing "rejects Conditional" test and add nested-normalization coverage**

Open `tests/test_main.cc`. Find this test (currently lines 393-425) inside `TEST_SUITE("CompositionNormalization")`:

```cpp
  TEST_CASE(
      "NormalizeComposition rejects Conditional content, consistent with "
      "PreviewNormalizeComposition") {
    EngineTestFixture fixture;

    auto cond = ConditionalBuilder()
                    .If(Condition{.attribute = "level",
                                  .allowedValues = {"expert"}})
                    .Then(Fragment::MakeStaticText("expert text"))
                    .Else(Fragment::MakeStaticText("default text"))
                    .build();

    CompositionDraftBuilder composition_builder("prompt.cond");
    composition_builder.AddConditional(std::move(cond));
    auto composition = fixture.engine.PublishComposition(
        composition_builder.build(), Version{1, 0});
    REQUIRE(composition.HasValue());

    fixture.engine.SetBlockNormalizer(std::make_shared<FakeBlockNormalizer>(
        Result<NormalizedBlockData>(NormalizedBlockData{
            .templ = "unused",
            .description = std::nullopt,
            .language = std::nullopt,
        })));

    auto result = fixture.engine.NormalizeComposition(
        CompositionNormalizationRequest{
            .source_composition_id = "prompt.cond",
            .style = SemanticStyle{.tone = std::string("warm")},
        });
    REQUIRE(result.HasError());
    CHECK(result.error().code == ErrorCode::InvalidParamType);
  }
```

Replace it with these three test cases (this is a straight replacement — the old test asserted the old, now-wrong behavior):

```cpp
  TEST_CASE(
      "NormalizeComposition recurses into Conditional branches and "
      "elseContent, rewriting nested BlockRefs and leaving nested "
      "StaticText in place (normalize_static_text defaults to false)") {
    EngineTestFixture fixture;

    Block expert_block =
        fixture.createAndPublishBlock("role.expert", "You are an expert.");
    Block beginner_block =
        fixture.createAndPublishBlock("role.beginner", "You are a beginner guide.");

    auto cond =
        ConditionalBuilder()
            .If(Condition{.attribute = "level", .allowedValues = {"none"}})
            .Then(Fragment::MakeStaticText("no particular expertise"))
            .If(Condition{.attribute = "level", .allowedValues = {"expert"}})
            .Then(Fragment::MakeBlockRef(
                BlockRef("role.expert", expert_block.version())))
            .Else(Fragment::MakeBlockRef(
                BlockRef("role.beginner", beginner_block.version())))
            .build();

    CompositionDraftBuilder composition_builder("prompt.cond");
    composition_builder.AddConditional(std::move(cond));
    auto composition = fixture.engine.PublishComposition(
        composition_builder.build(), Version{1, 0});
    REQUIRE(composition.HasValue());

    fixture.engine.SetBlockNormalizer(std::make_shared<FakeBlockNormalizer>(
        Result<NormalizedBlockData>(NormalizedBlockData{
            .templ = "normalized role text",
            .description = std::nullopt,
            .language = std::nullopt,
        })));

    auto result = fixture.engine.NormalizeComposition(
        CompositionNormalizationRequest{
            .source_composition_id = "prompt.cond",
            .style = SemanticStyle{.tone = std::string("warm")},
        });
    REQUIRE(result.HasValue());

    auto normalized_comp =
        fixture.engine.LoadComposition(result.value().composition_id);
    REQUIRE(normalized_comp.HasValue());
    REQUIRE(normalized_comp.value().fragmentCount() == 1);
    REQUIRE(normalized_comp.value().fragment(0).IsConditional());

    const Conditional& normalized_cond =
        normalized_comp.value().fragment(0).AsConditional();
    REQUIRE(normalized_cond.branches.size() == 2);

    REQUIRE(normalized_cond.branches[0].content.size() == 1);
    REQUIRE(normalized_cond.branches[0].content[0].IsStaticText());
    CHECK(normalized_cond.branches[0].content[0].AsStaticText().text() ==
          "no particular expertise");

    REQUIRE(normalized_cond.branches[1].content.size() == 1);
    REQUIRE(normalized_cond.branches[1].content[0].IsBlockRef());
    const auto& normalized_expert_ref =
        normalized_cond.branches[1].content[0].AsBlockRef();
    CHECK(normalized_expert_ref.GetBlockId() == "norm.role.expert");
    auto normalized_expert_block = fixture.engine.LoadBlock(
        normalized_expert_ref.GetBlockId(), *normalized_expert_ref.version());
    REQUIRE(normalized_expert_block.HasValue());
    CHECK(normalized_expert_block.value().templ().Content() ==
          "normalized role text");

    REQUIRE(normalized_cond.elseContent.has_value());
    REQUIRE(normalized_cond.elseContent->size() == 1);
    REQUIRE((*normalized_cond.elseContent)[0].IsBlockRef());
    CHECK((*normalized_cond.elseContent)[0].AsBlockRef().GetBlockId() ==
          "norm.role.beginner");

    REQUIRE(result.value().rewritten_blocks.size() == 2);
    bool has_expert_rewrite = false;
    bool has_beginner_rewrite = false;
    for (const auto& [from, to] : result.value().rewritten_blocks) {
      if (from == "role.expert" && to == "norm.role.expert") {
        has_expert_rewrite = true;
      }
      if (from == "role.beginner" && to == "norm.role.beginner") {
        has_beginner_rewrite = true;
      }
    }
    CHECK(has_expert_rewrite);
    CHECK(has_beginner_rewrite);
  }

  TEST_CASE(
      "NormalizeComposition recurses through a Conditional nested inside "
      "another Conditional's branch (two levels)") {
    EngineTestFixture fixture;

    auto inner_cond =
        ConditionalBuilder()
            .If(Condition{.attribute = "tone", .allowedValues = {"formal"}})
            .Then(Fragment::MakeStaticText("formal inner text"))
            .Else(Fragment::MakeStaticText("casual inner text"))
            .build();

    auto outer_cond =
        ConditionalBuilder()
            .If(Condition{.attribute = "level", .allowedValues = {"expert"}})
            .Then(Fragment::MakeConditional(std::move(inner_cond)))
            .Else(Fragment::MakeStaticText("outer else text"))
            .build();

    CompositionDraftBuilder composition_builder("prompt.nested_cond");
    composition_builder.AddConditional(std::move(outer_cond));
    auto composition = fixture.engine.PublishComposition(
        composition_builder.build(), Version{1, 0});
    REQUIRE(composition.HasValue());

    // No BlockRef anywhere in this tree, but NormalizeComposition still
    // requires a block normalizer to be configured before it does anything.
    fixture.engine.SetBlockNormalizer(std::make_shared<FakeBlockNormalizer>(
        Result<NormalizedBlockData>(NormalizedBlockData{
            .templ = "unused",
            .description = std::nullopt,
            .language = std::nullopt,
        })));

    auto result = fixture.engine.NormalizeComposition(
        CompositionNormalizationRequest{
            .source_composition_id = "prompt.nested_cond",
            .style = SemanticStyle{.tone = std::string("warm")},
        });
    REQUIRE(result.HasValue());

    auto normalized_comp =
        fixture.engine.LoadComposition(result.value().composition_id);
    REQUIRE(normalized_comp.HasValue());
    REQUIRE(normalized_comp.value().fragmentCount() == 1);
    REQUIRE(normalized_comp.value().fragment(0).IsConditional());

    const Conditional& outer = normalized_comp.value().fragment(0).AsConditional();
    REQUIRE(outer.branches.size() == 1);
    REQUIRE(outer.branches[0].content.size() == 1);
    REQUIRE(outer.branches[0].content[0].IsConditional());

    const Conditional& inner = outer.branches[0].content[0].AsConditional();
    REQUIRE(inner.branches.size() == 1);
    REQUIRE(inner.branches[0].content.size() == 1);
    CHECK(inner.branches[0].content[0].AsStaticText().text() ==
          "formal inner text");
    REQUIRE(inner.elseContent.has_value());
    REQUIRE(inner.elseContent->size() == 1);
    CHECK((*inner.elseContent)[0].AsStaticText().text() == "casual inner text");

    REQUIRE(outer.elseContent.has_value());
    REQUIRE(outer.elseContent->size() == 1);
    CHECK((*outer.elseContent)[0].AsStaticText().text() == "outer else text");
  }

  TEST_CASE(
      "a Conditional composition normalizes, publishes, and renders "
      "different branches for different RenderContexts") {
    EngineTestFixture fixture;

    auto cond =
        ConditionalBuilder()
            .If(Condition{.attribute = "level", .allowedValues = {"expert"}})
            .Then(Fragment::MakeStaticText("expert guidance"))
            .Else(Fragment::MakeStaticText("beginner guidance"))
            .build();

    CompositionDraftBuilder composition_builder("prompt.cond_render");
    composition_builder.AddConditional(std::move(cond));
    auto composition = fixture.engine.PublishComposition(
        composition_builder.build(), Version{1, 0});
    REQUIRE(composition.HasValue());

    fixture.engine.SetBlockNormalizer(std::make_shared<FakeBlockNormalizer>(
        Result<NormalizedBlockData>(NormalizedBlockData{
            .templ = "unused",
            .description = std::nullopt,
            .language = std::nullopt,
        })));

    auto normalize_result = fixture.engine.NormalizeComposition(
        CompositionNormalizationRequest{
            .source_composition_id = "prompt.cond_render",
            .style = SemanticStyle{.tone = std::string("warm")},
        });
    REQUIRE(normalize_result.HasValue());

    auto expert_render = fixture.engine.Render(
        normalize_result.value().composition_id,
        RenderContext{}.WithParam("level", "expert"));
    REQUIRE(expert_render.HasValue());
    CHECK(expert_render.value().text == "expert guidance");

    auto fallback_render = fixture.engine.Render(
        normalize_result.value().composition_id,
        RenderContext{}.WithParam("level", "novice"));
    REQUIRE(fallback_render.HasValue());
    CHECK(fallback_render.value().text == "beginner guidance");

    CHECK(expert_render.value().text != fallback_render.value().text);
  }
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build-rel --target core_tests --parallel && ./build-rel/tests/core_tests --test-suite="CompositionNormalization"`
Expected: FAIL. The three new test cases fail with `REQUIRE(result.HasValue())` failing (the engine still returns `InvalidParamType` for `Conditional`, since `NormalizeFragments` doesn't exist yet and `NormalizeComposition` hasn't been refactored). It's fine if this doesn't compile yet — the point of this step is confirming red before green; if it fails to compile because you also started the header/source edits, do this step before touching `engine.h`/`engine.cc`, or `git stash` those changes first, run the check, then restore them.

- [ ] **Step 3: Declare `NormalizeFragments` in `tf/engine.h`**

In `tf/engine.h`, find the `// ==================== Helpers ====================` comment near the end of the `private:` section (~line 464), immediately before `Result<Version> GetNextVersion(const BlockId& id, VersionBump bump);`. Add above it:

```cpp
  /**
   * Recursively normalizes a fragment list: StaticText via the configured
   * INormalizer (if request.normalize_static_text), BlockRef via the
   * configured IBlockNormalizer (deriving/publishing/caching a normalized
   * Block, same as today), Separator unchanged, and Conditional by
   * normalizing every branch's content and elseContent with this same
   * method. Used by NormalizeComposition and by
   * PreviewNormalizeComposition's fresh (non-cached) path.
   */
  [[nodiscard]] Result<std::vector<Fragment>> NormalizeFragments(
      const std::vector<Fragment>& fragments,
      const CompositionNormalizationRequest& request,
      const std::string& normalization_key_tag,
      std::vector<std::pair<BlockId, BlockId>>& rewritten_blocks);

```

- [ ] **Step 4: Implement `NormalizeFragments` in `tf/engine.cc`**

Add this as a new `Engine::` member function definition in `tf/engine.cc`, directly above `Result<NormalizedCompositionPreview> Engine::PreviewNormalizeComposition(...)` (currently line 875):

```cpp
Result<std::vector<Fragment>> Engine::NormalizeFragments(
    const std::vector<Fragment>& fragments,
    const CompositionNormalizationRequest& request,
    const std::string& normalization_key_tag,
    std::vector<std::pair<BlockId, BlockId>>& rewritten_blocks) {
  std::vector<Fragment> result;
  result.reserve(fragments.size());

  for (const auto& fragment : fragments) {
    if (fragment.IsSeparator()) {
      result.push_back(Fragment::MakeSeparator(fragment.AsSeparator().type));
      continue;
    }

    if (fragment.IsStaticText()) {
      std::string text = fragment.AsStaticText().text();
      if (request.normalize_static_text && normalizer_) {
        auto normalized = normalizer_->Normalize(text, request.style);
        if (normalized.HasError()) {
          return Result<std::vector<Fragment>>(normalized.error());
        }
        text = normalized.value();
      }
      result.push_back(Fragment::MakeStaticText(std::move(text)));
      continue;
    }

    if (fragment.IsConditional()) {
      const Conditional& cond = fragment.AsConditional();
      Conditional normalized_cond;
      normalized_cond.branches.reserve(cond.branches.size());
      for (const auto& branch : cond.branches) {
        Branch normalized_branch;
        normalized_branch.conditions = branch.conditions;
        auto normalized_content = NormalizeFragments(
            branch.content, request, normalization_key_tag, rewritten_blocks);
        if (normalized_content.HasError()) {
          return Result<std::vector<Fragment>>(normalized_content.error());
        }
        normalized_branch.content = std::move(normalized_content.value());
        normalized_cond.branches.push_back(std::move(normalized_branch));
      }
      if (cond.elseContent.has_value()) {
        auto normalized_else = NormalizeFragments(
            *cond.elseContent, request, normalization_key_tag, rewritten_blocks);
        if (normalized_else.HasError()) {
          return Result<std::vector<Fragment>>(normalized_else.error());
        }
        normalized_cond.elseContent = std::move(normalized_else.value());
      }
      result.push_back(Fragment::MakeConditional(std::move(normalized_cond)));
      continue;
    }

    const auto& block_ref = fragment.AsBlockRef();
    auto block_result = block_ref.version().has_value()
                            ? LoadBlock(block_ref.GetBlockId(), *block_ref.version())
                            : LoadBlock(block_ref.GetBlockId());
    if (block_result.HasError()) {
      return Result<std::vector<Fragment>>(block_result.error());
    }

    const Block source_block = block_result.value();
    const std::string derived_block_id = DerivedNormalizedBlockId(
        source_block, request.style, blockNormalizer_->Fingerprint());

    PublishedBlock published_block(derived_block_id, Version{1, 0});
    bool reused_cached_block = false;
    if (request.reuse_cached_blocks && blockRepo_) {
      auto existing = blockRepo_->LoadLatest(derived_block_id);
      if (!existing.HasError() &&
          HasTag(existing.value().tags(), normalization_key_tag)) {
        published_block = PublishedBlock(existing.value().Id(),
                                         existing.value().version());
        reused_cached_block = true;
      }
    }

    if (!reused_cached_block) {
      auto normalized =
          blockNormalizer_->NormalizeBlock({.source_block = source_block,
                                           .style = request.style});
      if (normalized.HasError()) {
        return Result<std::vector<Fragment>>(normalized.error());
      }

      const Template normalized_template(normalized.value().templ);
      if (PlaceholderSet(source_block.templ()) !=
          PlaceholderSet(normalized_template)) {
        return Result<std::vector<Fragment>>(
            Error{ErrorCode::InvalidParamType,
                  "Normalized block changed required placeholders"});
      }

      BlockDraftBuilder block_builder(derived_block_id);
      block_builder.WithType(source_block.type())
          .WithLanguage(
              normalized.value().language.value_or(source_block.language()))
          .WithDescription(
              normalized.value().description.value_or(source_block.description()))
          .WithTemplate(normalized_template)
          .WithDefaults(source_block.defaults())
          .WithParamSchema(source_block.param_schema());
      for (const auto& tag : source_block.tags()) {
        block_builder.WithTag(tag);
      }
      block_builder.WithTag("normalized");
      block_builder.WithTag(normalization_key_tag);

      Result<PublishedBlock> publish_result(Error{ErrorCode::StorageError, ""});
      if (blockRepo_->LoadLatest(derived_block_id).HasError()) {
        publish_result = PublishBlock(block_builder.build(), Version{1, 0});
      } else {
        publish_result = PublishBlock(block_builder.build(), VersionBump::Minor);
      }
      if (publish_result.HasError()) {
        return Result<std::vector<Fragment>>(publish_result.error());
      }
      published_block = publish_result.value();
    }

    BlockRef new_ref(published_block.ref().GetBlockId(), published_block.version(),
                     block_ref.LocalParams());
    result.push_back(Fragment::MakeBlockRef(std::move(new_ref)));
    rewritten_blocks.emplace_back(source_block.Id(), published_block.id());
  }

  return Result<std::vector<Fragment>>(std::move(result));
}

```

- [ ] **Step 5: Refactor `NormalizeComposition` to call `NormalizeFragments` and delete its Conditional rejection**

In `tf/engine.cc`, inside `Result<NormalizedCompositionResult> Engine::NormalizeComposition(...)` (currently starts at line 1033), replace the entire `for (const auto& fragment : source.fragments()) { ... }` loop (currently lines 1085-1189, from `std::vector<std::pair<BlockId, BlockId>> rewritten_blocks;` through the closing `}` of the loop) with:

```cpp
  std::vector<std::pair<BlockId, BlockId>> rewritten_blocks;
  const std::string normalization_key_tag = NormalizationKeyTag(normalization_key);
  auto normalized_fragments = NormalizeFragments(
      source.fragments(), request, normalization_key_tag, rewritten_blocks);
  if (normalized_fragments.HasError()) {
    return Result<NormalizedCompositionResult>(normalized_fragments.error());
  }

  for (auto& fragment : normalized_fragments.value()) {
    switch (fragment.type()) {
      case FragmentType::BlockRef:
        builder.AddBlockRef(std::move(fragment.AsBlockRef()));
        break;
      case FragmentType::StaticText:
        builder.AddStaticText(std::move(fragment.AsStaticText().content));
        break;
      case FragmentType::Separator:
        builder.AddSeparator(fragment.AsSeparator().type);
        break;
      case FragmentType::Conditional:
        builder.AddConditional(std::move(fragment.AsConditional()));
        break;
    }
  }
```

Everything after the loop (the `Result<PublishedComposition> publish_composition(...)` block through the end of the function) stays exactly as it is — do not touch it.

- [ ] **Step 6: Build and run the full test suite**

Run: `cmake --build build-rel --target core_tests --parallel && ./build-rel/tests/core_tests --test-suite="CompositionNormalization"`
Expected: PASS — all `CompositionNormalization` test cases, including the three new ones, pass. If a compile error mentions `Branch`/`Conditional` not visible in `engine.cc`'s anonymous namespace area, confirm `#include "fragment.h"` reaches there (it does transitively via `composition.h`, already included).

Then run the whole suite to confirm no regression elsewhere:

Run: `ctest --test-dir build-rel --output-on-failure`
Expected: PASS (`core_tests`, `cli_tests`, `tfe_smoke` all green).

- [ ] **Step 7: Commit**

```bash
git add tf/engine.h tf/engine.cc tests/test_main.cc
git commit -m "$(cat <<'EOF'
Recurse into Conditional in NormalizeComposition via new NormalizeFragments

Engine::NormalizeFragments is a new recursive private method that
normalizes StaticText/BlockRef/Separator exactly as the old flat loop
did, and additionally recurses into Conditional's branches and
elseContent with itself. NormalizeComposition now calls it instead of
rejecting any composition containing a Conditional fragment.

PreviewNormalizeComposition still rejects Conditional at this point --
that's Task 2/3 of the normalization-conditional-support plan.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: `Engine::FragmentTreeToPreviewText` — labeled preview text, wired into the fresh preview path

**Files:**
- Modify: `tf/engine.h` (add private method declaration, right after `NormalizeFragments`)
- Modify: `tf/engine.cc:875-1031` (`Engine::PreviewNormalizeComposition`, fresh path only — lines 948-1023 of the *current* file, i.e. everything after the `reuse_cached_blocks` block Task 3 will touch)
- Test: `tests/test_main.cc`, `TEST_SUITE("CompositionNormalization")`

**Interfaces:**
- Consumes: `Engine::NormalizeFragments` (Task 1).
- Produces: `Result<std::vector<std::string>> Engine::FragmentTreeToPreviewText(const std::vector<Fragment>& fragments, const std::optional<std::string>& delimiter) const` — private, const (only calls `LoadBlock`, never the LLM). `delimiter` is the `StructuralStyle::delimiter` in effect for the composition being previewed. Also introduces two small anonymous-namespace free functions in `engine.cc`: `RenderCondition(const Condition&) -> std::string` and `RenderBranchLabel(const std::vector<Condition>&, const char* keyword) -> std::string`.

- [ ] **Step 1: Write the failing test for the fresh preview path**

Add this test case inside `TEST_SUITE("CompositionNormalization")` in `tests/test_main.cc`, after the three tests added in Task 1:

```cpp
  TEST_CASE(
      "PreviewNormalizeComposition renders a Conditional as a labeled "
      "if/elif/else block") {
    EngineTestFixture fixture;

    auto cond =
        ConditionalBuilder()
            .If(Condition{.attribute = "language",
                          .allowedValues = {"ru", "en"}})
            .Then(Fragment::MakeStaticText("hello"))
            .If(Condition{.attribute = "tone",
                          .allowedValues = {"formal"},
                          .negate = true})
            .Then(Fragment::MakeStaticText("casual text"))
            .Else(Fragment::MakeStaticText("fallback"))
            .build();

    CompositionDraftBuilder composition_builder("prompt.cond_preview");
    composition_builder.AddConditional(std::move(cond));
    auto composition = fixture.engine.PublishComposition(
        composition_builder.build(), Version{1, 0});
    REQUIRE(composition.HasValue());

    fixture.engine.SetBlockNormalizer(std::make_shared<FakeBlockNormalizer>(
        Result<NormalizedBlockData>(NormalizedBlockData{
            .templ = "unused",
            .description = std::nullopt,
            .language = std::nullopt,
        })));

    auto preview = fixture.engine.PreviewNormalizeComposition(
        CompositionNormalizationRequest{
            .source_composition_id = "prompt.cond_preview",
            .style = SemanticStyle{.tone = std::string("warm")},
        });
    REQUIRE(preview.HasValue());

    const std::string& text = preview.value().preview_text;
    // allowedValues is an unordered_set, so a two-value set's rendered
    // order isn't guaranteed -- accept either.
    CHECK((text.find("[if language in {ru, en}]") != std::string::npos ||
          text.find("[if language in {en, ru}]") != std::string::npos));
    CHECK(text.find("hello") != std::string::npos);
    CHECK(text.find("[elif tone not in {formal}]") != std::string::npos);
    CHECK(text.find("casual text") != std::string::npos);
    CHECK(text.find("[else]") != std::string::npos);
    CHECK(text.find("fallback") != std::string::npos);
    CHECK(text.find("hello") < text.find("casual text"));
    CHECK(text.find("casual text") < text.find("fallback"));
  }
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build-rel --target core_tests --parallel && ./build-rel/tests/core_tests --test-suite="CompositionNormalization"`
Expected: FAIL — `REQUIRE(preview.HasValue())` fails (`PreviewNormalizeComposition`'s fresh path still rejects `Conditional`).

- [ ] **Step 3: Declare `FragmentTreeToPreviewText` in `tf/engine.h`**

Right after the `NormalizeFragments` declaration added in Task 1 Step 3, add:

```cpp
  /**
   * Converts an already-normalized fragment list into one preview-text
   * string per top-level fragment, with no LLM calls -- just reading
   * StaticText/Separator/Block content. A Conditional expands into a
   * labeled if/elif/else block; `delimiter` (the composition's effective
   * StructuralStyle::delimiter) is threaded into the recursion so a
   * branch's own multiple fragments join the same way Renderer does after
   * flattening a selected branch into the top-level fragment list.
   */
  [[nodiscard]] Result<std::vector<std::string>> FragmentTreeToPreviewText(
      const std::vector<Fragment>& fragments,
      const std::optional<std::string>& delimiter) const;

```

- [ ] **Step 4: Implement the label helpers and `FragmentTreeToPreviewText` in `tf/engine.cc`**

Add these two free functions to the anonymous namespace at the top of `tf/engine.cc`, right after the existing `PlaceholderSet` function (currently lines 178-181):

```cpp
std::string RenderCondition(const Condition& condition) {
  std::ostringstream stream;
  stream << condition.attribute << (condition.negate ? " not in {" : " in {");
  bool first = true;
  for (const auto& value : condition.allowedValues) {
    if (!first) {
      stream << ", ";
    }
    first = false;
    stream << value;
  }
  stream << "}";
  return stream.str();
}

std::string RenderBranchLabel(const std::vector<Condition>& conditions,
                              const char* keyword) {
  std::ostringstream stream;
  stream << "[" << keyword << " ";
  for (size_t i = 0; i < conditions.size(); ++i) {
    if (i > 0) {
      stream << " and ";
    }
    stream << RenderCondition(conditions[i]);
  }
  stream << "]";
  return stream.str();
}

std::string JoinWithDelimiter(const std::vector<std::string>& parts,
                              const std::optional<std::string>& delimiter) {
  std::ostringstream stream;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0 && delimiter.has_value()) {
      stream << *delimiter;
    }
    stream << parts[i];
  }
  return stream.str();
}

```

Then add `Engine::FragmentTreeToPreviewText` as a new member function, directly above `Result<NormalizedCompositionPreview> Engine::PreviewNormalizeComposition(...)`:

```cpp
Result<std::vector<std::string>> Engine::FragmentTreeToPreviewText(
    const std::vector<Fragment>& fragments,
    const std::optional<std::string>& delimiter) const {
  std::vector<std::string> texts;
  texts.reserve(fragments.size());

  for (const auto& fragment : fragments) {
    if (fragment.IsSeparator()) {
      texts.push_back(fragment.AsSeparator().toString());
      continue;
    }

    if (fragment.IsStaticText()) {
      texts.push_back(fragment.AsStaticText().text());
      continue;
    }

    if (fragment.IsConditional()) {
      const Conditional& cond = fragment.AsConditional();
      std::ostringstream block;
      bool is_first_branch = true;
      for (const auto& branch : cond.branches) {
        auto branch_texts = FragmentTreeToPreviewText(branch.content, delimiter);
        if (branch_texts.HasError()) {
          return Result<std::vector<std::string>>(branch_texts.error());
        }
        if (!is_first_branch) {
          block << "\n";
        }
        block << RenderBranchLabel(branch.conditions,
                                   is_first_branch ? "if" : "elif")
              << "\n" << JoinWithDelimiter(branch_texts.value(), delimiter);
        is_first_branch = false;
      }
      if (cond.elseContent.has_value()) {
        auto else_texts = FragmentTreeToPreviewText(*cond.elseContent, delimiter);
        if (else_texts.HasError()) {
          return Result<std::vector<std::string>>(else_texts.error());
        }
        block << "\n[else]\n" << JoinWithDelimiter(else_texts.value(), delimiter);
      }
      texts.push_back(block.str());
      continue;
    }

    const auto& block_ref = fragment.AsBlockRef();
    auto block_result = block_ref.version().has_value()
                            ? LoadBlock(block_ref.GetBlockId(), *block_ref.version())
                            : LoadBlock(block_ref.GetBlockId());
    if (block_result.HasError()) {
      return Result<std::vector<std::string>>(block_result.error());
    }
    texts.push_back(block_result.value().templ().Content());
  }

  return Result<std::vector<std::string>>(std::move(texts));
}

```

- [ ] **Step 5: Refactor `PreviewNormalizeComposition`'s fresh path**

In `tf/engine.cc`, inside `Result<NormalizedCompositionPreview> Engine::PreviewNormalizeComposition(...)`, find the fresh-path section (currently lines 948-1023): starts right after the `reuse_cached_blocks` block's closing `}` (do not touch that block yet — Task 3 handles it), begins with:

```cpp
  std::vector<std::pair<BlockId, BlockId>> rewritten_blocks;
  std::vector<std::string> fragment_texts;
  fragment_texts.reserve(source.fragments().size());

  for (const auto& fragment : source.fragments()) {
```

...and ends with the function's closing `return Result<NormalizedCompositionPreview>(NormalizedCompositionPreview{...});` block. Replace that entire span (from `std::vector<std::pair<BlockId, BlockId>> rewritten_blocks;` through the end of the function) with:

```cpp
  std::vector<std::pair<BlockId, BlockId>> rewritten_blocks;
  const std::string normalization_key_tag = NormalizationKeyTag(normalization_key);
  auto normalized_fragments = NormalizeFragments(
      source.fragments(), request, normalization_key_tag, rewritten_blocks);
  if (normalized_fragments.HasError()) {
    return Result<NormalizedCompositionPreview>(normalized_fragments.error());
  }

  const auto style = EffectiveStyle(source);
  auto fragment_texts =
      FragmentTreeToPreviewText(normalized_fragments.value(), style.delimiter);
  if (fragment_texts.HasError()) {
    return Result<NormalizedCompositionPreview>(fragment_texts.error());
  }

  return Result<NormalizedCompositionPreview>(NormalizedCompositionPreview{
      .composition_id = derived_composition_id,
      .preview_text = ApplyStructuralStyle(fragment_texts.value(), style),
      .rewritten_blocks = std::move(rewritten_blocks),
  });
}
```

(The trailing `}` above closes the function itself.)

- [ ] **Step 6: Build and run the tests**

Run: `cmake --build build-rel --target core_tests --parallel && ./build-rel/tests/core_tests --test-suite="CompositionNormalization"`
Expected: PASS — all tests including the new one from Step 1.

Run: `ctest --test-dir build-rel --output-on-failure`
Expected: PASS (no regression).

- [ ] **Step 7: Commit**

```bash
git add tf/engine.h tf/engine.cc tests/test_main.cc
git commit -m "$(cat <<'EOF'
Render Conditional as a labeled if/elif/else block in preview text

FragmentTreeToPreviewText is a new recursive, LLM-free private method
that turns an already-normalized fragment list into preview text,
expanding Conditional into "[if <cond>]\n<text>\n[elif ...]\n...
\n[else]\n<text>". PreviewNormalizeComposition's fresh path now calls
NormalizeFragments + FragmentTreeToPreviewText instead of its own flat
loop, and no longer rejects Conditional.

The reuse_cached_blocks fast path still rejects Conditional and still
has its own separate fragment-walking loop -- that's Task 3.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: Cache-reuse preview path — route through `FragmentTreeToPreviewText`, add normalizer call-counting

**Files:**
- Modify: `tests/test_main.cc` (extend `FakeBlockNormalizer`, currently lines 119-138; add one test)
- Modify: `tf/engine.cc:905-943` (`Engine::PreviewNormalizeComposition`'s `reuse_cached_blocks` block)

**Interfaces:**
- Consumes: `Engine::NormalizeFragments` (Task 1), `Engine::FragmentTreeToPreviewText` (Task 2).
- Produces: `FakeBlockNormalizer::call_count() const noexcept -> int` (test-only, for asserting no extra LLM calls happened).

- [ ] **Step 1: Extend `FakeBlockNormalizer` with a call counter and write the failing test**

In `tests/test_main.cc`, replace the `FakeBlockNormalizer` class (currently lines 119-138):

```cpp
class FakeBlockNormalizer final : public IBlockNormalizer {
 public:
  explicit FakeBlockNormalizer(Result<NormalizedBlockData> result,
                               std::string fingerprint = "fake-block-normalizer")
      : result_(std::move(result)), fingerprint_(std::move(fingerprint)) {}

  [[nodiscard]] Result<NormalizedBlockData> NormalizeBlock(
      const BlockNormalizationRequest&) const override {
    if (result_.HasError()) {
      return Result<NormalizedBlockData>(result_.error());
    }
    return Result<NormalizedBlockData>(result_.value());
  }

  [[nodiscard]] std::string Fingerprint() const override { return fingerprint_; }

 private:
  Result<NormalizedBlockData> result_;
  std::string fingerprint_;
};
```

with:

```cpp
class FakeBlockNormalizer final : public IBlockNormalizer {
 public:
  explicit FakeBlockNormalizer(Result<NormalizedBlockData> result,
                               std::string fingerprint = "fake-block-normalizer")
      : result_(std::move(result)), fingerprint_(std::move(fingerprint)) {}

  [[nodiscard]] Result<NormalizedBlockData> NormalizeBlock(
      const BlockNormalizationRequest&) const override {
    ++call_count_;
    if (result_.HasError()) {
      return Result<NormalizedBlockData>(result_.error());
    }
    return Result<NormalizedBlockData>(result_.value());
  }

  [[nodiscard]] std::string Fingerprint() const override { return fingerprint_; }

  /**
   * Number of times NormalizeBlock has been called. Lets a test assert
   * that a cache-reuse path made zero additional LLM-backed calls.
   */
  [[nodiscard]] int call_count() const noexcept { return call_count_; }

 private:
  Result<NormalizedBlockData> result_;
  std::string fingerprint_;
  mutable int call_count_ = 0;
};
```

Then add this test case inside `TEST_SUITE("CompositionNormalization")`, after the test added in Task 2:

```cpp
  TEST_CASE(
      "PreviewNormalizeComposition's reuse_cached_blocks fast path shows "
      "Conditional branches without calling the block normalizer again") {
    EngineTestFixture fixture;

    Block expert_block =
        fixture.createAndPublishBlock("role.expert", "Expert guide.");

    auto cond =
        ConditionalBuilder()
            .If(Condition{.attribute = "level", .allowedValues = {"expert"}})
            .Then(Fragment::MakeBlockRef(
                BlockRef("role.expert", expert_block.version())))
            .Else(Fragment::MakeStaticText("fallback text"))
            .build();

    CompositionDraftBuilder composition_builder("prompt.cond_cache");
    composition_builder.AddConditional(std::move(cond));
    auto composition = fixture.engine.PublishComposition(
        composition_builder.build(), Version{1, 0});
    REQUIRE(composition.HasValue());

    auto fake_normalizer = std::make_shared<FakeBlockNormalizer>(
        Result<NormalizedBlockData>(NormalizedBlockData{
            .templ = "Normalized expert guide.",
            .description = std::nullopt,
            .language = std::nullopt,
        }));
    fixture.engine.SetBlockNormalizer(fake_normalizer);

    CompositionNormalizationRequest request{
        .source_composition_id = "prompt.cond_cache",
        .style = SemanticStyle{.tone = std::string("warm")},
        .reuse_cached_blocks = true,
    };

    // First preview: no derived block and no derivative composition exist
    // yet, so this goes through the fresh path and calls the normalizer
    // exactly once (for the one BlockRef in the tree).
    auto first_preview = fixture.engine.PreviewNormalizeComposition(request);
    REQUIRE(first_preview.HasValue());
    CHECK(fake_normalizer->call_count() == 1);
    CHECK(first_preview.value().preview_text.find("Normalized expert guide.") !=
          std::string::npos);
    CHECK(first_preview.value().preview_text.find("fallback text") !=
          std::string::npos);
    CHECK(first_preview.value().preview_text.find("[if level in {expert}]") !=
          std::string::npos);
    CHECK(first_preview.value().preview_text.find("[else]") != std::string::npos);

    // PreviewNormalizeComposition never publishes a derivative composition
    // itself (only NormalizeComposition does) -- publish one now so the
    // second preview call below has something to find in its
    // composition-level cache check. The block-level cache (checked by
    // NormalizeFragments inside this call) already has "norm.role.expert"
    // tagged from the first preview above, so this does NOT call the
    // normalizer again.
    auto normalize_result = fixture.engine.NormalizeComposition(request);
    REQUIRE(normalize_result.HasValue());
    CHECK(fake_normalizer->call_count() == 1);

    // Second preview: the derivative composition now exists with a
    // matching style, so this takes the reuse_cached_blocks fast path --
    // reading straight from the stored, already-normalized Conditional via
    // FragmentTreeToPreviewText, with zero further normalizer calls.
    auto second_preview = fixture.engine.PreviewNormalizeComposition(request);
    REQUIRE(second_preview.HasValue());
    CHECK(second_preview.value().preview_text.find("Normalized expert guide.") !=
          std::string::npos);
    CHECK(second_preview.value().preview_text.find("fallback text") !=
          std::string::npos);
    CHECK(second_preview.value().preview_text.find("[if level in {expert}]") !=
          std::string::npos);
    CHECK(second_preview.value().preview_text.find("[else]") != std::string::npos);
    CHECK(fake_normalizer->call_count() == 1);
  }
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build-rel --target core_tests --parallel && ./build-rel/tests/core_tests --test-suite="CompositionNormalization"`
Expected: FAIL — the `first_preview` assertion `REQUIRE(first_preview.HasValue())` fails, since the `reuse_cached_blocks` block's Conditional check hasn't been touched yet and the fresh path (Task 2) doesn't run in this scenario until the composition-level cache is populated... actually on the *first* call here there's no cached composition yet, so it goes through the Task-2-fixed fresh path and should already pass that part. The failure will be on `normalize_result`/`second_preview`'s reuse_cached_blocks-path assertions, since that block (lines 905-943) still has its own separate loop that rejects `Conditional` — confirm the failure is in the `reuse_cached_blocks` block specifically (the error message will be "Normalization does not yet support compositions containing Conditional content" surfacing through `second_preview.HasError()` instead of `HasValue()`).

- [ ] **Step 3: Refactor the `reuse_cached_blocks` block in `PreviewNormalizeComposition`**

In `tf/engine.cc`, find (currently lines 905-943):

```cpp
  if (request.reuse_cached_blocks && compRepo_) {
    auto existing = compRepo_->LoadLatest(derived_composition_id);
    if (!existing.HasError() && existing.value().GetStyleProfile().has_value() &&
        StyleKey(existing.value().GetStyleProfile()->semantic) ==
            StyleKey(request.style)) {
      std::vector<std::string> fragment_texts;
      fragment_texts.reserve(existing.value().fragments().size());
      for (const auto& fragment : existing.value().fragments()) {
        if (fragment.IsSeparator()) {
          fragment_texts.push_back(fragment.AsSeparator().toString());
          continue;
        }
        if (fragment.IsStaticText()) {
          fragment_texts.push_back(fragment.AsStaticText().text());
          continue;
        }
        if (fragment.IsConditional()) {
          return Result<NormalizedCompositionPreview>(
              Error{ErrorCode::InvalidParamType,
                    "Normalization does not yet support compositions "
                    "containing Conditional content"});
        }
        const auto& block_ref = fragment.AsBlockRef();
        auto block_result = block_ref.version().has_value()
                                ? LoadBlock(block_ref.GetBlockId(),
                                            *block_ref.version())
                                : LoadBlock(block_ref.GetBlockId());
        if (block_result.HasError()) {
          return Result<NormalizedCompositionPreview>(block_result.error());
        }
        fragment_texts.push_back(block_result.value().templ().Content());
      }

      return Result<NormalizedCompositionPreview>(NormalizedCompositionPreview{
          .composition_id = derived_composition_id,
          .preview_text =
              ApplyStructuralStyle(fragment_texts, EffectiveStyle(existing.value())),
          .rewritten_blocks = {},
      });
    }
  }
```

Replace it with:

```cpp
  if (request.reuse_cached_blocks && compRepo_) {
    auto existing = compRepo_->LoadLatest(derived_composition_id);
    if (!existing.HasError() && existing.value().GetStyleProfile().has_value() &&
        StyleKey(existing.value().GetStyleProfile()->semantic) ==
            StyleKey(request.style)) {
      const auto style = EffectiveStyle(existing.value());
      auto fragment_texts =
          FragmentTreeToPreviewText(existing.value().fragments(), style.delimiter);
      if (fragment_texts.HasError()) {
        return Result<NormalizedCompositionPreview>(fragment_texts.error());
      }

      return Result<NormalizedCompositionPreview>(NormalizedCompositionPreview{
          .composition_id = derived_composition_id,
          .preview_text = ApplyStructuralStyle(fragment_texts.value(), style),
          .rewritten_blocks = {},
      });
    }
  }
```

- [ ] **Step 4: Build and run the tests**

Run: `cmake --build build-rel --target core_tests --parallel && ./build-rel/tests/core_tests --test-suite="CompositionNormalization"`
Expected: PASS — all `CompositionNormalization` tests, including the new cache-reuse test.

Run: `ctest --test-dir build-rel --output-on-failure`
Expected: PASS (no regression across `core_tests`, `cli_tests`, `tfe_smoke`).

- [ ] **Step 5: Commit**

```bash
git add tf/engine.cc tests/test_main.cc
git commit -m "$(cat <<'EOF'
Route reuse_cached_blocks preview path through FragmentTreeToPreviewText

The composition-level cache-reuse fast path in
PreviewNormalizeComposition had its own hand-rolled fragment-walking
loop (StaticText/Separator/BlockRef only, no LLM calls) that duplicated
the fresh path's leaf-handling and rejected Conditional outright. It
now calls FragmentTreeToPreviewText directly on the cached derivative
composition's fragments -- same method the fresh path uses (Task 2),
so it picks up Conditional support for free and the duplication is
gone.

FakeBlockNormalizer (tests/test_main.cc) gained a call_count() so a
test can assert this path makes zero additional block-normalizer
calls, proving it really is cache-reuse and not a second LLM pass.

This closes out the Conditional-in-Normalization design
(docs/superpowers/specs/2026-09-23-normalization-conditional-support-design.md)
-- Engine::NormalizeComposition and Engine::PreviewNormalizeComposition
(both paths) now fully support Conditional fragments.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

## Self-Review Notes

- **Spec coverage:** `NormalizeFragments` (spec §1) → Task 1. `FragmentTreeToPreviewText` + label format + `delimiter` threading (spec §2) → Tasks 2–3. `rewritten_blocks` nested accumulation (spec §3) → covered by Task 1's test asserting both nested rewrites appear. Error handling (spec, no new codes, first-error-wins) → inherent in every `Result<...>::HasError()` early-return in Tasks 1–3's code. All five spec Testing-section items → one test each in Tasks 1–3 (the `elseContent` defensive-empty case is explicitly noted in the spec as not independently testable through the public API, so no task adds a test for it).
- **Placeholder scan:** none — every step has complete code, exact file/line anchors, and concrete `Run:`/`Expected:` pairs.
- **Type consistency:** `NormalizeFragments`'s signature (declared Task 1 Step 3, defined Task 1 Step 4) matches its two call sites (Task 1 Step 5, Task 2 Step 5) exactly. `FragmentTreeToPreviewText`'s signature (declared Task 2 Step 3, defined Task 2 Step 4) matches its two call sites (Task 2 Step 5, Task 3 Step 3) exactly, including the `const` qualifier and the `delimiter` parameter at both.
