#include <doctest/doctest.h>

#include <sstream>
#include <string>
#include <vector>

#include "../cli/app.h"

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
