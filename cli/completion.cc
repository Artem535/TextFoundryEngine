#include "completion.h"

#include <algorithm>
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
    for (const auto& alias : command.aliases) {
      result << ' ' << alias;
    }
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
         << "  local root_index=1\n"
         << "  local i=1\n"
         << "  while (( i < COMP_CWORD )); do\n"
         << "    case \"${COMP_WORDS[i]}\" in\n"
         << "      --data|-d|--project|-P) (( i += 2 )) ;;\n"
         << "      --strict|--json) (( i++ )) ;;\n"
         << "      block|b|comp|composition|render|validate) root_index=$i; break ;;\n"
         << "      *) (( i++ )) ;;\n"
         << "    esac\n"
         << "  done\n"
         << "  local relative_index=$((COMP_CWORD-root_index))\n"
         << "  if [[ $relative_index -eq 2 ]]; then\n"
         << "    local query_kind=\"\"\n"
         << "    case \"${COMP_WORDS[root_index]}:${COMP_WORDS[root_index+1]}\" in\n"
         << "      block:inspect|block:deprecate|b:inspect|b:deprecate) query_kind=block ;;\n"
         << "      comp:inspect|comp:deprecate|composition:inspect|composition:deprecate) query_kind=composition ;;\n"
         << "      render:block|validate:block) query_kind=block ;;\n"
         << "      render:composition|validate:composition) query_kind=composition ;;\n"
         << "    esac\n"
         << "    if [[ -n \"$query_kind\" ]]; then\n"
         << "      local -a query_args=()\n"
         << "      for (( i=1; i<COMP_CWORD; i++ )); do\n"
         << "        case \"${COMP_WORDS[i]}\" in\n"
         << "          --data|-d|--project|-P) query_args+=(\"${COMP_WORDS[i]}\" \"${COMP_WORDS[i+1]}\"); (( i++ )) ;;\n"
         << "        esac\n"
         << "      done\n"
         << "      COMPREPLY=( $(tfe \"${query_args[@]}\" __complete \"$query_kind\" \"$cur\" 2>/dev/null) )\n"
         << "      return\n"
         << "    fi\n"
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
         << "_tfe_complete_dynamic() {\n"
         << "  if (( CURRENT == 4 )); then\n"
         << "    case \"$words[2]:$words[3]\" in\n"
         << "      block:inspect|block:deprecate|b:inspect|b:deprecate)\n"
         << "        local -a ids=(\"${(@f)$(tfe __complete block \"$words[CURRENT]\" 2>/dev/null)}\")\n"
         << "        _describe 'block id' ids; return ;;\n"
         << "      comp:inspect|comp:deprecate|composition:inspect|composition:deprecate)\n"
         << "        local -a ids=(\"${(@f)$(tfe __complete composition \"$words[CURRENT]\" 2>/dev/null)}\")\n"
         << "        _describe 'composition id' ids; return ;;\n"
         << "      render:block|validate:block)\n"
         << "        local -a ids=(\"${(@f)$(tfe __complete block \"$words[CURRENT]\" 2>/dev/null)}\")\n"
         << "        _describe 'block id' ids; return ;;\n"
         << "      render:composition|validate:composition)\n"
         << "        local -a ids=(\"${(@f)$(tfe __complete composition \"$words[CURRENT]\" 2>/dev/null)}\")\n"
         << "        _describe 'composition id' ids; return ;;\n"
         << "    esac\n"
         << "  fi\n"
         << "  _arguments '1:command:(" << RootCommandNames() << ")' "
         << "'--data[Engine data path]:path:_files' "
         << "'--from-json[JSON input]:file:_files' "
         << "'*:option:(" << GlobalOptionNames() << ")'\n"
         << "}\n"
         << "compdef _tfe_complete_dynamic tfe\n";
  return result.str();
}

std::string GenerateFish() {
  std::ostringstream result;
  result << "# fish completion for tfe\n";
  for (const auto& command : RootCommands()) {
    result << "complete -c tfe -f -n \"__fish_use_subcommand\" -a \""
           << command.name << "\" -d \"" << command.description << "\"\n";
    for (const auto& alias : command.aliases) {
      result << "complete -c tfe -f -n \"__fish_use_subcommand\" -a \""
             << alias << "\" -d \"" << command.description << "\"\n";
    }
  }
  for (const auto& option : GlobalOptions()) {
    result << "complete -c tfe -l " << option.long_name;
    if (!option.short_name.empty()) {
      result << " -s " << option.short_name;
    }
    result << "\n";
  }
  result << "complete -c tfe -f -n '__fish_seen_subcommand_from block b; __fish_seen_subcommand_from inspect deprecate' -a '(tfe __complete block (commandline -ct) 2>/dev/null)'\n"
         << "complete -c tfe -f -n '__fish_seen_subcommand_from comp composition; __fish_seen_subcommand_from inspect deprecate' -a '(tfe __complete composition (commandline -ct) 2>/dev/null)'\n"
         << "complete -c tfe -f -n '__fish_seen_subcommand_from render validate; __fish_seen_subcommand_from block' -a '(tfe __complete block (commandline -ct) 2>/dev/null)'\n"
         << "complete -c tfe -f -n '__fish_seen_subcommand_from render validate; __fish_seen_subcommand_from composition' -a '(tfe __complete composition (commandline -ct) 2>/dev/null)'\n";
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

std::vector<std::string> FilterCompletionCandidates(
    std::vector<std::string> candidates, std::string_view prefix) {
  std::sort(candidates.begin(), candidates.end());
  candidates.erase(std::unique(candidates.begin(), candidates.end()),
                   candidates.end());
  candidates.erase(
      std::remove_if(candidates.begin(), candidates.end(),
                     [prefix](const std::string& candidate) {
                       return candidate.size() < prefix.size() ||
                              candidate.compare(0, prefix.size(), prefix) != 0;
                     }),
      candidates.end());
  return candidates;
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
