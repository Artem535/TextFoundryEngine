# Structural Fragments (`Group`, `BlockElement`) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add two new `Fragment` variants — `Group` (nested bulleted/numbered lists)
and `BlockElement` (heading/quote/fenced-code-block) — end to end: data model,
validation, `Renderer`, ObjectBox persistence, and the CLI's `--from-json` DTO
layer.

**Architecture:** Follows the exact precedent already set by `Conditional`
(`tf/fragment.h`/`tf/fragment.cc`, ObjectBox persistence in `tf/obx_utils.hpp`,
resolution in `tf/renderer.cc`, CLI DTO in `cli/dto.h`/`cli/dto.cc`). `BlockType`
is untouched — this is composition-level structure, not block-level
specialization (see the design spec's "Why two variants and not one" and the
PRD's "Type is Enum, not Class" principle).

**Tech Stack:** C++23, doctest (`tests/test_main.cc` for core engine,
`tests/cli_dto_test.cc` for the CLI DTO layer), ObjectBox + FlatBuffers schema
codegen via the `objectbox-generator` binary.

## Global Constraints

- No new rendering *formats* (no `OutputFormat`/HTML/XML) — output stays plain
  text using the existing markdown-flavored convention (`Separator::Hr` already
  renders `"\n---\n"`).
- No changes to `Engine::NormalizeFragments`/`FragmentTreeToPreviewText`,
  `CompositionBlockRewrite`, or tables — all explicitly out of scope per the
  design spec (`docs/superpowers/specs/2026-09-23-structural-fragments-design.md`).
- No changes to `Conditional`'s own behavior, only to code that must now also
  recognize `Group`/`BlockElement` alongside it.
- **Ordering matters and is not arbitrary.** When `Conditional` was originally
  added, the ObjectBox persistence layer was *not* updated in the same pass —
  `Engine::Render()` loads compositions from storage, not from the in-memory
  object, so every `Conditional` was silently replaced by an empty `StaticText`
  on load, and this wasn't caught until a later Renderer-testing task failed
  with empty output (commit `404bba7`'s own commit message documents this as
  "a gap in the implementation plan"). This plan avoids repeating it: ObjectBox
  persistence (Task 2) ships *before* any test exercises `Group`/`BlockElement`
  through a real `Engine::Render()` call (Task 3) — Task 2's own tests only
  call `LoadComposition` (never `Render`), so they can't accidentally pass for
  the wrong reason.
- Spec: `docs/superpowers/specs/2026-09-23-structural-fragments-design.md` —
  read it before starting if anything below is unclear.

---

## File Structure

- **Modify: `tf/error.h`** — two new `ErrorCode`s + factory methods.
- **Modify: `tf/fragment.h`** — `Group`, `GroupKind`, `GroupBuilder`,
  `BlockElement`, `BlockElementKind`; extend `Fragment`'s variant, factories,
  accessors, `FragmentType`.
- **Modify: `tf/fragment.cc`** — `Group::validate`, `BlockElement::validate`,
  `GroupBuilder` methods, extend `Fragment::validate` and `VisitBlockRefs`.
- **Modify: `tf/composition.h`/`tf/composition.cc`** —
  `CompositionDraftBuilder::AddGroup`/`AddBlockElement`.
- **Modify: `cli/output.cc`** — `ErrorCodeName` gains the two new codes.
- **Modify: `tf/database_scheme.fbs`** — two new `ObxFragment` string fields
  (`groupJson`, `blockElementJson`); regenerate `tf/objectbox-model.h`,
  `tf/objectbox-model.json`, `tf/database_scheme.obx.hpp`,
  `tf/database_scheme.obx.cpp` via the `objectbox-generator` binary.
- **Modify: `tf/obx_utils.hpp`** — `GroupToGeneric`/`GenericToGroup`,
  `BlockElementToGeneric`/`GenericToBlockElement` (+ JSON wrappers), wire into
  `FragmentToGeneric`/`GenericToFragment`,
  `ObxFragmentTypeToFragmentType`/`FragmentTypeToObxFragmentType`,
  `ObxFragmentToFragment`/`fragment_to_obx_fragment`.
- **Modify: `tf/renderer.h`/`tf/renderer.cc`** — extend `ResolveConditionals`
  to recurse into `Group`/`BlockElement`; add `RenderGroup`/`RenderBlockElement`;
  wire into `RenderFragment`.
- **Modify: `cli/dto.h`/`cli/dto.cc`** — `GroupFragmentDto`/
  `BlockElementFragmentDto` (+ item/nested structs), `ToGroup`/`ToBlockElement`,
  wire into `ToFragment`/`AppendFragmentToDraft`.
- **Modify: `tests/test_main.cc`** — new suites `Group`, `GroupBuilder`,
  `BlockElement`, `GroupPersistence`/`BlockElementPersistence`,
  `GroupRendering`/`BlockElementRendering`; extend the existing
  `VisitBlockRefs`-adjacent coverage.
- **Modify: `tests/cli_dto_test.cc`** — DTO round-trip tests for `Group`/
  `BlockElement`.

---

### Task 1: Data model — `Group`, `BlockElement`, validation, builders, `VisitBlockRefs`

**Files:**
- Modify: `tf/error.h`
- Modify: `tf/fragment.h`
- Modify: `tf/fragment.cc`
- Modify: `tf/composition.h`, `tf/composition.cc`
- Modify: `cli/output.cc`
- Test: `tests/test_main.cc`

**Interfaces:**
- Produces: `Group{kind, items: vector<vector<Fragment>>}`,
  `BlockElement{kind, attr, content: vector<Fragment>}`, both with
  `[[nodiscard]] Error validate(bool isDraftContext) const`.
  `Fragment::MakeGroup(Group)`, `Fragment::MakeBlockElement(BlockElement)`,
  `Fragment::MakeHeading(int, vector<Fragment>)`,
  `Fragment::MakeQuote(vector<Fragment>)`,
  `Fragment::MakeCodeBlock(string, vector<Fragment>)`. `GroupBuilder(GroupKind)`
  with `.Item(vector<Fragment>)` / `.Item(Fragment)` / `.build()`.
  `CompositionDraftBuilder::AddGroup(Group)`/`AddBlockElement(BlockElement)`.
  `FragmentType::Group`/`FragmentType::BlockElement`.
- Consumed by: Tasks 2–4 (all of them construct `Group`/`BlockElement` via
  these exact names).

- [ ] **Step 1: Add `EmptyGroup`/`InvalidHeadingLevel` error codes**

In `tf/error.h`, in the `ErrorCode` enum, right after `MissingElseBranch,`:

```cpp
  EmptyGroup,              ///< Group has zero items
  InvalidHeadingLevel,     ///< BlockElement is Heading and attr isn't "1".."6"
```

Right after the `MissingElseBranch()` factory method (before `success()`):

```cpp
  [[nodiscard]] static Error EmptyGroup() {
    return Error{ErrorCode::EmptyGroup,
                 "Group must have at least one item"};
  }

  [[nodiscard]] static Error InvalidHeadingLevel() {
    return Error{ErrorCode::InvalidHeadingLevel,
                 "Heading level must be an integer from 1 to 6"};
  }
```

- [ ] **Step 2: Write the failing data-model tests**

Add to `tests/test_main.cc`, right after the closing `}` of `TEST_SUITE("Conditional")`
(currently ends at line 740, right before `TEST_SUITE("ConditionalRendering")`):

```cpp
TEST_SUITE("Group") {
  TEST_CASE("empty items is an error") {
    Group group{.kind = GroupKind::Bulleted, .items = {}};
    auto err = group.validate(false);
    CHECK(err.is_error());
    CHECK(err.code == ErrorCode::EmptyGroup);
  }

  TEST_CASE("an item with zero fragments is valid") {
    Group group{.kind = GroupKind::Bulleted, .items = {{}}};
    CHECK(group.validate(false).is_success());
  }

  TEST_CASE("a valid group with multiple items passes validation") {
    Group group{
        .kind = GroupKind::Numbered,
        .items = {{Fragment::MakeStaticText("first")},
                  {Fragment::MakeStaticText("second")}}};
    CHECK(group.validate(false).is_success());
  }

  TEST_CASE("an invalid fragment nested in an item propagates its error") {
    Conditional badCond;
    badCond.elseContent = std::vector<Fragment>{};  // no branches -> EmptyConditional
    Group group{.kind = GroupKind::Bulleted,
               .items = {{Fragment::MakeConditional(std::move(badCond))}}};

    auto err = group.validate(false);
    CHECK(err.is_error());
    CHECK(err.code == ErrorCode::EmptyConditional);
  }

  TEST_CASE("Fragment::validate propagates a bad Group's error") {
    Group group{.kind = GroupKind::Bulleted, .items = {}};
    Fragment f = Fragment::MakeGroup(std::move(group));
    auto err = f.validate(false);
    CHECK(err.is_error());
    CHECK(err.code == ErrorCode::EmptyGroup);
  }

  TEST_CASE("Composition::validate propagates a nested Group's error") {
    Composition comp("test.group.invalid");
    Group group{.kind = GroupKind::Bulleted, .items = {}};
    comp.InsertFragment(0, Fragment::MakeGroup(std::move(group)));

    auto err = comp.validate();
    CHECK(err.is_error());
    CHECK(err.code == ErrorCode::EmptyGroup);
  }
}

TEST_SUITE("GroupBuilder") {
  TEST_CASE("Item(vector<Fragment>) and Item(Fragment) build the expected items") {
    auto group = GroupBuilder(GroupKind::Numbered)
                     .Item(Fragment::MakeStaticText("single"))
                     .Item(std::vector<Fragment>{Fragment::MakeStaticText("a"),
                                                 Fragment::MakeStaticText("b")})
                     .build();

    REQUIRE(group.kind == GroupKind::Numbered);
    REQUIRE(group.items.size() == 2);
    REQUIRE(group.items[0].size() == 1);
    CHECK(group.items[0][0].AsStaticText().text() == "single");
    REQUIRE(group.items[1].size() == 2);
    CHECK(group.items[1][0].AsStaticText().text() == "a");
    CHECK(group.items[1][1].AsStaticText().text() == "b");
  }
}

TEST_SUITE("BlockElement") {
  TEST_CASE("Heading accepts levels 1 through 6") {
    for (const std::string& level : {"1", "2", "3", "4", "5", "6"}) {
      BlockElement element{.kind = BlockElementKind::Heading,
                           .attr = level,
                           .content = {Fragment::MakeStaticText("text")}};
      CHECK(element.validate(false).is_success());
    }
  }

  TEST_CASE("Heading rejects out-of-range or malformed levels") {
    for (const std::string& level : {"0", "7", "abc", "", "1.5", " 1"}) {
      BlockElement element{.kind = BlockElementKind::Heading,
                           .attr = level,
                           .content = {}};
      auto err = element.validate(false);
      CHECK(err.is_error());
      CHECK(err.code == ErrorCode::InvalidHeadingLevel);
    }
  }

  TEST_CASE("Quote and CodeBlock accept any attr, including empty") {
    BlockElement quote{.kind = BlockElementKind::Quote, .attr = "", .content = {}};
    CHECK(quote.validate(false).is_success());

    BlockElement code{.kind = BlockElementKind::CodeBlock, .attr = "", .content = {}};
    CHECK(code.validate(false).is_success());

    BlockElement codeWithLang{
        .kind = BlockElementKind::CodeBlock, .attr = "cpp", .content = {}};
    CHECK(codeWithLang.validate(false).is_success());
  }

  TEST_CASE("an invalid fragment nested in content propagates its error") {
    Conditional badCond;
    badCond.elseContent = std::vector<Fragment>{};  // no branches -> EmptyConditional
    BlockElement element{.kind = BlockElementKind::Quote,
                         .attr = "",
                         .content = {Fragment::MakeConditional(std::move(badCond))}};

    auto err = element.validate(false);
    CHECK(err.is_error());
    CHECK(err.code == ErrorCode::EmptyConditional);
  }

  TEST_CASE("Fragment::validate propagates a bad BlockElement's error") {
    Fragment f = Fragment::MakeHeading(7, {});
    auto err = f.validate(false);
    CHECK(err.is_error());
    CHECK(err.code == ErrorCode::InvalidHeadingLevel);
  }

  TEST_CASE("MakeHeading/MakeQuote/MakeCodeBlock encode attr correctly") {
    Fragment heading = Fragment::MakeHeading(3, {Fragment::MakeStaticText("h")});
    REQUIRE(heading.IsBlockElement());
    CHECK(heading.AsBlockElement().kind == BlockElementKind::Heading);
    CHECK(heading.AsBlockElement().attr == "3");

    Fragment quote = Fragment::MakeQuote({Fragment::MakeStaticText("q")});
    CHECK(quote.AsBlockElement().kind == BlockElementKind::Quote);
    CHECK(quote.AsBlockElement().attr.empty());

    Fragment code = Fragment::MakeCodeBlock("cpp", {Fragment::MakeStaticText("c")});
    CHECK(code.AsBlockElement().kind == BlockElementKind::CodeBlock);
    CHECK(code.AsBlockElement().attr == "cpp");
  }
}

TEST_SUITE("StructuralFragmentBlockRefVisiting") {
  TEST_CASE("VisitBlockRefs finds a BlockRef nested inside a Group item") {
    Group group{.kind = GroupKind::Bulleted,
               .items = {{Fragment::MakeBlockRef(
                   BlockRef("role.expert", Version{1, 0}))}}};
    std::vector<Fragment> fragments = {Fragment::MakeGroup(std::move(group))};

    std::vector<BlockId> found;
    VisitBlockRefs(fragments,
                   [&](const BlockRef& ref) { found.push_back(ref.GetBlockId()); });
    REQUIRE(found.size() == 1);
    CHECK(found[0] == "role.expert");
  }

  TEST_CASE("VisitBlockRefs finds a BlockRef nested inside a BlockElement") {
    BlockElement element{
        .kind = BlockElementKind::Quote,
        .attr = "",
        .content = {Fragment::MakeBlockRef(BlockRef("role.expert", Version{1, 0}))}};
    std::vector<Fragment> fragments = {Fragment::MakeBlockElement(std::move(element))};

    std::vector<BlockId> found;
    VisitBlockRefs(fragments,
                   [&](const BlockRef& ref) { found.push_back(ref.GetBlockId()); });
    REQUIRE(found.size() == 1);
    CHECK(found[0] == "role.expert");
  }

  TEST_CASE(
      "VisitBlockRefs finds a BlockRef three levels deep: Group -> nested "
      "Group -> BlockElement -> BlockRef") {
    BlockElement innerElement{
        .kind = BlockElementKind::Quote,
        .attr = "",
        .content = {Fragment::MakeBlockRef(BlockRef("deep.block", Version{1, 0}))}};
    Group innerGroup{.kind = GroupKind::Bulleted,
                     .items = {{Fragment::MakeBlockElement(std::move(innerElement))}}};
    Group outerGroup{.kind = GroupKind::Bulleted,
                     .items = {{Fragment::MakeGroup(std::move(innerGroup))}}};
    std::vector<Fragment> fragments = {Fragment::MakeGroup(std::move(outerGroup))};

    std::vector<BlockId> found;
    VisitBlockRefs(fragments,
                   [&](const BlockRef& ref) { found.push_back(ref.GetBlockId()); });
    REQUIRE(found.size() == 1);
    CHECK(found[0] == "deep.block");
  }
}
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `cmake --build build-rel --target core_tests --parallel 2>&1 | tail -60`
Expected: compile errors — `Group`, `GroupKind`, `GroupBuilder`, `BlockElement`,
`BlockElementKind`, `ErrorCode::EmptyGroup`, `ErrorCode::InvalidHeadingLevel`,
`Fragment::MakeGroup`, etc. don't exist yet.

- [ ] **Step 4: Add `GroupKind`/`Group`/`GroupBuilder`/`BlockElementKind`/`BlockElement` to `tf/fragment.h`**

Right after `ConditionalBuilder`'s closing `};` (currently line 146), before the
`Fragment` class:

```cpp
enum class GroupKind { Bulleted, Numbered };

/**
 * Group - an ordered list of items, each item itself a fragment list (so an
 * item can contain a nested Group, StaticText, BlockRef, or Conditional).
 * Rendering (markers, indentation, line joining) is entirely a Renderer
 * concern -- Group only carries the semantic shape.
 */
struct Group {
  GroupKind kind = GroupKind::Bulleted;
  std::vector<std::vector<Fragment>> items;

  [[nodiscard]] Error validate(bool isDraftContext) const;
};

/**
 * Fluent builder for Group. Item() appends one item (a fragment list); the
 * single-Fragment overload is a convenience that wraps it in a one-element
 * vector. Unlike ConditionalBuilder there's no open/closed branch state --
 * items don't chain the way If()/Then() does.
 */
class GroupBuilder {
 public:
  explicit GroupBuilder(GroupKind kind);

  GroupBuilder& Item(std::vector<Fragment> content);
  GroupBuilder& Item(Fragment fragment);

  [[nodiscard]] Group build();

 private:
  Group group_;
};

enum class BlockElementKind { Heading, Quote, CodeBlock };

/**
 * BlockElement - a single structural wrapper around a fragment list. `attr`
 * is kind-specific: heading level as a decimal string ("1".."6") for
 * Heading, language for CodeBlock (may be empty -- "unspecified"), unused
 * (kept empty) for Quote.
 */
struct BlockElement {
  BlockElementKind kind;
  std::string attr;
  std::vector<Fragment> content;

  [[nodiscard]] Error validate(bool isDraftContext) const;
};
```

- [ ] **Step 5: Extend `FragmentType` and `Fragment`'s variant/constructors/factories/accessors in `tf/fragment.h`**

Change the enum (currently lines 25-30):

```cpp
enum class FragmentType {
  BlockRef,      ///< Reference to a Block
  StaticText,    ///< Raw text without parameters
  Separator,     ///< Typed separator (newline, paragraph, hr)
  Conditional,   ///< if/elif/else content selection
  Group,         ///< Ordered list of items (bulleted or numbered)
  BlockElement   ///< Heading, blockquote, or fenced code block
};
```

In `Fragment`, add constructors right after the `Conditional` one:

```cpp
  explicit Fragment(Group group) : data_(std::move(group)) {}

  explicit Fragment(BlockElement element) : data_(std::move(element)) {}
```

Add factories right after `MakeConditional`:

```cpp
  [[nodiscard]] static Fragment MakeGroup(Group group) {
    return Fragment(std::move(group));
  }

  [[nodiscard]] static Fragment MakeBlockElement(BlockElement element) {
    return Fragment(std::move(element));
  }

  [[nodiscard]] static Fragment MakeHeading(int level,
                                            std::vector<Fragment> content) {
    return Fragment(BlockElement{.kind = BlockElementKind::Heading,
                                 .attr = std::to_string(level),
                                 .content = std::move(content)});
  }

  [[nodiscard]] static Fragment MakeQuote(std::vector<Fragment> content) {
    return Fragment(BlockElement{.kind = BlockElementKind::Quote,
                                 .attr = "",
                                 .content = std::move(content)});
  }

  [[nodiscard]] static Fragment MakeCodeBlock(std::string language,
                                              std::vector<Fragment> content) {
    return Fragment(BlockElement{.kind = BlockElementKind::CodeBlock,
                                 .attr = std::move(language),
                                 .content = std::move(content)});
  }
```

In `type()`'s `std::visit`, add two more `if constexpr` branches right after the
`Conditional` one:

```cpp
          if constexpr (std::is_same_v<T, Group>)
            return FragmentType::Group;
          if constexpr (std::is_same_v<T, BlockElement>)
            return FragmentType::BlockElement;
```

Right after `IsConditional()`:

```cpp
  [[nodiscard]] bool IsGroup() const noexcept {
    return std::holds_alternative<Group>(data_);
  }

  [[nodiscard]] bool IsBlockElement() const noexcept {
    return std::holds_alternative<BlockElement>(data_);
  }
```

Right after the `AsConditional()` `&`/`const&` pair:

```cpp
  [[nodiscard]] Group& AsGroup() & { return std::get<Group>(data_); }

  [[nodiscard]] const Group& AsGroup() const& {
    return std::get<Group>(data_);
  }

  [[nodiscard]] BlockElement& AsBlockElement() & {
    return std::get<BlockElement>(data_);
  }

  [[nodiscard]] const BlockElement& AsBlockElement() const& {
    return std::get<BlockElement>(data_);
  }
```

Right after the `GetConditional()` pair:

```cpp
  [[nodiscard]] Group* GetGroup() noexcept { return std::get_if<Group>(&data_); }

  [[nodiscard]] const Group* GetGroup() const noexcept {
    return std::get_if<Group>(&data_);
  }

  [[nodiscard]] BlockElement* GetBlockElement() noexcept {
    return std::get_if<BlockElement>(&data_);
  }

  [[nodiscard]] const BlockElement* GetBlockElement() const noexcept {
    return std::get_if<BlockElement>(&data_);
  }
```

Finally, extend the variant itself (currently line 284):

```cpp
  std::variant<BlockRef, StaticText, Separator, Conditional, Group, BlockElement> data_;
```

- [ ] **Step 6: Implement `Group::validate`, `BlockElement::validate`, `GroupBuilder`, and extend `Fragment::validate`/`VisitBlockRefs` in `tf/fragment.cc`**

Add `#include <charconv>` to the top of `tf/fragment.cc` (needed for
`std::from_chars` in `BlockElement::validate`).

Right after `Conditional ConditionalBuilder::build() { return std::move(cond_); }`:

```cpp
// Group implementation
Error Group::validate(bool isDraftContext) const {
  if (items.empty()) {
    return Error::EmptyGroup();
  }
  for (const auto& item : items) {
    for (const auto& fragment : item) {
      auto err = fragment.validate(isDraftContext);
      if (err.is_error()) {
        return err;
      }
    }
  }
  return Error::success();
}

// GroupBuilder implementation
GroupBuilder::GroupBuilder(GroupKind kind) { group_.kind = kind; }

GroupBuilder& GroupBuilder::Item(std::vector<Fragment> content) {
  group_.items.push_back(std::move(content));
  return *this;
}

GroupBuilder& GroupBuilder::Item(Fragment fragment) {
  std::vector<Fragment> content;
  content.push_back(std::move(fragment));
  group_.items.push_back(std::move(content));
  return *this;
}

Group GroupBuilder::build() { return std::move(group_); }

// BlockElement implementation
Error BlockElement::validate(bool isDraftContext) const {
  if (kind == BlockElementKind::Heading) {
    int level = 0;
    const auto* begin = attr.data();
    const auto* end = begin + attr.size();
    const auto parsed = std::from_chars(begin, end, level);
    if (parsed.ec != std::errc{} || parsed.ptr != end || level < 1 || level > 6) {
      return Error::InvalidHeadingLevel();
    }
  }
  for (const auto& fragment : content) {
    auto err = fragment.validate(isDraftContext);
    if (err.is_error()) {
      return err;
    }
  }
  return Error::success();
}
```

In `Fragment::validate`, add two more `if constexpr` branches right after the
`Conditional` one:

```cpp
        if constexpr (std::is_same_v<T, Group>) {
          return val.validate(isDraftContext);
        }
        if constexpr (std::is_same_v<T, BlockElement>) {
          return val.validate(isDraftContext);
        }
```

In `VisitBlockRefs`, add right after the existing `if (fragment.IsConditional())`
block (inside the same `for` loop, still before the loop's closing `}`):

```cpp
    if (fragment.IsGroup()) {
      const Group& group = fragment.AsGroup();
      for (const auto& item : group.items) {
        VisitBlockRefs(item, visitor);
      }
    }
    if (fragment.IsBlockElement()) {
      VisitBlockRefs(fragment.AsBlockElement().content, visitor);
    }
```

- [ ] **Step 7: Add `CompositionDraftBuilder::AddGroup`/`AddBlockElement`**

In `tf/composition.h`, right after `CompositionDraftBuilder& AddConditional(Conditional cond);`:

```cpp
  CompositionDraftBuilder& AddGroup(Group group);

  CompositionDraftBuilder& AddBlockElement(BlockElement element);
```

In `tf/composition.cc`, right after `CompositionDraftBuilder::AddConditional`'s
implementation:

```cpp
CompositionDraftBuilder& CompositionDraftBuilder::AddGroup(Group group) {
  comp_.InsertFragment(comp_.fragmentCount(),
                       Fragment::MakeGroup(std::move(group)));
  return *this;
}

CompositionDraftBuilder& CompositionDraftBuilder::AddBlockElement(
    BlockElement element) {
  comp_.InsertFragment(comp_.fragmentCount(),
                       Fragment::MakeBlockElement(std::move(element)));
  return *this;
}
```

- [ ] **Step 8: Extend `cli/output.cc`'s `ErrorCodeName`**

Right after the `EmptyBranchConditions`/`MissingElseBranch` cases:

```cpp
    case tf::ErrorCode::EmptyGroup:
      return "EmptyGroup";
    case tf::ErrorCode::InvalidHeadingLevel:
      return "InvalidHeadingLevel";
```

- [ ] **Step 9: Build and run the new tests**

Run:
```bash
cmake --build build-rel --target core_tests --parallel && \
./build-rel/tests/core_tests --test-suite="Group,GroupBuilder,BlockElement,StructuralFragmentBlockRefVisiting"
```
Expected: PASS, all new test cases green.

Run: `ctest --test-dir build-rel --output-on-failure`
Expected: PASS across `core_tests`, `cli_tests`, `tfe_smoke` — nothing else was
touched that could regress, but this confirms the enum/variant extension
didn't break any existing `switch`-based code path (it can't, silently, since
none of those switches have a `default:` that would swallow the new cases
into wrong behavior — worth confirming directly rather than assuming).

- [ ] **Step 10: Commit**

```bash
git add tf/error.h tf/fragment.h tf/fragment.cc tf/composition.h tf/composition.cc cli/output.cc tests/test_main.cc
git commit -m "$(cat <<'EOF'
Add Group and BlockElement as new Fragment variants (data model)

Group (bulleted/numbered lists, arbitrarily nested) and BlockElement
(heading/quote/code block) join BlockRef/StaticText/Separator/Conditional
as Fragment variants, following the same pattern Conditional established:
a builder (GroupBuilder), validate(), CompositionDraftBuilder support,
and VisitBlockRefs recursion.

Renderer, ObjectBox persistence, and the CLI --from-json DTO layer don't
know about these two variants yet -- that's Tasks 2-4 of the
structural-fragments plan. Until then, a Group/BlockElement fragment can
be constructed and validated, but publishing one through a real Engine
would silently lose it on reload (same trap Conditional's own history
documents in commit 404bba7) and Render() would hit the "unreachable in
practice" fallback -- so nothing in this commit exercises either path.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: ObjectBox persistence

**Files:**
- Modify: `tf/database_scheme.fbs`
- Regenerate: `tf/objectbox-model.h`, `tf/objectbox-model.json`,
  `tf/database_scheme.obx.hpp`, `tf/database_scheme.obx.cpp`
- Modify: `tf/obx_utils.hpp`
- Test: `tests/test_main.cc`

**Interfaces:**
- Consumes: `Group`, `BlockElement`, `Fragment::MakeGroup`/`MakeBlockElement`,
  `CompositionDraftBuilder::AddGroup`/`AddBlockElement` (Task 1).
- Produces: `ObxFragment::groupJson`/`blockElementJson` string fields;
  `GroupToJson`/`JsonToGroup`, `BlockElementToJson`/`JsonToBlockElement` in
  `tf/obx_utils.hpp` (same shape as `ConditionalToJson`/`JsonToConditional`).

- [ ] **Step 1: Locate (or extract) the `objectbox-generator` binary**

This repository's generated ObjectBox files (`tf/objectbox-model.h`,
`tf/objectbox-model.json`, `tf/database_scheme.obx.hpp`,
`tf/database_scheme.obx.cpp`) are machine-generated from
`tf/database_scheme.fbs` — `objectbox-model.h` literally starts with
`// Code generated by ObjectBox; DO NOT EDIT.`. Do not hand-edit any of the
four generated files. A working `objectbox-generator` binary for this machine
was located and verified (regenerating the *unchanged* schema reproduces the
committed generated files byte-for-byte) at:

```
/home/a.durynin/Projects/C++/CppWiki/.claude/worktrees/objectbox-vector-sidecar-spike/build/objectbox-vector-sidecar-spike/ObjectBoxGenerator-download/5.0.0/install/objectbox-generator
```

If that path no longer exists, extract it from
`~/Downloads/objectbox-generator-Linux.zip` (also present) into a scratch
directory instead — `unzip -o ~/Downloads/objectbox-generator-Linux.zip -d /tmp/obxgen` and use `/tmp/obxgen/objectbox-generator`.

Before editing the schema, re-verify the binary you found still reproduces the
current committed files byte-for-byte (protects against a generator-version
mismatch silently reassigning IDs/UIDs on unrelated tables):

```bash
GEN=/home/a.durynin/Projects/C++/CppWiki/.claude/worktrees/objectbox-vector-sidecar-spike/build/objectbox-vector-sidecar-spike/ObjectBoxGenerator-download/5.0.0/install/objectbox-generator
rm -rf /tmp/obx_verify && mkdir /tmp/obx_verify
cp tf/database_scheme.fbs tf/objectbox-model.json /tmp/obx_verify/
(cd /tmp/obx_verify && "$GEN" -cpp database_scheme.fbs)
diff <(tail -n +2 /tmp/obx_verify/objectbox-model.h) <(tail -n +2 tf/objectbox-model.h)
diff /tmp/obx_verify/objectbox-model.json tf/objectbox-model.json
```
Expected: both `diff`s produce no output (the first line of `objectbox-model.h`
is a timestamp comment, hence `tail -n +2`). If either `diff` shows output,
STOP — do not proceed with a mismatched generator; report this instead of
guessing at hand-edits.

- [ ] **Step 2: Add `groupJson`/`blockElementJson` to the schema**

In `tf/database_scheme.fbs`, inside `table ObxFragment { ... }`, update the
`fragmentType` comment and add two fields right after `conditionalJson`:

```
    /// Fragment type: 0=BlockRef, 1=StaticText, 2=Separator, 3=Conditional, 4=Group, 5=BlockElement
    fragmentType: byte;
```
(replacing the existing comment line only — the field itself is unchanged)

```
    /// For Conditional: JSON-encoded branches/conditions/elseContent
    /// (including nested Fragments), see ConditionalToJson/JsonToConditional
    /// in obx_utils.hpp
    conditionalJson: string;
    /// For Group: JSON-encoded kind/items (including nested Fragments), see
    /// GroupToJson/JsonToGroup in obx_utils.hpp
    groupJson: string;
    /// For BlockElement: JSON-encoded kind/attr/content (including nested
    /// Fragments), see BlockElementToJson/JsonToBlockElement in obx_utils.hpp
    blockElementJson: string;
```

- [ ] **Step 3: Regenerate the ObjectBox bindings**

```bash
GEN=/home/a.durynin/Projects/C++/CppWiki/.claude/worktrees/objectbox-vector-sidecar-spike/build/objectbox-vector-sidecar-spike/ObjectBoxGenerator-download/5.0.0/install/objectbox-generator
"$GEN" -cpp tf/database_scheme.fbs
git diff --stat tf/objectbox-model.h tf/objectbox-model.json tf/database_scheme.obx.hpp tf/database_scheme.obx.cpp
```
Expected: all four files show a diff; `objectbox-model.h`/`.json` gain two new
property entries for `ObxFragment` with new auto-assigned IDs/UIDs (additive —
every existing property's ID/UID must be unchanged; if `git diff` shows an
existing property's ID or UID changing, STOP and report it rather than
committing). `database_scheme.obx.hpp` gains `groupJson`/`blockElementJson`
fields on the `ObxFragment` struct and matching `obx::Property<...>` entries
on `ObxFragment_`.

- [ ] **Step 4: Write the failing persistence tests**

Add to `tests/test_main.cc`, right after `TEST_SUITE("StructuralFragmentBlockRefVisiting")`
from Task 1 (or immediately after `TEST_SUITE("BlockElement")` if Task 1's
`VisitBlockRefs` suite hasn't landed yet — anywhere inside the file at the top
level works, doctest suites don't need to be contiguous):

```cpp
TEST_SUITE("StructuralFragmentPersistence") {
  TEST_CASE_FIXTURE(EngineTestFixture,
                    "a nested Group survives an ObjectBox store+load round trip") {
    CompositionDraftBuilder builder("group.roundtrip");
    Group inner{.kind = GroupKind::Numbered,
               .items = {{Fragment::MakeStaticText("inner one")},
                         {Fragment::MakeStaticText("inner two")}}};
    Group outer{.kind = GroupKind::Bulleted,
               .items = {{Fragment::MakeStaticText("outer text")},
                         {Fragment::MakeGroup(std::move(inner))}}};
    builder.AddGroup(std::move(outer));
    auto pubResult = engine.PublishComposition(builder.build(), Engine::VersionBump::Minor);
    REQUIRE(pubResult.HasValue());

    auto loaded = engine.LoadComposition("group.roundtrip");
    REQUIRE(loaded.HasValue());
    REQUIRE(loaded.value().fragmentCount() == 1);
    REQUIRE(loaded.value().fragment(0).IsGroup());

    const Group& loadedOuter = loaded.value().fragment(0).AsGroup();
    CHECK(loadedOuter.kind == GroupKind::Bulleted);
    REQUIRE(loadedOuter.items.size() == 2);
    REQUIRE(loadedOuter.items[0].size() == 1);
    CHECK(loadedOuter.items[0][0].AsStaticText().text() == "outer text");
    REQUIRE(loadedOuter.items[1].size() == 1);
    REQUIRE(loadedOuter.items[1][0].IsGroup());

    const Group& loadedInner = loadedOuter.items[1][0].AsGroup();
    CHECK(loadedInner.kind == GroupKind::Numbered);
    REQUIRE(loadedInner.items.size() == 2);
    CHECK(loadedInner.items[0][0].AsStaticText().text() == "inner one");
    CHECK(loadedInner.items[1][0].AsStaticText().text() == "inner two");
  }

  TEST_CASE_FIXTURE(EngineTestFixture,
                    "each BlockElementKind survives an ObjectBox store+load round trip") {
    CompositionDraftBuilder builder("block_element.roundtrip");
    builder.AddBlockElement(
        BlockElement{.kind = BlockElementKind::Heading,
                    .attr = "2",
                    .content = {Fragment::MakeStaticText("a heading")}});
    builder.AddBlockElement(
        BlockElement{.kind = BlockElementKind::Quote,
                    .attr = "",
                    .content = {Fragment::MakeStaticText("a quote")}});
    builder.AddBlockElement(
        BlockElement{.kind = BlockElementKind::CodeBlock,
                    .attr = "cpp",
                    .content = {Fragment::MakeStaticText("int x = 1;")}});
    auto pubResult = engine.PublishComposition(builder.build(), Engine::VersionBump::Minor);
    REQUIRE(pubResult.HasValue());

    auto loaded = engine.LoadComposition("block_element.roundtrip");
    REQUIRE(loaded.HasValue());
    REQUIRE(loaded.value().fragmentCount() == 3);

    REQUIRE(loaded.value().fragment(0).IsBlockElement());
    const BlockElement& heading = loaded.value().fragment(0).AsBlockElement();
    CHECK(heading.kind == BlockElementKind::Heading);
    CHECK(heading.attr == "2");
    REQUIRE(heading.content.size() == 1);
    CHECK(heading.content[0].AsStaticText().text() == "a heading");

    REQUIRE(loaded.value().fragment(1).IsBlockElement());
    const BlockElement& quote = loaded.value().fragment(1).AsBlockElement();
    CHECK(quote.kind == BlockElementKind::Quote);
    CHECK(quote.content[0].AsStaticText().text() == "a quote");

    REQUIRE(loaded.value().fragment(2).IsBlockElement());
    const BlockElement& code = loaded.value().fragment(2).AsBlockElement();
    CHECK(code.kind == BlockElementKind::CodeBlock);
    CHECK(code.attr == "cpp");
    CHECK(code.content[0].AsStaticText().text() == "int x = 1;");
  }
}
```

- [ ] **Step 5: Run the tests to verify they fail**

Run:
```bash
cmake --build build-rel --target core_tests --parallel && \
./build-rel/tests/core_tests --test-suite="StructuralFragmentPersistence"
```
Expected: FAIL or crash — `ObxFragmentTypeToFragmentType`/`fragment_to_obx_fragment`
don't know about `Group`/`BlockElement` yet, so a published `Group`/`BlockElement`
either falls through to the `StaticText` default on load (losing all structure)
or, depending on how `fragment_to_obx_fragment`'s switch behaves for an
unhandled case (no `default:`, so `obxFrag.fragmentType` is still set correctly
by the line before the switch, but none of the type-specific fields get
populated), reloads as the *correct* `FragmentType::Group`/`BlockElement` but
with empty `items`/`content` — either way, the `REQUIRE`/`CHECK` assertions on
the reloaded structure's content fail.

- [ ] **Step 6: Extend `ObxFragmentTypeToFragmentType`/`FragmentTypeToObxFragmentType` in `tf/obx_utils.hpp`**

Update the doc comment and add two more cases to each (currently lines 155-189):

```cpp
/**
 * Convert ObxFragment fragment type (int8_t) to FragmentType enum
 * 0=BlockRef, 1=StaticText, 2=Separator, 3=Conditional, 4=Group, 5=BlockElement
 */
inline FragmentType ObxFragmentTypeToFragmentType(int8_t type) {
  switch (type) {
    case 0:
      return FragmentType::BlockRef;
    case 1:
      return FragmentType::StaticText;
    case 2:
      return FragmentType::Separator;
    case 3:
      return FragmentType::Conditional;
    case 4:
      return FragmentType::Group;
    case 5:
      return FragmentType::BlockElement;
    default:
      return FragmentType::StaticText;
  }
}

/**
 * Convert FragmentType enum to ObxFragment fragment type (int8_t)
 */
inline int8_t FragmentTypeToObxFragmentType(FragmentType type) {
  switch (type) {
    case FragmentType::BlockRef:
      return 0;
    case FragmentType::StaticText:
      return 1;
    case FragmentType::Separator:
      return 2;
    case FragmentType::Conditional:
      return 3;
    case FragmentType::Group:
      return 4;
    case FragmentType::BlockElement:
      return 5;
  }
  return 1;  // StaticText as default
}
```

- [ ] **Step 7: Add `GroupToGeneric`/`GenericToGroup`, `BlockElementToGeneric`/`GenericToBlockElement`, and their JSON wrappers**

Right after `GenericToConditional` (currently ends at line 452, right before
`inline rfl::Generic FragmentToGeneric(const Fragment& fragment) {`):

```cpp
inline rfl::Generic GroupToGeneric(const Group& group) {
  rfl::Generic::Object obj;
  obj["kind"] = std::string(group.kind == GroupKind::Numbered ? "numbered"
                                                              : "bulleted");
  rfl::Generic::Array items;
  for (const auto& item : group.items) {
    rfl::Generic::Array itemFragments;
    for (const auto& fragment : item) {
      itemFragments.emplace_back(FragmentToGeneric(fragment));
    }
    items.emplace_back(itemFragments);
  }
  obj["items"] = items;
  return obj;
}

inline Group GenericToGroup(const rfl::Generic& generic) {
  Group group;
  auto obj = generic.to_object().value();
  const auto kindStr = obj.get("kind").value().to_string().value();
  group.kind = (kindStr == "numbered") ? GroupKind::Numbered : GroupKind::Bulleted;
  // Materialize before looping: see the comment in GenericToCondition for
  // why this isn't optional (a real GCC-13-only UB bug was already hit once
  // in this exact file for the analogous Conditional/Branch path).
  const auto items = obj.get("items").value().to_array().value();
  for (const auto& itemGeneric : items) {
    const auto itemFragments = itemGeneric.to_array().value();
    std::vector<Fragment> item;
    for (const auto& f : itemFragments) {
      item.push_back(GenericToFragment(f));
    }
    group.items.push_back(std::move(item));
  }
  return group;
}

inline rfl::Generic BlockElementToGeneric(const BlockElement& element) {
  rfl::Generic::Object obj;
  std::string kindStr;
  switch (element.kind) {
    case BlockElementKind::Heading:
      kindStr = "heading";
      break;
    case BlockElementKind::Quote:
      kindStr = "quote";
      break;
    case BlockElementKind::CodeBlock:
      kindStr = "code_block";
      break;
  }
  obj["kind"] = kindStr;
  obj["attr"] = element.attr;
  rfl::Generic::Array content;
  for (const auto& fragment : element.content) {
    content.emplace_back(FragmentToGeneric(fragment));
  }
  obj["content"] = content;
  return obj;
}

inline BlockElement GenericToBlockElement(const rfl::Generic& generic) {
  BlockElement element;
  auto obj = generic.to_object().value();
  const auto kindStr = obj.get("kind").value().to_string().value();
  if (kindStr == "heading") {
    element.kind = BlockElementKind::Heading;
  } else if (kindStr == "code_block") {
    element.kind = BlockElementKind::CodeBlock;
  } else {
    element.kind = BlockElementKind::Quote;
  }
  element.attr = obj.get("attr").value().to_string().value();
  // Materialize before looping -- see GenericToGroup above.
  const auto content = obj.get("content").value().to_array().value();
  for (const auto& f : content) {
    element.content.push_back(GenericToFragment(f));
  }
  return element;
}

inline std::string GroupToJson(const Group& group) {
  return rfl::json::write(GroupToGeneric(group));
}

inline Group JsonToGroup(const std::string& json) {
  return GenericToGroup(rfl::json::read<rfl::Generic>(json).value());
}

inline std::string BlockElementToJson(const BlockElement& element) {
  return rfl::json::write(BlockElementToGeneric(element));
}

inline BlockElement JsonToBlockElement(const std::string& json) {
  return GenericToBlockElement(rfl::json::read<rfl::Generic>(json).value());
}
```

- [ ] **Step 8: Wire `Group`/`BlockElement` into `FragmentToGeneric`/`GenericToFragment`**

In `FragmentToGeneric`'s switch (currently lines 456-488), add two cases right
after `FragmentType::Conditional`:

```cpp
    case FragmentType::Group: {
      obj["type"] = std::string("group");
      obj["group"] = GroupToGeneric(fragment.AsGroup());
      break;
    }
    case FragmentType::BlockElement: {
      obj["type"] = std::string("block_element");
      obj["blockElement"] = BlockElementToGeneric(fragment.AsBlockElement());
      break;
    }
```

In `GenericToFragment` (currently lines 492-531), add right after the
`if (type == "conditional")` block:

```cpp
  if (type == "group") {
    return Fragment::MakeGroup(GenericToGroup(obj.get("group").value()));
  }

  if (type == "block_element") {
    return Fragment::MakeBlockElement(
        GenericToBlockElement(obj.get("blockElement").value()));
  }
```

- [ ] **Step 9: Wire `groupJson`/`blockElementJson` into `ObxFragmentToFragment`/`fragment_to_obx_fragment`**

In `ObxFragmentToFragment`'s switch (currently lines 551-581), add right after
the `FragmentType::Conditional` case:

```cpp
    case FragmentType::Group: {
      return Fragment::MakeGroup(JsonToGroup(obxFrag.groupJson));
    }

    case FragmentType::BlockElement: {
      return Fragment::MakeBlockElement(JsonToBlockElement(obxFrag.blockElementJson));
    }
```

In `fragment_to_obx_fragment`'s switch (currently lines 599-631), add right
after the `FragmentType::Conditional` case:

```cpp
    case FragmentType::Group: {
      obxFrag.groupJson = GroupToJson(fragment.AsGroup());
      break;
    }

    case FragmentType::BlockElement: {
      obxFrag.blockElementJson = BlockElementToJson(fragment.AsBlockElement());
      break;
    }
```

- [ ] **Step 10: Build and run the tests**

Run:
```bash
cmake --build build-rel --target core_tests --parallel && \
./build-rel/tests/core_tests --test-suite="StructuralFragmentPersistence"
```
Expected: PASS.

Run: `ctest --test-dir build-rel --output-on-failure`
Expected: PASS across `core_tests`, `cli_tests`, `tfe_smoke`.

- [ ] **Step 11: Commit**

```bash
git add tf/database_scheme.fbs tf/objectbox-model.h tf/objectbox-model.json tf/database_scheme.obx.hpp tf/database_scheme.obx.cpp tf/obx_utils.hpp tests/test_main.cc
git commit -m "$(cat <<'EOF'
Add ObjectBox persistence for Group and BlockElement fragments

Adds groupJson/blockElementJson fields to the ObxFragment schema
(regenerated via objectbox-generator, additive -- existing property
IDs/UIDs unchanged), and hand-rolled GroupToJson/JsonToGroup,
BlockElementToJson/JsonToBlockElement encoders in obx_utils.hpp built on
rfl::Generic, following the exact pattern ConditionalToJson/
JsonToConditional already established (Fragment's nested tree can't be
reflected directly by reflect-cpp).

Done as its own task, before any Renderer test exercises these fragment
types through a real Engine::Render() call (Task 3) -- Conditional's own
history (commit 404bba7) shows what happens when persistence lags behind
the data model: a fragment type silently reverts to empty StaticText on
reload, and the bug isn't caught until a later Renderer test fails with
empty output for reasons that look unrelated to storage.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: `Renderer` — resolve nested `Conditional`, render `Group`/`BlockElement` to text

**Files:**
- Modify: `tf/renderer.h`
- Modify: `tf/renderer.cc`
- Test: `tests/test_main.cc`

**Interfaces:**
- Consumes: `Group`, `BlockElement`, ObjectBox persistence (Tasks 1-2) — tests
  in this task publish through a real `Engine` and call `Render()`, which now
  correctly round-trips these fragment types.
- Produces: `Renderer::RenderGroup(const Group&, const RenderContext&, vector<pair<BlockId,Version>>&) const -> Result<std::string>`,
  `Renderer::RenderBlockElement(const BlockElement&, const RenderContext&, vector<pair<BlockId,Version>>&) const -> Result<std::string>`.

- [ ] **Step 1: Write the failing rendering tests**

Add to `tests/test_main.cc`, right after `TEST_SUITE("ConditionalRendering")`'s
closing `}` (currently line 905, right before `TEST_SUITE("ConditionalBuilder")`):

```cpp
TEST_SUITE("GroupRendering") {
  TEST_CASE_FIXTURE(EngineTestFixture, "a flat bulleted list renders with '- ' markers") {
    CompositionDraftBuilder builder("group.flat_bulleted");
    Group group{.kind = GroupKind::Bulleted,
               .items = {{Fragment::MakeStaticText("first")},
                         {Fragment::MakeStaticText("second")},
                         {Fragment::MakeStaticText("third")}}};
    builder.AddGroup(std::move(group));
    auto pubResult = engine.PublishComposition(builder.build(), Engine::VersionBump::Minor);
    REQUIRE(pubResult.HasValue());

    auto result = engine.Render("group.flat_bulleted");
    REQUIRE(result.HasValue());
    CHECK(result.value().text == "- first\n- second\n- third");
  }

  TEST_CASE_FIXTURE(EngineTestFixture, "a flat numbered list renders with '1. ', '2. ', ... markers") {
    CompositionDraftBuilder builder("group.flat_numbered");
    Group group{.kind = GroupKind::Numbered,
               .items = {{Fragment::MakeStaticText("first")},
                         {Fragment::MakeStaticText("second")}}};
    builder.AddGroup(std::move(group));
    auto pubResult = engine.PublishComposition(builder.build(), Engine::VersionBump::Minor);
    REQUIRE(pubResult.HasValue());

    auto result = engine.Render("group.flat_numbered");
    REQUIRE(result.HasValue());
    CHECK(result.value().text == "1. first\n2. second");
  }

  TEST_CASE_FIXTURE(EngineTestFixture,
                    "a nested list renders indented under its parent item, numbering restarting at 1") {
    CompositionDraftBuilder builder("group.nested");
    Group inner{.kind = GroupKind::Numbered,
               .items = {{Fragment::MakeStaticText("salt")},
                         {Fragment::MakeStaticText("pepper")}}};
    Group outer{.kind = GroupKind::Bulleted,
               .items = {{Fragment::MakeStaticText("tomatoes")},
                         {Fragment::MakeStaticText("spices:"),
                          Fragment::MakeGroup(std::move(inner))},
                         {Fragment::MakeStaticText("onion")}}};
    builder.AddGroup(std::move(outer));
    auto pubResult = engine.PublishComposition(builder.build(), Engine::VersionBump::Minor);
    REQUIRE(pubResult.HasValue());

    auto result = engine.Render("group.nested");
    REQUIRE(result.HasValue());
    CHECK(result.value().text ==
          "- tomatoes\n"
          "- spices:\n"
          "  1. salt\n"
          "  2. pepper\n"
          "- onion");
  }

  TEST_CASE_FIXTURE(EngineTestFixture, "a Group item containing a BlockRef renders the block's expanded template") {
    createAndPublishBlock("group.item_block", "an ingredient");
    CompositionDraftBuilder builder("group.with_block_ref");
    Group group{.kind = GroupKind::Bulleted,
               .items = {{Fragment::MakeBlockRef(
                   BlockRef("group.item_block", Version{1, 0}))}}};
    builder.AddGroup(std::move(group));
    auto pubResult = engine.PublishComposition(builder.build(), Engine::VersionBump::Minor);
    REQUIRE(pubResult.HasValue());

    auto result = engine.Render("group.with_block_ref");
    REQUIRE(result.HasValue());
    CHECK(result.value().text == "- an ingredient");
  }

  TEST_CASE_FIXTURE(EngineTestFixture,
                    "a Conditional nested inside a Group item is resolved at render time") {
    CompositionDraftBuilder builder("group.with_conditional");
    Conditional cond;
    cond.branches.push_back(Branch{
        .conditions = {Condition{.attribute = "level", .allowedValues = {"expert"}}},
        .content = {Fragment::MakeStaticText("expert item")}});
    cond.elseContent = std::vector<Fragment>{Fragment::MakeStaticText("default item")};
    Group group{.kind = GroupKind::Bulleted,
               .items = {{Fragment::MakeConditional(std::move(cond))}}};
    builder.AddGroup(std::move(group));
    auto pubResult = engine.PublishComposition(builder.build(), Engine::VersionBump::Minor);
    REQUIRE(pubResult.HasValue());

    auto expertResult =
        engine.Render("group.with_conditional", RenderContext{}.WithParam("level", "expert"));
    REQUIRE(expertResult.HasValue());
    CHECK(expertResult.value().text == "- expert item");

    auto defaultResult = engine.Render("group.with_conditional");
    REQUIRE(defaultResult.HasValue());
    CHECK(defaultResult.value().text == "- default item");
  }

  TEST_CASE_FIXTURE(EngineTestFixture,
                    "a Group nested inside a Conditional branch still resolves correctly (regression)") {
    CompositionDraftBuilder builder("group.inside_conditional");
    Group group{.kind = GroupKind::Bulleted,
               .items = {{Fragment::MakeStaticText("a")}, {Fragment::MakeStaticText("b")}}};
    Conditional cond;
    cond.branches.push_back(Branch{
        .conditions = {Condition{.attribute = "show", .allowedValues = {"list"}}},
        .content = {Fragment::MakeGroup(std::move(group))}});
    cond.elseContent = std::vector<Fragment>{Fragment::MakeStaticText("no list")};
    builder.AddConditional(std::move(cond));
    auto pubResult = engine.PublishComposition(builder.build(), Engine::VersionBump::Minor);
    REQUIRE(pubResult.HasValue());

    auto result =
        engine.Render("group.inside_conditional", RenderContext{}.WithParam("show", "list"));
    REQUIRE(result.HasValue());
    CHECK(result.value().text == "- a\n- b");
  }
}

TEST_SUITE("BlockElementRendering") {
  TEST_CASE_FIXTURE(EngineTestFixture, "Heading level 2 renders with two '#' characters") {
    CompositionDraftBuilder builder("block_element.heading");
    builder.AddBlockElement(Fragment::MakeHeading(2, {Fragment::MakeStaticText("Title")})
                                .AsBlockElement());
    auto pubResult = engine.PublishComposition(builder.build(), Engine::VersionBump::Minor);
    REQUIRE(pubResult.HasValue());

    auto result = engine.Render("block_element.heading");
    REQUIRE(result.HasValue());
    CHECK(result.value().text == "## Title");
  }

  TEST_CASE_FIXTURE(EngineTestFixture, "Quote prefixes every line with '> '") {
    CompositionDraftBuilder builder("block_element.quote");
    builder.AddBlockElement(
        Fragment::MakeQuote({Fragment::MakeStaticText("line one")}).AsBlockElement());
    auto pubResult = engine.PublishComposition(builder.build(), Engine::VersionBump::Minor);
    REQUIRE(pubResult.HasValue());

    auto result = engine.Render("block_element.quote");
    REQUIRE(result.HasValue());
    CHECK(result.value().text == "> line one");
  }

  TEST_CASE_FIXTURE(EngineTestFixture, "CodeBlock wraps content in a language-tagged fence") {
    CompositionDraftBuilder builder("block_element.code");
    builder.AddBlockElement(
        Fragment::MakeCodeBlock("cpp", {Fragment::MakeStaticText("int x = 1;")})
            .AsBlockElement());
    auto pubResult = engine.PublishComposition(builder.build(), Engine::VersionBump::Minor);
    REQUIRE(pubResult.HasValue());

    auto result = engine.Render("block_element.code");
    REQUIRE(result.HasValue());
    CHECK(result.value().text == "```cpp\nint x = 1;\n```");
  }
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run:
```bash
cmake --build build-rel --target core_tests --parallel && \
./build-rel/tests/core_tests --test-suite="GroupRendering,BlockElementRendering"
```
Expected: FAIL. Most cases fail with `result.HasError()` — `RenderFragment`'s
switch has no `Group`/`BlockElement` cases, so those fragments fall through to
`Result<std::string>(Error{ErrorCode::InvalidParamType, "Unknown fragment type"})`.
The "Conditional nested inside a Group item" and "Group nested inside a
Conditional branch" cases may fail differently or even hit the
`ResolveConditionals` `Group`-passthrough gap — that's expected, this step is
about confirming red, not diagnosing every failure mode individually.

- [ ] **Step 3: Extend `ResolveConditionals` to recurse into (and reconstruct) `Group`/`BlockElement`**

In `tf/renderer.cc`, inside `ResolveConditionals`'s loop (currently lines
205-246), add two more branches right after
`if (!fragment.IsConditional()) { resolved.push_back(fragment); continue; }`
— i.e. replace that single passthrough line with:

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

    if (!fragment.IsConditional()) {
      resolved.push_back(fragment);
      continue;
    }
```

(The rest of the function — the existing `Conditional` handling — is
unchanged.)

- [ ] **Step 4: Declare `RenderGroup`/`RenderBlockElement` in `tf/renderer.h`**

Find `Renderer::ExpandBlockRef`'s declaration and add two more private methods
right after it:

```cpp
  [[nodiscard]] Result<std::string> RenderGroup(
      const Group& group, const RenderContext& context,
      std::vector<std::pair<BlockId, Version>>& blocksUsed) const;

  // Helper for RenderGroup: renders a Group into its individual, fully
  // marked and indented lines (never containing an embedded '\n' within one
  // element) rather than one joined string -- see RenderGroup's own comment
  // for why this two-function split exists.
  [[nodiscard]] Result<std::vector<std::string>> RenderGroupLines(
      const Group& group, const RenderContext& context,
      std::vector<std::pair<BlockId, Version>>& blocksUsed) const;

  [[nodiscard]] Result<std::string> RenderBlockElement(
      const BlockElement& element, const RenderContext& context,
      std::vector<std::pair<BlockId, Version>>& blocksUsed) const;
```

- [ ] **Step 5: Implement `RenderGroup`/`RenderBlockElement` and wire them into `RenderFragment`**

In `tf/renderer.cc`, add both new methods right after `ExpandBlockRef`
(currently ends at line 191, right before `StructuralStyle Renderer::GetEffectiveStyle`):

**Why two functions, not one:** the natural-looking single-function version
(render each item to one string with `depth * 2` spaces of leading indent
baked in, recursing into a nested `Group` at `depth + 1`, then joining) has a
real bug: a nested `Group`'s own lines already come back pre-indented for
*their* depth, so splicing them into the parent item's line list and then
*also* prefixing every non-first line with the parent's marker-width padding
double-indents them. The clean fix is for `RenderGroupLines` to never bake in
any indent for its own depth at all -- it always renders as if it were
top-level (`"1. salt"`, not `"  1. salt"`) -- and exactly one place adds
exactly one level of indent (`marker.size()` spaces) to every line of a
finished `Group`'s output *except* the first: the same uniform per-line rule
already applied to a single item's own wrapped multi-line text. That rule
applied once, at exactly the point where a rendered value (whether an
ordinary fragment's multi-line text or a whole nested list) is folded into
`raw`, is what gives correct, single-level indentation with no special-casing
needed for "was this line's origin a nested list or not".

```cpp
Result<std::vector<std::string>> Renderer::RenderGroupLines(
    const Group& group, const RenderContext& context,
    std::vector<std::pair<BlockId, Version>>& blocksUsed) const {
  std::vector<std::string> lines;

  for (size_t itemIndex = 0; itemIndex < group.items.size(); ++itemIndex) {
    const std::string marker = (group.kind == GroupKind::Numbered)
                                   ? (std::to_string(itemIndex + 1) + ". ")
                                   : std::string("- ");
    const std::string padding(marker.size(), ' ');

    // This item's own contributed lines, unindented and without the marker
    // -- built left to right. Consecutive plain fragments concatenate onto
    // the same running line (direct concatenation, per the design spec); a
    // nested Group always ends the current line and contributes its own
    // lines (also unindented at this point -- see the two-function split
    // above) as additional entries.
    std::vector<std::string> raw;
    std::ostringstream currentLine;
    bool haveOpenLine = false;

    for (const auto& fragment : group.items[itemIndex]) {
      if (fragment.IsGroup()) {
        auto nestedLines = RenderGroupLines(fragment.AsGroup(), context, blocksUsed);
        if (nestedLines.HasError()) {
          return nestedLines;
        }
        if (haveOpenLine) {
          raw.push_back(currentLine.str());
          currentLine.str("");
          haveOpenLine = false;
        }
        for (const auto& nestedLine : nestedLines.value()) {
          raw.push_back(nestedLine);
        }
        continue;
      }

      auto rendered = RenderFragment(fragment, context, blocksUsed);
      if (rendered.HasError()) {
        return Result<std::vector<std::string>>(rendered.error());
      }
      // A rendered fragment's own text might already contain '\n' (e.g. a
      // multi-line StaticText) -- split so each becomes its own raw line
      // rather than corrupting later marker alignment.
      std::istringstream renderedStream(rendered.value());
      std::string piece;
      bool firstPiece = true;
      while (std::getline(renderedStream, piece)) {
        if (!firstPiece) {
          raw.push_back(currentLine.str());
          currentLine.str("");
        }
        currentLine << piece;
        haveOpenLine = true;
        firstPiece = false;
      }
    }
    if (haveOpenLine) {
      raw.push_back(currentLine.str());
    }
    if (raw.empty()) {
      raw.push_back("");  // an item with zero fragments still gets a marker-only line
    }

    for (size_t i = 0; i < raw.size(); ++i) {
      lines.push_back((i == 0 ? marker : padding) + raw[i]);
    }
  }

  return Result<std::vector<std::string>>(std::move(lines));
}

Result<std::string> Renderer::RenderGroup(
    const Group& group, const RenderContext& context,
    std::vector<std::pair<BlockId, Version>>& blocksUsed) const {
  auto lines = RenderGroupLines(group, context, blocksUsed);
  if (lines.HasError()) {
    return Result<std::string>(lines.error());
  }
  std::ostringstream result;
  for (size_t i = 0; i < lines.value().size(); ++i) {
    if (i > 0) {
      result << "\n";
    }
    result << lines.value()[i];
  }
  return Result<std::string>(result.str());
}

Result<std::string> Renderer::RenderBlockElement(
    const BlockElement& element, const RenderContext& context,
    std::vector<std::pair<BlockId, Version>>& blocksUsed) const {
  std::ostringstream innerStream;
  for (const auto& fragment : element.content) {
    auto rendered = RenderFragment(fragment, context, blocksUsed);
    if (rendered.HasError()) {
      return rendered;
    }
    innerStream << rendered.value();
  }
  const std::string inner = innerStream.str();

  switch (element.kind) {
    case BlockElementKind::Heading: {
      int level = 1;
      const auto* begin = element.attr.data();
      const auto* end = begin + element.attr.size();
      auto parsed = std::from_chars(begin, end, level);
      if (parsed.ec != std::errc{} || parsed.ptr != end || level < 1 || level > 6) {
        // validate() guarantees this for any published Composition; Render()
        // loads from storage rather than validating again, so this is a
        // defensive fallback, not a redundant check (same posture as
        // ResolveConditionals's elseContent check below it).
        level = 1;
      }
      return Result<std::string>(std::string(level, '#') + " " + inner);
    }
    case BlockElementKind::Quote: {
      std::istringstream innerLines(inner);
      std::ostringstream quoted;
      std::string line;
      bool first = true;
      while (std::getline(innerLines, line)) {
        if (!first) {
          quoted << "\n";
        }
        first = false;
        quoted << "> " << line;
      }
      return Result<std::string>(quoted.str());
    }
    case BlockElementKind::CodeBlock: {
      return Result<std::string>("```" + element.attr + "\n" + inner + "\n```");
    }
  }
  return Result<std::string>(
      Error{ErrorCode::InvalidParamType, "Unknown BlockElementKind"});
}
```

`RenderBlockElement` uses `std::from_chars`, so add `#include <charconv>` to
the top of `tf/renderer.cc`.

Wire both into `RenderFragment`'s switch (currently lines 131-153), right
after the `FragmentType::Conditional` case:

```cpp
    case FragmentType::Group:
      return RenderGroup(fragment.AsGroup(), context, blocksUsed);

    case FragmentType::BlockElement:
      return RenderBlockElement(fragment.AsBlockElement(), context, blocksUsed);
```

- [ ] **Step 6: Build and run the tests**

Run:
```bash
cmake --build build-rel --target core_tests --parallel && \
./build-rel/tests/core_tests --test-suite="GroupRendering,BlockElementRendering"
```
Expected: PASS.

Run: `ctest --test-dir build-rel --output-on-failure`
Expected: PASS across `core_tests`, `cli_tests`, `tfe_smoke`.

- [ ] **Step 7: Commit**

```bash
git add tf/renderer.h tf/renderer.cc tests/test_main.cc
git commit -m "$(cat <<'EOF'
Render Group and BlockElement fragments; resolve them in ResolveConditionals

ResolveConditionals now recurses into (and reconstructs) Group and
BlockElement, so a Conditional nested inside a Group item -- and a Group
nested inside a Conditional branch, which already worked -- both resolve
correctly before RenderFragment ever sees them.

Renderer::RenderGroup renders a (possibly nested) Group as an indented
bulleted or numbered list, joined by newlines, with numbering restarting
at 1 in every nested Group. Renderer::RenderBlockElement renders Heading
("## text"), Quote ("> text" per line), and CodeBlock ("```lang\ntext\n```")
-- the same markdown-flavored plain-text convention Separator::Hr already
uses ("\n---\n"), not a new OutputFormat mechanism.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: CLI `--from-json` DTO layer

**Files:**
- Modify: `cli/dto.h`
- Modify: `cli/dto.cc`
- Test: `tests/cli_dto_test.cc`

**Interfaces:**
- Consumes: `Group`, `BlockElement`, `CompositionDraftBuilder::AddGroup`/
  `AddBlockElement` (Task 1).
- Produces: `cli::GroupFragmentDto`, `cli::GroupItemDto`,
  `cli::BlockElementFragmentDto` (new, reflectable via `rfl`, same as the
  existing `ConditionalFragmentDto`/`BranchDto`); `cli::ToGroup`,
  `cli::ToBlockElement` (internal, mirroring `cli::ToConditional`).

- [ ] **Step 1: Write the failing DTO tests**

Add to `tests/cli_dto_test.cc`, right after the closing `}` of the anonymous
namespace (currently line 38, right before `TEST_CASE("conditional composition DTO round-trips...")`):

First, extend the anonymous namespace (before its closing `}`) with builder
helpers mirroring `MakeConditionalDto`:

```cpp
cli::CompositionDto MakeGroupDto() {
  cli::FragmentDto first;
  first.kind = "static_text";
  first.static_text = cli::StaticTextFragmentDto{"first"};

  cli::FragmentDto second;
  second.kind = "static_text";
  second.static_text = cli::StaticTextFragmentDto{"second"};

  cli::GroupItemDto item1;
  item1.content.push_back(first);
  cli::GroupItemDto item2;
  item2.content.push_back(second);

  cli::FragmentDto group;
  group.kind = "group";
  group.group = cli::GroupFragmentDto{"bulleted", {item1, item2}};

  cli::CompositionDto composition;
  composition.id = "shopping_list";
  composition.fragments.push_back(std::move(group));
  return composition;
}

cli::CompositionDto MakeBlockElementDto() {
  cli::FragmentDto text;
  text.kind = "static_text";
  text.static_text = cli::StaticTextFragmentDto{"Title"};

  cli::FragmentDto heading;
  heading.kind = "block_element";
  heading.block_element = cli::BlockElementFragmentDto{"heading", "2", {text}};

  cli::CompositionDto composition;
  composition.id = "doc.with_heading";
  composition.fragments.push_back(std::move(heading));
  return composition;
}
```

Then add the test cases:

```cpp
TEST_CASE("group composition DTO round-trips and converts to a draft") {
  const auto source = MakeGroupDto();
  const auto json = rfl::json::write(source);
  const auto parsed = rfl::json::read<cli::CompositionDto>(json);

  REQUIRE(parsed.has_value());
  const auto converted = cli::ToComposition(parsed.value());
  REQUIRE(converted.HasValue());
  CHECK(converted.value().validate().is_success());
  REQUIRE(converted.value().fragmentCount() == 1);
  REQUIRE(converted.value().fragment(0).IsGroup());
  const tf::Group& group = converted.value().fragment(0).AsGroup();
  CHECK(group.kind == tf::GroupKind::Bulleted);
  REQUIRE(group.items.size() == 2);
  CHECK(group.items[0][0].AsStaticText().text() == "first");
  CHECK(group.items[1][0].AsStaticText().text() == "second");
}

TEST_CASE(
    "group composition DTO closes the loop through the real --from-json "
    "production path (ToCompositionDraft), published and rendered") {
  const auto source = MakeGroupDto();
  const auto json = rfl::json::write(source);
  const auto parsed = rfl::json::read<cli::CompositionDto>(json);
  REQUIRE(parsed.has_value());

  auto draft = cli::ToCompositionDraft(parsed.value(), "default");
  REQUIRE(draft.HasValue());

  tf::EngineConfig config;
  config.default_data_path = "memory:cli_dto_group_roundtrip_render";
  tf::Engine engine(std::move(config));

  auto published = engine.PublishComposition(std::move(draft).value());
  REQUIRE(published.HasValue());

  auto result = engine.Render(published.value().id());
  REQUIRE(result.HasValue());
  CHECK(result.value().text == "- first\n- second");
}

TEST_CASE("block_element composition DTO round-trips and converts to a draft") {
  const auto source = MakeBlockElementDto();
  const auto json = rfl::json::write(source);
  const auto parsed = rfl::json::read<cli::CompositionDto>(json);

  REQUIRE(parsed.has_value());
  const auto converted = cli::ToComposition(parsed.value());
  REQUIRE(converted.HasValue());
  CHECK(converted.value().validate().is_success());
  REQUIRE(converted.value().fragmentCount() == 1);
  REQUIRE(converted.value().fragment(0).IsBlockElement());
  const tf::BlockElement& element = converted.value().fragment(0).AsBlockElement();
  CHECK(element.kind == tf::BlockElementKind::Heading);
  CHECK(element.attr == "2");
  CHECK(element.content[0].AsStaticText().text() == "Title");
}

TEST_CASE("an invalid heading level DTO is rejected") {
  auto source = MakeBlockElementDto();
  source.fragments.front().block_element->attr = "9";

  const auto converted = cli::ToComposition(source);

  REQUIRE(converted.HasError());
  CHECK(converted.error().code == tf::ErrorCode::InvalidHeadingLevel);
}

TEST_CASE("fragment DTO rejects a group discriminator with the wrong payload") {
  cli::FragmentDto fragment;
  fragment.kind = "group";
  fragment.static_text = cli::StaticTextFragmentDto{"oops"};

  const auto converted = cli::ToFragment(fragment);

  REQUIRE(converted.HasError());
  CHECK(converted.error().code == tf::ErrorCode::InvalidParamType);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build-rel --target cli_tests --parallel 2>&1 | tail -60`
Expected: compile errors — `cli::GroupFragmentDto`, `cli::GroupItemDto`,
`cli::BlockElementFragmentDto`, `FragmentDto::group`/`block_element` don't
exist yet.

- [ ] **Step 3: Add the new DTO structs to `cli/dto.h`**

Right after `struct ConditionalFragmentDto { ... };` (currently lines 42-45):

```cpp
struct GroupItemDto {
  std::vector<FragmentDto> content;
};

struct GroupFragmentDto {
  std::string kind;  // "bulleted" | "numbered"
  std::vector<GroupItemDto> items;
};

struct BlockElementFragmentDto {
  std::string kind;  // "heading" | "quote" | "code_block"
  std::string attr;
  std::vector<FragmentDto> content;
};
```

In `struct FragmentDto { ... }` (currently lines 47-53), add two more optional
payload fields right after `conditional`:

```cpp
  std::optional<GroupFragmentDto> group;
  std::optional<BlockElementFragmentDto> block_element;
```

- [ ] **Step 4: Implement `ToGroup`/`ToBlockElement` in `cli/dto.cc` and wire them into `ToFragment`/`AppendFragmentToDraft`**

Right after `ToConditional` (currently ends at line 119, before the
`AppendFragmentToDraft` comment):

```cpp
tf::Result<tf::Group> ToGroup(const GroupFragmentDto& dto, bool is_draft_context) {
  tf::Group group;
  group.kind = (dto.kind == "numbered") ? tf::GroupKind::Numbered
                                        : tf::GroupKind::Bulleted;
  group.items.reserve(dto.items.size());

  for (const auto& item_dto : dto.items) {
    auto content = ToFragments(item_dto.content, is_draft_context);
    if (content.HasError()) {
      return tf::Result<tf::Group>(content.error());
    }
    group.items.push_back(std::move(content.value()));
  }

  const auto validation = group.validate(is_draft_context);
  if (validation.is_error()) {
    return tf::Result<tf::Group>(validation);
  }
  return tf::Result<tf::Group>(std::move(group));
}

tf::Result<tf::BlockElement> ToBlockElement(const BlockElementFragmentDto& dto,
                                            bool is_draft_context) {
  tf::BlockElement element;
  if (dto.kind == "heading") {
    element.kind = tf::BlockElementKind::Heading;
  } else if (dto.kind == "code_block") {
    element.kind = tf::BlockElementKind::CodeBlock;
  } else if (dto.kind == "quote") {
    element.kind = tf::BlockElementKind::Quote;
  } else {
    return tf::Result<tf::BlockElement>(
        InvalidDto("Unknown block_element kind: " + dto.kind));
  }
  element.attr = dto.attr;

  auto content = ToFragments(dto.content, is_draft_context);
  if (content.HasError()) {
    return tf::Result<tf::BlockElement>(content.error());
  }
  element.content = std::move(content.value());

  const auto validation = element.validate(is_draft_context);
  if (validation.is_error()) {
    return tf::Result<tf::BlockElement>(validation);
  }
  return tf::Result<tf::BlockElement>(std::move(element));
}
```

In `AppendFragmentToDraft`'s switch, add two cases right after
`FragmentType::Conditional`:

```cpp
    case tf::FragmentType::Group:
      builder.AddGroup(std::move(fragment.AsGroup()));
      return tf::Error::success();
    case tf::FragmentType::BlockElement:
      builder.AddBlockElement(std::move(fragment.AsBlockElement()));
      return tf::Error::success();
```

In `ToFragment`, update the `payload_count` computation to include the two new
optional payloads:

```cpp
  const auto payload_count = static_cast<int>(dto.block_ref.has_value()) +
                              static_cast<int>(dto.static_text.has_value()) +
                              static_cast<int>(dto.separator.has_value()) +
                              static_cast<int>(dto.conditional.has_value()) +
                              static_cast<int>(dto.group.has_value()) +
                              static_cast<int>(dto.block_element.has_value());
```

Then add two more `if (dto.kind == ...)` blocks right after the
`"conditional"` one:

```cpp
  if (dto.kind == "group") {
    if (!dto.group.has_value()) {
      return tf::Result<tf::Fragment>(
          InvalidDto("Fragment kind group requires group payload"));
    }
    auto group = ToGroup(*dto.group, is_draft_context);
    if (group.HasError()) {
      return tf::Result<tf::Fragment>(group.error());
    }
    return tf::Result<tf::Fragment>(tf::Fragment::MakeGroup(std::move(group.value())));
  }

  if (dto.kind == "block_element") {
    if (!dto.block_element.has_value()) {
      return tf::Result<tf::Fragment>(
          InvalidDto("Fragment kind block_element requires block_element payload"));
    }
    auto element = ToBlockElement(*dto.block_element, is_draft_context);
    if (element.HasError()) {
      return tf::Result<tf::Fragment>(element.error());
    }
    return tf::Result<tf::Fragment>(
        tf::Fragment::MakeBlockElement(std::move(element.value())));
  }
```

- [ ] **Step 5: Build and run the tests**

Run: `cmake --build build-rel --target cli_tests --parallel && ./build-rel/tests/cli_tests`
Expected: PASS, all tests including the new ones.

Run: `ctest --test-dir build-rel --output-on-failure`
Expected: PASS across `core_tests`, `cli_tests`, `tfe_smoke`.

- [ ] **Step 6: Commit**

```bash
git add cli/dto.h cli/dto.cc tests/cli_dto_test.cc
git commit -m "$(cat <<'EOF'
Add Group and BlockElement to the CLI --from-json DTO layer

GroupFragmentDto/GroupItemDto and BlockElementFragmentDto join
ConditionalFragmentDto as reflectable JSON payloads FragmentDto can
carry, converted through ToGroup/ToBlockElement -- the same one-directional
DTO-to-domain path ToConditional already established (there is still no
reverse domain-to-DTO path; --json output for compositions stays a
shallow summary view, unrelated to this DTO layer).

Without this, Group/BlockElement fragments were constructible only from
C++ code, not from `tfe comp create --from-json` -- defeating the point
of adding them as composition-authoring primitives. This closes out the
structural-fragments design
(docs/superpowers/specs/2026-09-23-structural-fragments-design.md):
Group and BlockElement are now supported end to end (data model,
validation, Renderer, ObjectBox persistence, CLI).

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

## Self-Review Notes

- **Spec coverage:** Data model + validation (spec "Data model"/"Validation")
  → Task 1. ObjectBox persistence (spec "Persistence") → Task 2, sequenced
  *before* Task 3 specifically because of the `Conditional`/`404bba7`
  precedent the spec itself cites. `ResolveConditionals` reconstruction +
  `RenderGroup`/`RenderBlockElement` (spec "Rendering") → Task 3, including
  both required test directions (`Conditional` inside `Group`, and the
  already-working `Group` inside `Conditional` as a regression check).
  `VisitBlockRefs` (spec) → Task 1, with a three-levels-deep test matching the
  spec's own Testing section. CLI/DTO layer (spec) → Task 4. Every item in the
  spec's Testing section has a corresponding test in some task.
- **Placeholder scan:** none — every step has complete code, exact file
  anchors (current line numbers, verified via direct reads of this branch's
  actual files, not assumed from the spec), and concrete `Run:`/`Expected:`
  pairs.
- **Type consistency:** `Group{kind, items}` / `BlockElement{kind, attr,
  content}` (Task 1) are used with these exact field names in every later
  task (Task 2's `obx_utils.hpp` code, Task 3's `RenderGroup`/
  `RenderBlockElement` signatures, Task 4's `ToGroup`/`ToBlockElement`).
  `Fragment::MakeGroup`/`MakeBlockElement`/`MakeHeading`/`MakeQuote`/
  `MakeCodeBlock` (Task 1) are called with matching signatures in Tasks 2-4.
  `CompositionDraftBuilder::AddGroup`/`AddBlockElement` (Task 1) match their
  call sites in Tasks 2-4 exactly.
