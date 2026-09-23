#include <doctest/doctest.h>

#include <sstream>
#include <string>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

#include "../cli/output.h"

TEST_CASE("CLI views render as JSON and an FTXUI table") {
  const cli::IdListView view{"blocks", {"welcome", "farewell"}};

  const auto json = cli::ToJson(view);
  CHECK(json.find("blocks") != std::string::npos);
  CHECK(json.find("welcome") != std::string::npos);

  auto element = cli::ToTable(view);
  auto screen = ftxui::Screen::Create(ftxui::Dimension::Fit(element));
  ftxui::Render(screen, element);
  const auto rendered = screen.ToString();
  CHECK(rendered.find("welcome") != std::string::npos);
  CHECK(rendered.find("farewell") != std::string::npos);
}

TEST_CASE("CLI errors use the nested JSON error shape") {
  const tf::Error error{tf::ErrorCode::InvalidParamType, "bad input"};

  const auto json = cli::ErrorJson(error);
  CHECK(json ==
        R"({"error":{"code":"InvalidParamType","message":"bad input"}})");

  std::ostringstream stdout_stream;
  std::ostringstream stderr_stream;
  CHECK(cli::PrintError(error, false, stdout_stream, stderr_stream) == 1);
  CHECK(stdout_stream.str().empty());
  CHECK(stderr_stream.str().find("Error:") != std::string::npos);
}
