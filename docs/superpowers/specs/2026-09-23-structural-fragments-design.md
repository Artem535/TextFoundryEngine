# Structural Fragments: `Group` (lists) and `BlockElement` (heading/quote/code) — Design

## Context

`Fragment` today is `std::variant<BlockRef, StaticText, Separator, Conditional>`. All
composition "shape" is flat: a `Composition` is just an ordered list of fragments
joined by `StructuralStyle::delimiter`. There is no way to express "these N
fragments form one list", or "this is a heading/quote/code block" — a block's
`BlockType` is explicitly documented as pure taxonomy that "does NOT affect
Template Expansion" (`tf/block_type.hpp:14-16`), and the project's PRD formalizes
this as a deliberate principle: **Type is an Enum, not a Class** — using an enum
instead of polymorphic dispatch keeps `Renderer` uniform and deterministic
regardless of block category.

This spec adds two new `Fragment` variants that give composition *authors* a way
to express structure explicitly, at composition-assembly time, without touching
`BlockType` or making `Renderer` branch on block category:

- **`Group`** — an ordered, possibly-nested list (bulleted or numbered).
- **`BlockElement`** — a single structural wrapper: heading, blockquote, or
  fenced code block.

Both follow the same architectural precedent already set by `Conditional`
(`tf/fragment.h:88-146`, added in commit `998d2d9` and since extended through
`404bba7`/`de45f53`/`fb7ebb6`): a new `Fragment` variant, recursive, resolved by
`Renderer`, persisted through `obx_utils.hpp`, constructible via the CLI's
`--from-json`. `BlockType` is untouched — this is a composition-level decision,
matching what was agreed in conversation (option "specialization lives in how
blocks are assembled", not "in the block itself").

## Why two variants and not one

`Group` and `BlockElement` share the same *plumbing* (validation, ObjectBox
serialization, CLI DTO, `Renderer`, `VisitBlockRefs`) but have a different
*shape*: `Group` accumulates a variable number of items (each itself a fragment
list); `BlockElement` wraps exactly one fragment list with one scalar attribute.
Forcing them into a single variant with optional fields for "items" vs. "content"
would be a Data Clump / primitive-obsession trade the wrong way — two small,
honest structs are clearer than one struct with fields that don't all apply.

## Explicitly out of scope

- **Tables.** A table is a 2-dimensional structure (rows × cells), not a
  refinement of `Group`'s 1-dimensional item list. It needs its own data shape,
  its own rendering logic (column handling), and is comparable in size to this
  entire spec — it gets its own future spec, not a bolt-on here.
- **Normalization support** (`Engine::NormalizeFragments`,
  `Engine::FragmentTreeToPreviewText`). Exactly the same two-phase pattern
  already used for `Conditional`: `Conditional`'s core engine support shipped
  first (data model → ObjectBox → Renderer → builder), and `NormalizeComposition`
  explicitly *rejected* any composition containing a `Conditional` (commit
  `325e7c4`) until a dedicated follow-up spec added recursive normalization
  support (this session's `2026-09-23-normalization-conditional-support-design.md`).
  This spec repeats that split deliberately: `NormalizeComposition` and
  `PreviewNormalizeComposition` will explicitly reject `Group`/`BlockElement`
  fragments (new, or reuse `InvalidParamType`? — see Validation) until a
  follow-up spec extends `NormalizeFragments`/`FragmentTreeToPreviewText` the
  same way it was just extended for `Conditional`. Passing them through
  *unresolved* would silently skip normalizing any `BlockRef` nested inside a
  list item or heading, producing a partially-normalized composition — worse
  than a clear rejection.
- **`CompositionBlockRewrite`** (`tf/engine.cc`, the block-preserving rewrite
  workflow) — separate concern, separate follow-up if needed, same reasoning
  the Conditional-in-Normalization plan used to exclude it.
- **Real multi-format output** (`OutputFormat`/HTML/XML rendering). Rendering
  stays plain text, using the same markdown-flavored conventions the renderer
  already has (`Separator::Hr` already renders `"\n---\n"`, i.e. a markdown
  horizontal rule — `tf/fragment.cc:9-19`). `Group`/`BlockElement` extend that
  existing convention (`- `/`1. ` bullets, `#`/`> `/`` ``` `` fences); they do
  not reintroduce the `OutputFormat` enum that was removed on day one of the
  project (`df1e5a2`, "Убран format output") and never revisited.

## Data model

New file? No — same pattern as `Conditional`/`Branch`: add to `tf/fragment.h`,
implement in `tf/fragment.cc`.

```cpp
enum class GroupKind { Bulleted, Numbered };

/**
 * Group - an ordered list of items, each item itself a fragment list (so an
 * item can contain a nested Group, StaticText, BlockRef, or Conditional).
 * Rendering is entirely a Renderer concern: Group carries no visual detail
 * beyond kind (bulleted vs numbered) -- indentation, markers, and line
 * joining are computed at render time (see Rendering below).
 */
struct Group {
  GroupKind kind;
  std::vector<std::vector<Fragment>> items;

  [[nodiscard]] Error validate(bool isDraftContext) const;
};

enum class BlockElementKind { Heading, Quote, CodeBlock };

/**
 * BlockElement - a single structural wrapper around a fragment list.
 * `attr` is kind-specific and optional in meaning, not in storage (always a
 * string, may be empty): heading level as a decimal string ("1".."6"),
 * fenced code block language (may be empty -- "no language specified"),
 * unused (kept empty) for Quote.
 */
struct BlockElement {
  BlockElementKind kind;
  std::string attr;
  std::vector<Fragment> content;

  [[nodiscard]] Error validate(bool isDraftContext) const;
};
```

`Fragment` gains both variants, following the exact existing pattern for every
other variant (constructor, `MakeX` factory, `IsX`, `AsX` `&`/`const&`, `GetX`
nullable accessor, `FragmentType` enum entry):

```cpp
enum class FragmentType {
  BlockRef, StaticText, Separator, Conditional, Group, BlockElement
};
```

`Fragment` gains `MakeGroup(Group)`, following the exact same shape as
`MakeConditional(Conditional cond)` (`tf/fragment.h:178-180`) — a raw factory
taking the already-built struct, used directly by `ResolveConditionals`'s new
`Group` branch below and by `GroupBuilder::build()`'s callers.

Additional named factories on `Fragment` for the three `BlockElement` kinds,
matching how a caller actually wants to construct one (parallel to how
`ConditionalBuilder` hides branch/else bookkeeping, but here a full builder
class is unnecessary — there's nothing to accumulate beyond `content`):

```cpp
[[nodiscard]] static Fragment MakeBlockElement(BlockElement element);  // raw, like MakeConditional
[[nodiscard]] static Fragment MakeHeading(int level, std::vector<Fragment> content);
[[nodiscard]] static Fragment MakeQuote(std::vector<Fragment> content);
[[nodiscard]] static Fragment MakeCodeBlock(std::string language, std::vector<Fragment> content);
```

`MakeHeading`/`MakeQuote`/`MakeCodeBlock` just build a `BlockElement` and encode
`attr` (`std::to_string(level)` for `Heading`, `language` for `CodeBlock`, `""`
for `Quote`) — no range validation at construction time, matching how `BlockRef`
isn't validated at construction either; validation happens at `validate()` time,
the same as everything else in this codebase.

`GroupBuilder`, for `Group`, mirrors `ConditionalBuilder`'s existence but is
simpler (no branch/else state machine — items don't chain the way `If`/`Then`
does):

```cpp
class GroupBuilder {
 public:
  explicit GroupBuilder(GroupKind kind);
  GroupBuilder& Item(std::vector<Fragment> content);
  GroupBuilder& Item(Fragment single);  // convenience: wraps in a one-element vector
  [[nodiscard]] Group build();
 private:
  Group group_;
};
```

## Validation

Two new `ErrorCode`s (matching the precedent of `EmptyConditional`/
`EmptyBranchConditions`/`MissingElseBranch` added for `Conditional`):

```cpp
EmptyGroup,           ///< Group has zero items
InvalidHeadingLevel,  ///< BlockElement is Heading and attr isn't "1".."6"
```

`Group::validate`: `items.empty()` → `Error::EmptyGroup()`. For each item, for
each fragment in that item, recurse `fragment.validate(isDraftContext)` and
return the first error, same first-error-wins style as `Conditional::validate`
(`tf/fragment.cc:35-64`). An individual item being an empty fragment list is
**not** an error — matches the precedent that `Conditional::elseContent` being
`Some(empty vector)` is explicitly valid ("else renders nothing").

`BlockElement::validate`: if `kind == Heading`, parse `attr` as an integer and
require it in `[1, 6]`; anything else (unparseable, out of range) →
`Error::InvalidHeadingLevel()`. For `Quote`/`CodeBlock`, `attr` is unconstrained
(an empty `CodeBlock` language is valid — "unspecified language", same
open-world spirit as `Block::language` defaulting to `"en"` rather than being
required). Then recurse `validate()` into every fragment in `content`.

`Fragment::validate` (`tf/fragment.cc:109-123`) gains two more `if constexpr`
branches dispatching to `Group::validate`/`BlockElement::validate`.

## Rendering (`tf/renderer.cc`)

Two behaviors are needed, and they're different in kind:

**1. Resolving `Conditional` nested inside `Group`/`BlockElement`, and vice
versa.** `Group` inside `Conditional` already works with zero changes: a
`Group` placed inside `Branch::content` or `Conditional::elseContent` is just
another `Fragment` in a `vector<Fragment>`, and `ResolveConditionals`
(`tf/renderer.cc:200-249`) already recurses into those. The other direction
does **not** work yet: `ResolveConditionals`'s loop only branches on
`fragment.IsConditional()` — anything else, including a `Group`, is pushed
through unchanged (`tf/renderer.cc:206-209`), so a `Conditional` nested inside
a `Group` item would never get resolved, and would reach `RenderFragment`
still as an unresolved `Conditional`, hitting the "unreachable in practice"
error path (`tf/renderer.cc:144-149`) — which would no longer be unreachable.
Fix: `ResolveConditionals` gets two more branches that **reconstruct** (not
flatten) `Group`/`BlockElement`, recursing into their nested content:

```cpp
if (fragment.IsGroup()) {
  const Group& group = fragment.AsGroup();
  Group resolvedGroup{.kind = group.kind};
  resolvedGroup.items.reserve(group.items.size());
  for (const auto& item : group.items) {
    resolvedGroup.items.push_back(ResolveConditionals(item, params));
  }
  resolved.push_back(Fragment::MakeGroup(std::move(resolvedGroup)));
  continue;
}
if (fragment.IsBlockElement()) {
  const BlockElement& elem = fragment.AsBlockElement();
  resolved.push_back(Fragment::MakeBlockElement(BlockElement{
      .kind = elem.kind, .attr = elem.attr,
      .content = ResolveConditionals(elem.content, params)}));
  continue;
}
```

This is the key structural difference from `Conditional`: `Conditional`
*selects and flattens* one branch into the parent list (`resolved.insert(...)`
at `tf/renderer.cc:243-245`); `Group`/`BlockElement` never flatten — they
always occupy exactly one slot in the resolved list, just with their nested
content resolved.

**2. Turning a resolved `Group`/`BlockElement` into text.** This is new
rendering logic, added as two private `Renderer` methods alongside
`ExpandBlockRef`:

```cpp
[[nodiscard]] Result<std::string> RenderGroup(
    const Group& group, const RenderContext& context, size_t depth,
    std::vector<std::pair<BlockId, Version>>& blocksUsed) const;

[[nodiscard]] Result<std::string> RenderBlockElement(
    const BlockElement& element, const RenderContext& context,
    std::vector<std::pair<BlockId, Version>>& blocksUsed) const;
```

`RenderFragment`'s switch (`tf/renderer.cc:131-153`) gains matching cases that
call these.

**Item-content joining is a deliberate, explicit decision, not an assumption**
(this is the mistake the Conditional-preview spec caught in its own
self-review, and this spec makes the call up front instead): fragments inside
one `Group` item (or one `BlockElement`'s `content`) join with **plain
concatenation (`""`)**, *not* `StructuralStyle::delimiter`. Reasoning: unlike
`Conditional`'s branch content, item content is never spliced into the
top-level fragment list, so there is no real render-time behavior it needs to
match — it's genuinely a new, self-contained joining decision, and direct
concatenation (`"BlockRef text" + "!" from a following StaticText`) is the
least surprising default for content meant to read as one line/paragraph.
`RenderGroup`/`RenderBlockElement` do **not** need `StructuralStyle` threaded
into them for this reason — no new parameter beyond what's listed above.

**`Group` line format:**
- Each item renders to one or more lines: concatenate that item's fragment
  content (recursing into `RenderFragment` for each fragment, `RenderGroup` at
  `depth + 1` for a nested `Group` found among them), producing the item's own
  text, then prefix the *first* line with the marker (`"- "` for `Bulleted`,
  `"{1-based index}. "` for `Numbered`, numbering restarts at 1 in every
  nested `Group`) and indent every line the item contributes (including lines
  from a nested `Group`) by `2 * depth` spaces.
- Items join with `"\n"` — a hard requirement (list items must be on separate
  lines), not delimiter-controlled.
- The whole multi-line block is returned as a single string — same as any
  other fragment's rendered text — and takes its place as one entry in
  `Render()`'s `fragmentTexts` (`tf/renderer.cc:47-61`), subject to the outer
  `ApplyStructuralStyle` (`blockWrapper`/`delimiter`/`preamble`/`postamble`)
  exactly like everything else at the top level.

**`BlockElement` format** (plain-text/markdown-flavored, matching
`Separator::Hr`'s existing `"\n---\n"` convention):
- `Heading`: parse `attr` as an int (defensively default to `1` if somehow
  unparseable at render time — same "validate() guarantees this for published
  compositions, Render() defends anyway" posture as
  `ResolveConditionals`'s `elseContent` check at `tf/renderer.cc:228-240`) →
  `string(level, '#') + " " + content`.
- `Quote`: split `content` on `"\n"`, prefix every line with `"> "`, rejoin
  with `"\n"`.
- `CodeBlock`: `` "```" + attr + "\n" + content + "\n```" ``.

## `VisitBlockRefs` (`tf/fragment.h:296-297`, `tf/fragment.cc:125-142`)

Extended with two more branches, recursing into `Group`'s items and
`BlockElement`'s content — otherwise any engine-level code relying on "see
every reachable `BlockRef`" (usage checks, future rewrite passes) would
silently miss `BlockRef`s nested inside a list or heading.

## Persistence (`tf/obx_utils.hpp`)

Same hand-rolled `rfl::Generic` encode/decode pattern already used for
`Conditional`/`Branch`/`Condition` (`tf/obx_utils.hpp:350-452`), stored inside
the existing `conditionalJson`-style string field mechanism (or a sibling
field — implementer's call, matching whatever `ObxFragment` already does for
`Conditional`'s storage column). New functions: `GroupToGeneric`/
`GenericToGroup`, `BlockElementToGeneric`/`GenericToBlockElement`, wired into
`FragmentToGeneric`/`GenericToFragment`'s switch (`tf/obx_utils.hpp:454-531`)
with `"group"`/`"block_element"` type tags.

**Mandatory**: every `GenericToX` function must materialize each
`obj.get(...).value()...` chain into a **named variable before looping**, per
the comment already in `GenericToCondition` (`tf/obx_utils.hpp:366-376`) — this
is not a style preference, it's the fix for a real UB bug (dangling reference
into a destroyed temporary, GCC-13-only) already hit once this session in this
exact file. Do not write a fresh `for (const auto& x : obj.get(...).value().to_array().value())` anywhere in the new code.

`GroupKind`/`BlockElementKind` get `ToString`/`FromString` helpers in
`tf/block_type.hpp`, next to `SeparatorTypeToString`/etc. (same kind of small
closed enum needing string round-tripping).

## CLI / DTO layer (`cli/dto.cc`)

`cli/dto.cc` has its own, separate `FragmentType` switch and Conditional DTO
conversion (`ToConditional`, the `FragmentType::Conditional` case at line 141,
`ConditionalFragmentDto` at line 215) parallel to `obx_utils.hpp`'s — this is
how `tfe comp create --from-json` / `--json` construct and emit `Conditional`
content today. `Group`/`BlockElement` need the same treatment (new DTO
structs, `ToGroup`/`ToBlockElement` conversion functions, new
`FragmentType::Group`/`FragmentType::BlockElement` switch cases) — otherwise
these new fragment types would be constructible only from C++ code, not from
the CLI, defeating the point of adding them as composition-authoring
primitives. This is in scope (unlike Normalization): it's the same kind of
"engine understands this fragment type" work as the Renderer/ObjectBox pieces
above, not the separate LLM-rewrite concern.

## Testing

- `Group::validate` rejects zero items; accepts an item with zero fragments.
- `BlockElement::validate` (Heading) rejects `attr` = `"0"`, `"7"`, `"abc"`,
  `""`; accepts `"1"`..`"6"`.
- Flat bulleted list renders with `"- "` markers, one per line.
- Flat numbered list renders `"1. "`, `"2. "`, ... markers.
- Nested list (an item containing another `Group`) renders the sub-list
  indented under that item, numbering restarting at 1 inside the nested list.
- A `Group` item containing a `BlockRef` renders that block's expanded
  template as the item's text.
- `Conditional` nested inside a `Group` item resolves to the matching
  branch's content at render time (proves the new `ResolveConditionals`
  branches work, not just that they compile).
- A `Group` nested inside a `Conditional` branch (the direction that already
  worked) still resolves correctly — regression coverage, not new behavior.
- Each `BlockElement` kind (`Heading` level 2, `Quote`, `CodeBlock` with a
  language) renders its expected markdown-flavored text.
- ObjectBox round-trip: publish a composition containing a `Group` and a
  `BlockElement`, reload it, assert structural equality (mirrors the existing
  `Conditional` persistence tests).
- CLI: `tfe comp create ... --from-json` with a `Group`/`BlockElement` in the
  JSON succeeds and `--json` output round-trips it.
- `VisitBlockRefs` finds a `BlockRef` nested three levels deep (list → nested
  list → `BlockElement` → `BlockRef`).
