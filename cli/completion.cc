#include "completion.h"

#include <sstream>
#include <string>
#include <string_view>

#include "command_catalog.h"

namespace cli {
namespace {

std::string RootCommandNames() {
  std::ostringstream result;
  bool first = true;
  for (const auto& command : RootCommands()) {
    if (!first) {
      result << ' ';
    }
    first = false;
    result << command.name;
  }
  return result.str();
}

std::string GlobalOptionNames() {
  std::ostringstream result;
  for (const auto& option : GlobalOptions()) {
    result << "--" << option.long_name << ' ';
    if (!option.short_name.empty()) {
      result << '-' << option.short_name << ' ';
    }
  }
  result << "--help";
  return result.str();
}

std::string GenerateBash() {
  const auto commands = RootCommandNames();
  const auto options = GlobalOptionNames();
  std::ostringstream result;
  result << "# bash completion for tfe\n"
         << "_tfe_complete() {\n"
         << "  local cur=\"" "$" "{COMP_WORDS[COMP_CWORD]}" "\"\n"
         << "  local prev=\"" "$" "{COMP_WORDS[COMP_CWORD-1]}" "\"\n"
         << "  if [[ \"$prev\" == \"--data\" || \"$prev\" == \"--from-json\" ]]; then\n"
         << "    COMPREPLY=( $(compgen -f -- \"$cur\") )\n"
         << "    return\n"
         << "  fi\n"
         << "  local words=\"" << commands << " " << options << "\"\n"
         << "  if [[ " "$" "{COMP_CWORD} -eq 1 ]]; then\n"
         << "    COMPREPLY=( $(compgen -W \"" << commands
         << "\" -- \"$cur\") )\n"
         << "  else\n"
         << "    COMPREPLY=( $(compgen -W \"$words\" -- \"$cur\") )\n"
         << "  fi\n"
         << "}\n"
         << "complete -F _tfe_complete tfe\n";
  return result.str();
}

std::string GenerateZsh() {
  std::ostringstream result;
  result << "#compdef tfe\n"
         << "_arguments '1:command:(" << RootCommandNames() << ")' "
         << "'--data[Engine data path]:path:_files' "
         << "'--from-json[JSON input]:file:_files' "
         << "'*:option:(" << GlobalOptionNames() << ")'\n";
  return result.str();
}

std::string GenerateFish() {
  std::ostringstream result;
  result << "# fish completion for tfe\n";
  for (const auto& command : RootCommands()) {
    result << "complete -c tfe -f -n \"__fish_use_subcommand\" -a \""
           << command.name << "\" -d \"" << command.description << "\"\n";
  }
  for (const auto& option : GlobalOptions()) {
    result << "complete -c tfe -l " << option.long_name;
    if (!option.short_name.empty()) {
      result << " -s " << option.short_name;
    }
    result << "\n";
  }
  result << "complete -c tfe -l data -r -F\n"
         << "complete -c tfe -l from-json -r -F\n";
  return result.str();
}

}  // namespace

Shell ParseShell(std::string_view name) noexcept {
  if (name == "bash") {
    return Shell::Bash;
  }
  if (name == "zsh") {
    return Shell::Zsh;
  }
  if (name == "fish") {
    return Shell::Fish;
  }
  return Shell::Invalid;
}

std::optional<Shell> TryParseShell(std::string_view name) noexcept {
  const auto shell = ParseShell(name);
  if (shell == Shell::Invalid) {
    return std::nullopt;
  }
  return shell;
}

std::string GenerateCompletion(Shell shell) {
  switch (shell) {
    case Shell::Bash:
      return GenerateBash();
    case Shell::Zsh:
      return GenerateZsh();
    case Shell::Fish:
      return GenerateFish();
    case Shell::Invalid:
      return {};
  }
  return {};
}

}  // namespace cli
