#include "output.h"

#include <ftxui/dom/table.hpp>

#include <string>
#include <utility>

namespace cli {
namespace {

ftxui::Element MakeTable(std::vector<std::vector<std::string>> rows) {
  ftxui::Table table(std::move(rows));
  table.SelectAll().Border(ftxui::LIGHT);
  table.SelectAll().Separator(ftxui::LIGHT);
  return table.Render();
}

}  // namespace

ftxui::Element ToTable(const IdListView& view) {
  std::vector<std::vector<std::string>> rows{{view.kind + " id"}};
  rows.reserve(view.ids.size() + 1);
  for (const auto& id : view.ids) {
    rows.push_back({id});
  }
  return MakeTable(std::move(rows));
}

ftxui::Element ToTable(const EntityView& view) {
  return MakeTable({{"kind", view.kind},
                    {"id", view.id},
                    {"version", view.version},
                    {"state", view.state}});
}

ftxui::Element ToTable(const RenderView& view) {
  return MakeTable({{"kind", view.kind},
                    {"composition", view.composition_id},
                    {"version", view.composition_version},
                    {"text", view.text}});
}

ftxui::Element ToTable(const ValidationView& view) {
  return MakeTable({{"kind", view.kind},
                    {"id", view.id},
                    {"valid", view.valid ? "true" : "false"},
                    {"message", view.message}});
}

std::string ErrorCodeName(tf::ErrorCode code) {
  switch (code) {
    case tf::ErrorCode::MissingParam:
      return "MissingParam";
    case tf::ErrorCode::InvalidParamType:
      return "InvalidParamType";
    case tf::ErrorCode::UnknownParam:
      return "UnknownParam";
    case tf::ErrorCode::VersionRequired:
      return "VersionRequired";
    case tf::ErrorCode::VersionNotFound:
      return "VersionNotFound";
    case tf::ErrorCode::InvalidVersion:
      return "InvalidVersion";
    case tf::ErrorCode::BlockNotFound:
      return "BlockNotFound";
    case tf::ErrorCode::CompositionNotFound:
      return "CompositionNotFound";
    case tf::ErrorCode::DuplicateId:
      return "DuplicateId";
    case tf::ErrorCode::InvalidStateTransition:
      return "InvalidStateTransition";
    case tf::ErrorCode::DraftRequired:
      return "DraftRequired";
    case tf::ErrorCode::PublishedRequired:
      return "PublishedRequired";
    case tf::ErrorCode::TemplateSyntaxError:
      return "TemplateSyntaxError";
    case tf::ErrorCode::CircularReference:
      return "CircularReference";
    case tf::ErrorCode::EmptyConditional:
      return "EmptyConditional";
    case tf::ErrorCode::EmptyBranchConditions:
      return "EmptyBranchConditions";
    case tf::ErrorCode::MissingElseBranch:
      return "MissingElseBranch";
    case tf::ErrorCode::EmptyGroup:
      return "EmptyGroup";
    case tf::ErrorCode::InvalidHeadingLevel:
      return "InvalidHeadingLevel";
    case tf::ErrorCode::StorageError:
      return "StorageError";
    case tf::ErrorCode::Success:
      return "Success";
  }
  return "Unknown";
}

std::string ErrorJson(const tf::Error& error) {
  return ToJson(ErrorView{ErrorPayload{ErrorCodeName(error.code),
                                       error.message}});
}

int PrintError(const tf::Error& error, bool json_mode, std::ostream& output,
               std::ostream& errors) {
  if (json_mode) {
    output << ErrorJson(error) << '\n';
  } else {
    errors << "Error: " << error.message << '\n';
  }
  return 1;
}

}  // namespace cli
