#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cli {

enum class Shell { Bash, Zsh, Fish, Invalid };

[[nodiscard]] Shell ParseShell(std::string_view name) noexcept;
[[nodiscard]] std::optional<Shell> TryParseShell(
    std::string_view name) noexcept;
[[nodiscard]] std::vector<std::string> FilterCompletionCandidates(
    std::vector<std::string> candidates, std::string_view prefix);
[[nodiscard]] std::string GenerateCompletion(Shell shell);

}  // namespace cli
