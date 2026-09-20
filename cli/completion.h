#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace cli {

enum class Shell { Bash, Zsh, Fish, Invalid };

[[nodiscard]] Shell ParseShell(std::string_view name) noexcept;
[[nodiscard]] std::optional<Shell> TryParseShell(
    std::string_view name) noexcept;
[[nodiscard]] std::string GenerateCompletion(Shell shell);

}  // namespace cli
