#include "command_catalog.h"

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
       {}},
      {"comp",
       "Manage compositions",
       {{"create", "Create a composition", {}, {}},
        {"list", "List compositions", {}, {}},
        {"deprecate", "Deprecate a composition version", {}, {}},
        {"inspect", "Inspect a composition", {}, {}}},
       {}},
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

}  // namespace cli
