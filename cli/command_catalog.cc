#include "command_catalog.h"

#include <sstream>

namespace cli {

const std::vector<OptionSpec>& GlobalOptions() {
  static const std::vector<OptionSpec> options = {
      {"data", "d", true, "PATH", {}, true, false},
      {"project", "P", true, "KEY", {}, false, false},
      {"strict", "", false, "", {}, false, false},
      {"json", "", false, "", {}, false, false},
  };
  return options;
}

const std::vector<CommandSpec>& RootCommands() {
  static const std::vector<CommandSpec> commands = {
      {"block",
       "Manage blocks",
       {{"create", "Create a block", {}, {}},
        {"publish", "Publish a block version", {}, {}},
        {"list", "List blocks", {}, {}},
        {"deprecate", "Deprecate a block version", {}, {}},
        {"inspect", "Inspect a block", {}, {}}},
       {},
       {"b"},
       {"tfe block create welcome --template 'Hello, {{name}}!'",
        "tfe b list"}},
      {"comp",
       "Manage compositions",
       {{"create", "Create a composition", {}, {}},
        {"list", "List compositions", {}, {}},
        {"deprecate", "Deprecate a composition version", {}, {}},
        {"inspect", "Inspect a composition", {}, {}}},
       {},
       {"composition"},
       {"tfe composition list"}},
      {"render", "Render a block or composition", {}, {}},
      {"validate", "Validate a block or composition", {}, {}},
      {"completion",
       "Generate shell completion",
       {},
       {{"shell", "", true, "SHELL", {"bash", "zsh", "fish"}, false,
         false}}},
  };
  return commands;
}

std::string RootHelpFooter() {
  std::ostringstream result;
  result << "Aliases:\n";
  for (const auto& command : RootCommands()) {
    for (const auto& alias : command.aliases) {
      result << "  " << alias << " -> " << command.name << '\n';
    }
  }
  result << "\nExamples:\n";
  for (const auto& command : RootCommands()) {
    for (const auto& example : command.examples) {
      result << "  " << example << '\n';
    }
  }
  return result.str();
}

}  // namespace cli
