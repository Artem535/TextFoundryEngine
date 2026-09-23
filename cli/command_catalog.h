#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace cli {

struct OptionSpec {
  std::string long_name;
  std::string short_name;
  bool takes_value = false;
  std::string value_hint;
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

inline const std::string& CommandSpecName(const CommandSpec& spec) {
  return spec.name;
}

inline const std::string& OptionSpecName(const OptionSpec& option) {
  return option.long_name;
}

/**
 * Linear-scans a catalog range for an entry whose name (per `name_of`)
 * matches, or throws std::logic_error -- shared by every catalog lookup
 * (root commands, subcommands, global options) instead of each call site
 * hand-rolling the same scan-or-throw loop.
 */
template <typename Range, typename NameOf>
const typename Range::value_type& FindByName(const Range& items,
                                             std::string_view name,
                                             NameOf name_of,
                                             std::string_view what) {
  for (const auto& item : items) {
    if (name_of(item) == name) {
      return item;
    }
  }
  throw std::logic_error("missing " + std::string(what) +
                         " catalog entry: " + std::string(name));
}

}  // namespace cli
