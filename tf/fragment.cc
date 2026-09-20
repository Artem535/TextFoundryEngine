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
