#pragma once

#include <string>
#include <vector>

namespace cli {

struct OptionSpec {
  std::string long_name;
  std::string short_name;
  bool takes_value = false;
  std::string value_hint;
  std::vector<std::string> choices;
  bool file_path = false;
  bool repeatable = false;
};

struct CommandSpec {
  std::string name;
  std::string description;
  std::vector<CommandSpec> subcommands;
  std::vector<OptionSpec> options;
  std::vector<std::string> aliases;
  std::vector<std::string> examples;
};

[[nodiscard]] const std::vector<OptionSpec>& GlobalOptions();
[[nodiscard]] const std::vector<CommandSpec>& RootCommands();
[[nodiscard]] std::string RootHelpFooter();

}  // namespace cli
