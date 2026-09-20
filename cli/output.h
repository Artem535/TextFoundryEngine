#pragma once

#include <iostream>
#include <ostream>
#include <string>
#include <vector>

#include <ftxui/dom/elements.hpp>
#include <rfl/json.hpp>

#include "../tf/error.h"

namespace cli {

struct IdListView {
  std::string kind;
  std::vector<std::string> ids;
};

struct EntityView {
  std::string kind;
  std::string id;
  std::string version;
  std::string state;
};

struct RenderView {
  std::string kind = "render";
  std::string text;
  std::string composition_id;
  std::string composition_version;
};

struct ValidationView {
  std::string kind;
  std::string id;
  bool valid = false;
  std::string message;
};

struct ErrorPayload {
  std::string code;
  std::string message;
};

struct ErrorView {
  ErrorPayload error;
};

template <typename View>
[[nodiscard]] std::string ToJson(const View& view) {
  return rfl::json::write(view);
}

[[nodiscard]] ftxui::Element ToTable(const IdListView& view);
[[nodiscard]] ftxui::Element ToTable(const EntityView& view);
[[nodiscard]] ftxui::Element ToTable(const RenderView& view);
[[nodiscard]] ftxui::Element ToTable(const ValidationView& view);

template <typename View>
void PrintResult(const View& view, bool json_mode,
                 std::ostream& output = std::cout) {
  if (json_mode) {
    output << ToJson(view) << '\n';
    return;
  }

  auto element = ToTable(view);
  auto screen = ftxui::Screen::Create(ftxui::Dimension::Fit(element));
  ftxui::Render(screen, element);
  output << screen.ToString() << '\n';
}

[[nodiscard]] std::string ErrorCodeName(tf::ErrorCode code);
[[nodiscard]] std::string ErrorJson(const tf::Error& error);
[[nodiscard]] int PrintError(const tf::Error& error, bool json_mode,
                              std::ostream& output, std::ostream& errors);

}  // namespace cli
