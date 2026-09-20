#include <doctest/doctest.h>

#include <algorithm>
#include <string>

#include "../cli/command_catalog.h"
#include "../cli/completion.h"

TEST_CASE("command catalog exposes the stable tfe surface") {
  const auto& commands = cli::RootCommands();

  REQUIRE(commands.size() == 5);
  CHECK(commands[0].name == "block");
  CHECK(commands[1].name == "comp");
  CHECK(commands[2].name == "render");
  CHECK(commands[3].name == "validate");
  CHECK(commands[4].name == "completion");

  const auto& options = cli::GlobalOptions();
  CHECK(std::any_of(options.begin(), options.end(),
                    [](const cli::OptionSpec& option) {
                      return option.long_name == "json";
                    }));
}

TEST_CASE("shell completion contains commands and global options") {
  const auto bash = cli::GenerateCompletion(cli::Shell::Bash);
  const auto zsh = cli::GenerateCompletion(cli::Shell::Zsh);
  const auto fish = cli::GenerateCompletion(cli::Shell::Fish);

  for (const auto* script : {&bash, &zsh, &fish}) {
    CHECK(script->find("block") != std::string::npos);
    CHECK(script->find(" b") != std::string::npos);
    CHECK(script->find("comp") != std::string::npos);
    CHECK(script->find("composition") != std::string::npos);
    CHECK(script->find("completion") != std::string::npos);
    CHECK(script->find("json") != std::string::npos);
    CHECK(script->find("project") != std::string::npos);
  }

  CHECK(bash.find("complete") != std::string::npos);
  CHECK(bash.find("compgen -f") != std::string::npos);
  CHECK(zsh.find("#compdef tfe") != std::string::npos);
  CHECK(zsh.find("_files") != std::string::npos);
  CHECK(fish.find("complete -c tfe") != std::string::npos);
  CHECK(fish.find("-r -F") != std::string::npos);
}

TEST_CASE("shell parser accepts only supported shells") {
  CHECK(cli::ParseShell("bash") == cli::Shell::Bash);
  CHECK(cli::ParseShell("zsh") == cli::Shell::Zsh);
  CHECK(cli::ParseShell("fish") == cli::Shell::Fish);
  CHECK_FALSE(cli::TryParseShell("powershell").has_value());
}
