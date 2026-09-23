//
// Created by artem.d on 28.01.2026.
//

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>

#include "block_ref.h"
#include "block_type.hpp"
#include "error.h"
#include "types.h"

namespace tf {

/**
 * Fragment types supported in Composition
 */
enum class FragmentType {
  BlockRef,      ///< Reference to a Block
  StaticText,    ///< Raw text without parameters
  Separator,     ///< Typed separator (newline, paragraph, hr)
  Conditional,   ///< if/elif/else content selection
  Group,         ///< Ordered list of items (bulleted or numbered)
  BlockElement   ///< Heading, blockquote, or fenced code block
};

/**
 * Static text fragment - no parameters, no versioning
 * Stored inline in Composition, doesn't create Block entities
 */
struct StaticText {
  std::string content;

  explicit StaticText(std::string text) : content(std::move(text)) {}

  [[nodiscard]] const std::string& text() const noexcept;
};

/**
 * Separator fragment - typed delimiter
 */
struct Separator {
  SeparatorType type;

  explicit Separator(const SeparatorType& sepType) : type(sepType) {}

  /**
   * Get string representation of separator
   */
  [[nodiscard]] std::string toString() const;
};

// Forward declaration: Branch::content and Conditional::elseContent hold
// nested Fragments, but Fragment itself (defined below) holds a Conditional
// inside its variant, so Fragment must be forward-declared before these.
class Fragment;

/**
 * Condition - DITA-ditaval-style profiling filter: one attribute, a set of
 * allowed values (OR within the set), optional negation. Multiple
 * Conditions on the same Branch combine with AND.
 */
struct Condition {
  std::string attribute;
  std::unordered_set<std::string> allowedValues;
  bool negate = false;

  /**
   * Evaluate against RenderContext params. A missing attribute is treated
   * as not matching -- never an error. This check comes BEFORE negate is
   * applied: with negate=true, a missing attribute still returns false,
   * not true.
   */
  [[nodiscard]] bool matches(const Params& params) const noexcept;
};

/**
 * Branch - one if/elif arm of a Conditional fragment. Conditions combine
 * with AND (a branch with zero conditions is invalid, see
 * Conditional::validate). Content is a nested fragment list, rendered in
 * place when this branch is selected.
 */
struct Branch {
  std::vector<Condition> conditions;
  std::vector<Fragment> content;
};

/**
 * Conditional - if/elif/.../else as a Fragment variant.
 *
 * At render time, branches are evaluated in order; the first branch whose
 * conditions all match (AND) is selected and its content rendered in
 * place. elseContent is mandatory -- std::nullopt (not an empty vector)
 * means "no else was set" and is a validate() error. An empty vector
 * inside the optional is a deliberate, valid "else renders nothing".
 */
struct Conditional {
  std::vector<Branch> branches;
  std::optional<std::vector<Fragment>> elseContent;

  [[nodiscard]] Error validate(bool isDraftContext) const;
};

/**
 * Fluent builder for Conditional. If() opens a new branch; And() adds an
 * additional (AND-combined) Condition to the branch most recently opened
 * by If(); Then() appends a Fragment to that branch's content; Else()
 * appends a Fragment to elseContent (creating it on first call). Then()
 * or And() called before any If() throws EngineException -- a
 * builder-usage bug in the calling C++ code, not a Composition data error.
 */
class ConditionalBuilder {
 public:
  ConditionalBuilder() = default;

  ConditionalBuilder& If(Condition condition);
  ConditionalBuilder& And(Condition condition);
  ConditionalBuilder& Then(Fragment fragment);
  ConditionalBuilder& Else(Fragment fragment);

  /**
   * Sets elseContent to an explicit, empty vector if it hasn't been set
   * yet (a no-op if Else(Fragment) or this was already called). This is
   * the only way to build the valid-but-empty "else renders nothing"
   * shape via the fluent API -- Else(Fragment) alone can't express it,
   * since it always pushes at least one fragment.
   */
  ConditionalBuilder& Else();

  /**
   * Consumes the builder, returning the accumulated Conditional by move.
   * A second call returns an emptied Conditional (branches cleared,
   * elseContent left as whatever moved-from state it was in) -- build()
   * is meant to be called once.
   */
  [[nodiscard]] Conditional build();

 private:
  Conditional cond_;
  bool has_open_branch_ = false;
};

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

/**
 * Fragment - single element of Composition
 * Can be BlockRef, StaticText, or Separator
 */
class Fragment {
 public:
  // Constructors for each fragment type
  Fragment() : data_(StaticText("")) {}

  explicit Fragment(BlockRef blockRef) : data_(std::move(blockRef)) {}

  explicit Fragment(StaticText staticText) : data_(std::move(staticText)) {}

  explicit Fragment(Separator separator) : data_(separator) {}

  explicit Fragment(Conditional conditional) : data_(std::move(conditional)) {}

  explicit Fragment(Group group) : data_(std::move(group)) {}

  explicit Fragment(BlockElement element) : data_(std::move(element)) {}

  // Factory methods
  [[nodiscard]] static Fragment MakeBlockRef(BlockRef ref) {
    return Fragment(std::move(ref));
  }

  [[nodiscard]] static Fragment MakeStaticText(std::string text) {
    return Fragment(StaticText(std::move(text)));
  }

  [[nodiscard]] static Fragment MakeSeparator(SeparatorType type) {
    return Fragment(Separator(type));
  }

  [[nodiscard]] static Fragment MakeConditional(Conditional cond) {
    return Fragment(std::move(cond));
  }

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

  // Type checking
  [[nodiscard]] FragmentType type() const noexcept {
    return std::visit(
        [](const auto& val) -> FragmentType {
          using T = std::decay_t<decltype(val)>;
          if constexpr (std::is_same_v<T, BlockRef>)
            return FragmentType::BlockRef;
          if constexpr (std::is_same_v<T, StaticText>)
            return FragmentType::StaticText;
          if constexpr (std::is_same_v<T, Separator>)
            return FragmentType::Separator;
          if constexpr (std::is_same_v<T, Conditional>)
            return FragmentType::Conditional;
          if constexpr (std::is_same_v<T, Group>)
            return FragmentType::Group;
          if constexpr (std::is_same_v<T, BlockElement>)
            return FragmentType::BlockElement;
          return FragmentType::StaticText;  // default
        },
        data_);
  }

  [[nodiscard]] bool IsBlockRef() const noexcept {
    return std::holds_alternative<BlockRef>(data_);
  }

  [[nodiscard]] bool IsStaticText() const noexcept {
    return std::holds_alternative<StaticText>(data_);
  }

  [[nodiscard]] bool IsSeparator() const noexcept {
    return std::holds_alternative<Separator>(data_);
  }

  [[nodiscard]] bool IsConditional() const noexcept {
    return std::holds_alternative<Conditional>(data_);
  }

  [[nodiscard]] bool IsGroup() const noexcept {
    return std::holds_alternative<Group>(data_);
  }

  [[nodiscard]] bool IsBlockElement() const noexcept {
    return std::holds_alternative<BlockElement>(data_);
  }

  // Accessors (use only after checking type)
  [[nodiscard]] BlockRef& AsBlockRef() & { return std::get<BlockRef>(data_); }

  [[nodiscard]] const BlockRef& AsBlockRef() const& {
    return std::get<BlockRef>(data_);
  }

  [[nodiscard]] StaticText& AsStaticText() & {
    return std::get<StaticText>(data_);
  }

  [[nodiscard]] const StaticText& AsStaticText() const& {
    return std::get<StaticText>(data_);
  }

  [[nodiscard]] Separator& AsSeparator() & {
    return std::get<Separator>(data_);
  }

  [[nodiscard]] const Separator& AsSeparator() const& {
    return std::get<Separator>(data_);
  }

  [[nodiscard]] Conditional& AsConditional() & {
    return std::get<Conditional>(data_);
  }

  [[nodiscard]] const Conditional& AsConditional() const& {
    return std::get<Conditional>(data_);
  }

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

  // Safe accessors returning nullptr if wrong type
  [[nodiscard]] BlockRef* GetBlockRef() noexcept {
    return std::get_if<BlockRef>(&data_);
  }

  [[nodiscard]] const BlockRef* GetBlockRef() const noexcept {
    return std::get_if<BlockRef>(&data_);
  }

  [[nodiscard]] StaticText* GetStaticText() noexcept {
    return std::get_if<StaticText>(&data_);
  }

  [[nodiscard]] const StaticText* GetStaticText() const noexcept {
    return std::get_if<StaticText>(&data_);
  }

  [[nodiscard]] Separator* GetSeparator() noexcept {
    return std::get_if<Separator>(&data_);
  }

  [[nodiscard]] const Separator* GetSeparator() const noexcept {
    return std::get_if<Separator>(&data_);
  }

  [[nodiscard]] Conditional* GetConditional() noexcept {
    return std::get_if<Conditional>(&data_);
  }

  [[nodiscard]] const Conditional* GetConditional() const noexcept {
    return std::get_if<Conditional>(&data_);
  }

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

  // Validation
  [[nodiscard]] Error validate(bool isDraftContext = false) const;

 private:
  std::variant<BlockRef, StaticText, Separator, Conditional, Group, BlockElement> data_;
};

/**
 * Recursively visits every BlockRef reachable from a fragment list,
 * including ones nested inside Conditional branches and elseContent.
 * Engine-level code that needs to see every BlockRef a Composition could
 * possibly render (usage checks, rewrite/normalization passes) should use
 * this instead of walking a fragment list directly and assuming BlockRefs
 * only ever appear at the top level -- that assumption stopped holding
 * once Fragment gained the Conditional variant.
 */
void VisitBlockRefs(const std::vector<Fragment>& fragments,
                    const std::function<void(const BlockRef&)>& visitor);

}  // namespace tf
