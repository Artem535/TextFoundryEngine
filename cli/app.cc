#include "app.h"

#include <CLI/CLI.hpp>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "../tf/error.h"
#include "../tf/logger.h"

#include "command_catalog.h"
#include "commands.h"

namespace cli {
namespace {

bool HasJsonFlag(int argc, char** argv) {
  for (int index = 1; index < argc; ++index) {
    if (std::string_view(argv[index]) == "--json") {
      return true;
    }
  }
  return false;
}

const OptionSpec& FindGlobalOption(std::string_view name) {
  for (const auto& option : GlobalOptions()) {
    if (option.long_name == name) {
      return option;
    }
  }
  throw std::logic_error("missing global option catalog entry: " +
                         std::string(name));
}

std::string OptionFlags(const OptionSpec& option) {
  if (option.short_name.empty()) {
    return "--" + option.long_name;
  }
  return "-" + option.short_name + ",--" + option.long_name;
}

}  // namespace

int RunApplication(int argc, char** argv, std::ostream& output,
                   std::ostream& errors) {
  tf::Logger::init(tf::LogLevel::Warn);
  struct LoggerGuard {
    ~LoggerGuard() { tf::Logger::shutdown(); }
  } logger_guard;

  CLI::App app{"Standalone TextFoundryEngine CLI", "tfe"};
  AppState state{AppConfig{}, output, errors};
  state.config.json = HasJsonFlag(argc, argv);
  app.add_option(OptionFlags(FindGlobalOption("data")), state.config.data_path,
                 "Engine data path");
  app.add_option(OptionFlags(FindGlobalOption("project")),
                 state.config.project_key,
                 "Project key");
  app.add_flag(OptionFlags(FindGlobalOption("strict")), state.config.strict,
                "Fail rendering on missing parameters");
  app.add_flag(OptionFlags(FindGlobalOption("json")), state.config.json,
                "Print JSON instead of a table");
  app.fallthrough();
  RegisterCommands(app, state);

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& error) {
    if (state.config.json && error.get_exit_code() != 0) {
      (void)PrintError(
          tf::Error{tf::ErrorCode::InvalidParamType, error.what()}, true,
          output, errors);
      return 2;
    }
    const int cli_code = app.exit(error, output, errors);
    return cli_code == 0 ? 0 : 2;
  } catch (const std::exception& error) {
    return PrintError(
        tf::Error{tf::ErrorCode::StorageError, error.what()},
        state.config.json, output, errors);
  }
  return state.result_code;
}

int RunApplication(const std::vector<std::string>& args,
                   std::ostream& output, std::ostream& errors) {
  std::vector<std::string> storage = args;
  std::vector<char*> argv;
  argv.reserve(storage.size());
  for (auto& arg : storage) {
    argv.push_back(arg.data());
  }
  return RunApplication(static_cast<int>(argv.size()), argv.data(), output,
                        errors);
}

}  // namespace cli
