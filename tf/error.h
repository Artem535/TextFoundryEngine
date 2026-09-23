//
// Created by a.durynin on 29.01.2026.
//

#pragma once

#include <fmt/format.h>
#include <stdexcept>
#include <string>
#include <variant>

namespace tf {

/**
 * Error types for TextFoundry operations.
 * All errors follow the "no magic" principle - deterministic and testable.
 */
enum class ErrorCode {
  // Parameter errors
  MissingParam,      ///< Required parameter not provided
  InvalidParamType,  ///< Parameter type mismatch
  UnknownParam,      ///< Parameter not defined in schema

  // Version errors
  VersionRequired,  ///< BlockRef must specify version
  VersionNotFound,  ///< Requested version does not exist
  InvalidVersion,   ///< Malformed version string

  // Entity errors
  BlockNotFound,        ///< Block with given ID not found
  CompositionNotFound,  ///< Composition with given ID not found
  DuplicateId,          ///< ID already exists

  // State errors
  InvalidStateTransition,  ///< Cannot transition from current state
  DraftRequired,           ///< Operation requires Draft state
  PublishedRequired,       ///< Operation requires Published state

  // Rendering errors
  TemplateSyntaxError,  ///< Invalid template syntax
  CircularReference,    ///< Circular dependency in blocks
  EmptyConditional,        ///< Conditional has zero branches (only Else, or nothing at all)
  EmptyBranchConditions,   ///< A Branch has zero Conditions (always matches -- likely a bug)
  MissingElseBranch,       ///< Conditional::elseContent is std::nullopt
  EmptyGroup,              ///< Group has zero items
  InvalidHeadingLevel,     ///< BlockElement is Heading and attr isn't "1".."6"

  // Storage errors
  StorageError,  ///< Underlying storage failure

  // Success
  Success  ///< No error
};

/**
 * Error structure containing code and message
 */
struct Error {
  ErrorCode code;
  std::string message;

  [[nodiscard]] bool is_error() const noexcept {
    return code != ErrorCode::Success;
  }

  [[nodiscard]] bool is_success() const noexcept {
    return code == ErrorCode::Success;
  }

  bool operator==(const Error& error) const = default;

  // Factory methods for common errors
  [[nodiscard]] static Error MissingParam(const std::string_view& paramName) {
    return Error{ErrorCode::MissingParam,
                 fmt::format("Missing required parameter: {}", paramName)};
  }

  [[nodiscard]] static Error VersionRequired() {
    return Error{
        ErrorCode::VersionRequired,
        "BlockRef must specify version (UseLatest only allowed in Draft)"};
  }

  [[nodiscard]] static Error BlockNotFound(const std::string& blockId) {
    return Error{ErrorCode::BlockNotFound, "Block not found: " + blockId};
  }

  [[nodiscard]] static Error CompositionNotFound(const std::string& compId) {
    return Error{ErrorCode::CompositionNotFound,
                 "Composition not found: " + compId};
  }

  [[nodiscard]] static Error EmptyConditional() {
    return Error{ErrorCode::EmptyConditional,
                 "Conditional must have at least one branch"};
  }

  [[nodiscard]] static Error EmptyBranchConditions() {
    return Error{ErrorCode::EmptyBranchConditions,
                 "Branch must have at least one Condition"};
  }

  [[nodiscard]] static Error MissingElseBranch() {
    return Error{ErrorCode::MissingElseBranch,
                 "Conditional must have an explicit else branch"};
  }

  [[nodiscard]] static Error EmptyGroup() {
    return Error{ErrorCode::EmptyGroup,
                 "Group must have at least one item"};
  }

  [[nodiscard]] static Error InvalidHeadingLevel() {
    return Error{ErrorCode::InvalidHeadingLevel,
                 "Heading level must be an integer from 1 to 6"};
  }

  [[nodiscard]] static Error success() { return Error{ErrorCode::Success, ""}; }
};

/**
 * Result type - either value or error
 * Similar to std::expected (C++23) for compatibility
 */
template <typename T>
class Result {
 public:
  explicit Result(T value) : data_(std::move(value)) {}
  explicit Result(Error error) : data_(std::move(error)) {}

  [[nodiscard]] bool HasValue() const noexcept {
    return std::holds_alternative<T>(data_);
  }

  [[nodiscard]] bool HasError() const noexcept {
    return std::holds_alternative<Error>(data_);
  }

  [[nodiscard]] T& value() & { return std::get<T>(data_); }

  [[nodiscard]] const T& value() const& { return std::get<T>(data_); }

  [[nodiscard]] T&& value() && { return std::get<T>(std::move(data_)); }

  [[nodiscard]] Error& error() & { return std::get<Error>(data_); }

  [[nodiscard]] const Error& error() const& { return std::get<Error>(data_); }

  [[nodiscard]] T* operator->() { return &std::get<T>(data_); }

  [[nodiscard]] const T* operator->() const { return &std::get<T>(data_); }

  [[nodiscard]] T& operator*() & { return std::get<T>(data_); }

  [[nodiscard]] const T& operator*() const& { return std::get<T>(data_); }

 private:
  std::variant<T, Error> data_;
};

/**
 * Exception thrown for unrecoverable engine errors
 */
class EngineException final : public std::runtime_error {
 public:
  explicit EngineException(const std::string& message)
      : std::runtime_error(message) {}
};

}  // namespace tf
