//
// Created by artem.d on 28.01.2026.
//

#include "fragment.h"

namespace tf {

std::string Separator::toString() const {
  switch (type) {
    case SeparatorType::Newline:
      return "\n";
    case SeparatorType::Paragraph:
      return "\n\n";
    case SeparatorType::Hr:
      return "\n---\n";
  }
  return "";
}

// StaticText implementation
const std::string& StaticText::text() const noexcept { return content; }

// Condition implementation
bool Condition::matches(const Params& params) const noexcept {
  auto it = params.find(attribute);
  if (it == params.end()) {
    return false;
  }
  bool inSet = allowedValues.contains(it->second);
  return negate ? !inSet : inSet;
}

// Conditional implementation
Error Conditional::validate(bool isDraftContext) const {
  if (branches.empty()) {
    return Error::EmptyConditional();
  }

  for (const auto& branch : branches) {
    if (branch.conditions.empty()) {
      return Error::EmptyBranchConditions();
    }
    for (const auto& fragment : branch.content) {
      auto err = fragment.validate(isDraftContext);
      if (err.is_error()) {
        return err;
      }
    }
  }

  if (!elseContent.has_value()) {
    return Error::MissingElseBranch();
  }

  for (const auto& fragment : *elseContent) {
    auto err = fragment.validate(isDraftContext);
    if (err.is_error()) {
      return err;
    }
  }

  return Error::success();
}

// ConditionalBuilder implementation
ConditionalBuilder& ConditionalBuilder::If(Condition condition) {
  Branch branch;
  branch.conditions.push_back(std::move(condition));
  cond_.branches.push_back(std::move(branch));
  has_open_branch_ = true;
  return *this;
}

ConditionalBuilder& ConditionalBuilder::And(Condition condition) {
  if (!has_open_branch_) {
    throw EngineException("ConditionalBuilder::And called before If()");
  }
  cond_.branches.back().conditions.push_back(std::move(condition));
  return *this;
}

ConditionalBuilder& ConditionalBuilder::Then(Fragment fragment) {
  if (!has_open_branch_) {
    throw EngineException("ConditionalBuilder::Then called before If()");
  }
  cond_.branches.back().content.push_back(std::move(fragment));
  return *this;
}

ConditionalBuilder& ConditionalBuilder::Else(Fragment fragment) {
  if (!cond_.elseContent.has_value()) {
    cond_.elseContent = std::vector<Fragment>{};
  }
  cond_.elseContent->push_back(std::move(fragment));
  return *this;
}

Conditional ConditionalBuilder::build() { return std::move(cond_); }

// Fragment implementation
Error Fragment::validate(bool isDraftContext) const {
  return std::visit(
      [&](const auto& val) -> Error {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, BlockRef>) {
          return val.validate(isDraftContext);
        }
        if constexpr (std::is_same_v<T, Conditional>) {
          return val.validate(isDraftContext);
        }
        // StaticText and Separator are always valid
        return Error::success();
      },
      data_);
}
}  // namespace tf
