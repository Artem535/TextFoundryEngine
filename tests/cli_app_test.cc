#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <rfl/json.hpp>

#include "../cli/app.h"
#include "../cli/dto.h"
#include "../cli/output.h"

namespace {

int Run(std::initializer_list<std::string> args, std::ostringstream& output,
        std::ostringstream& errors) {
  return cli::RunApplication(std::vector<std::string>(args), output, errors);
}

}  // namespace

TEST_CASE("tfe help is a successful CLI11 result") {
  std::ostringstream output;
  std::ostringstream errors;

  const auto result = Run({"tfe", "--help"}, output, errors);

  CHECK(result == 0);
  CHECK(output.str().find("tfe") != std::string::npos);
  CHECK(output.str().find("Aliases:") != std::string::npos);
  CHECK(output.str().find("Examples:") != std::string::npos);
  CHECK(output.str().find("tfe block create") != std::string::npos);
  CHECK(errors.str().empty());
}

TEST_CASE("tfe accepts stable root command aliases") {
  std::ostringstream output;
  std::ostringstream errors;

  const auto result = Run({"tfe", "--data", "memory:cli_alias_block", "b",
                           "list", "--json"},
                          output, errors);

  CHECK(result == 0);
  CHECK(output.str().find("\"kind\":\"blocks\"") != std::string::npos);
  CHECK(errors.str().empty());
}

TEST_CASE("tfe accepts the long composition alias") {
  std::ostringstream output;
  std::ostringstream errors;

  const auto result = Run({"tfe", "--data", "memory:cli_alias_composition",
                           "composition", "list", "--json"},
                          output, errors);

  CHECK(result == 0);
  CHECK(output.str().find("\"kind\":\"compositions\"") !=
        std::string::npos);
  CHECK(errors.str().empty());
}

TEST_CASE("tfe maps CLI11 usage errors to exit code 2") {
  std::ostringstream output;
  std::ostringstream errors;

  const auto result = Run({"tfe", "block", "inspect"}, output, errors);

  CHECK(result == 2);
  CHECK(errors.str().find("required") != std::string::npos);
}

TEST_CASE("tfe maps JSON CLI11 usage errors to one JSON document") {
  std::ostringstream output;
  std::ostringstream errors;

  const auto result = Run({"tfe", "--json", "block", "inspect"}, output,
                          errors);

  CHECK(result == 2);
  CHECK(output.str().find("\"error\"") != std::string::npos);
  CHECK(errors.str().empty());
}

TEST_CASE("tfe rejects flag and JSON composition input as a usage error") {
  std::ostringstream output;
  std::ostringstream errors;

  const auto result = Run({"tfe", "--json", "comp", "create", "welcome",
                           "--from-json", "-", "--text", "hello"},
                          output, errors);

  CHECK(result == 2);
  CHECK(output.str().find("\"error\"") != std::string::npos);
  CHECK(errors.str().empty());
}

TEST_CASE("tfe maps engine errors to JSON exit code 1") {
  std::ostringstream output;
  std::ostringstream errors;

  const auto result = Run({"tfe", "--data", "memory:cli_app_test",
                           "--json", "block", "inspect", "missing"},
                          output, errors);

  CHECK(result == 1);
  CHECK(output.str().find("\"error\"") != std::string::npos);
  CHECK(output.str().find("StorageError") != std::string::npos);
  CHECK(errors.str().empty());
}

TEST_CASE("tfe completion does not initialize the engine") {
  std::ostringstream output;
  std::ostringstream errors;

  const auto result = Run({"tfe", "completion", "bash"}, output, errors);

  CHECK(result == 0);
  CHECK(output.str().find("complete -F") != std::string::npos);
  CHECK(errors.str().empty());
}

TEST_CASE(
    "tfe --json list output actually parses as JSON matching IdListView, "
    "not just a matching substring") {
  std::ostringstream output;
  std::ostringstream errors;

  const auto result = Run(
      {"tfe", "--data", "memory:cli_json_list_shape", "block", "list",
       "--json"},
      output, errors);

  CHECK(result == 0);
  const auto parsed = rfl::json::read<cli::IdListView>(output.str());
  REQUIRE(parsed.has_value());
  CHECK(parsed.value().kind == "blocks");
  CHECK(parsed.value().ids.empty());
  CHECK(errors.str().empty());
}

TEST_CASE(
    "tfe --json single-entity output for a real created block actually "
    "parses as JSON matching EntityView") {
  const auto data_path =
      std::filesystem::temp_directory_path() / "tfe_json_entity_shape_test";
  std::filesystem::remove_all(data_path);

  std::ostringstream create_output;
  std::ostringstream create_errors;
  const auto create_result =
      Run({"tfe", "--data", data_path.string(), "block", "create",
          "greeting.hello", "-t", "Hello, {{name}}!"},
         create_output, create_errors);
  REQUIRE(create_result == 0);

  std::ostringstream output;
  std::ostringstream errors;
  const auto result =
      Run({"tfe", "--data", data_path.string(), "--json", "block", "inspect",
          "greeting.hello"},
         output, errors);

  std::filesystem::remove_all(data_path);

  CHECK(result == 0);
  const auto parsed = rfl::json::read<cli::EntityView>(output.str());
  REQUIRE(parsed.has_value());
  CHECK(parsed.value().kind == "block");
  CHECK(parsed.value().id == "greeting.hello");
  CHECK(parsed.value().version == "1.0");
  CHECK(parsed.value().state == "published");
  CHECK(errors.str().empty());
}

TEST_CASE(
    "tfe surfaces a DTO conversion error through the real --from-json path "
    "as a JSON error document with exit code 1, matching the documented "
    "error contract") {
  cli::FragmentDto greeting;
  greeting.kind = "static_text";
  greeting.static_text = cli::StaticTextFragmentDto{"hello"};

  cli::BranchDto branch;
  branch.conditions.push_back(cli::ConditionDto{"language", {"ru"}, false});
  branch.content.push_back(greeting);

  cli::FragmentDto conditional;
  conditional.kind = "conditional";
  // else_content deliberately left unset (nullopt): the domain model
  // requires it, so conversion must fail with MissingElseBranch.
  conditional.conditional = cli::ConditionalFragmentDto{{branch}, std::nullopt};

  cli::CompositionDto dto;
  dto.fragments.push_back(conditional);

  const auto json_path =
      std::filesystem::temp_directory_path() / "tfe_missing_else_test.json";
  {
    std::ofstream file(json_path);
    file << rfl::json::write(dto);
  }

  std::ostringstream output;
  std::ostringstream errors;
  const auto result = Run(
      {"tfe", "--data", "memory:cli_missing_else", "--json", "comp", "create",
       "no_else", "--from-json", json_path.string()},
      output, errors);

  std::filesystem::remove(json_path);

  CHECK(result == 1);
  CHECK(output.str().find("\"error\"") != std::string::npos);
  CHECK(output.str().find("MissingElseBranch") != std::string::npos);
  CHECK(errors.str().empty());
}
