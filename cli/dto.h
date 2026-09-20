#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../tf/composition.h"
#include "../tf/error.h"
#include "../tf/fragment.h"

namespace cli {

struct FragmentDto;

struct ConditionDto {
  std::string attribute;
  std::vector<std::string> allowed_values;
  bool negate = false;
};

struct BranchDto {
  std::vector<ConditionDto> conditions;
  std::vector<FragmentDto> content;
};

struct BlockRefFragmentDto {
  std::string block_id;
  std::optional<std::string> version;
  std::unordered_map<std::string, std::string> params;
};

struct StaticTextFragmentDto {
  std::string text;
};

struct SeparatorFragmentDto {
  std::string separator_type;
};

struct ConditionalFragmentDto {
  std::vector<BranchDto> branches;
  std::optional<std::vector<FragmentDto>> else_content;
};

struct FragmentDto {
  std::string kind;
  std::optional<BlockRefFragmentDto> block_ref;
  std::optional<StaticTextFragmentDto> static_text;
  std::optional<SeparatorFragmentDto> separator;
  std::optional<ConditionalFragmentDto> conditional;
};

struct CompositionDto {
  std::string id;
  std::optional<std::string> description;
  std::vector<FragmentDto> fragments;
};

[[nodiscard]] tf::Result<tf::Fragment> ToFragment(
    const FragmentDto& dto, bool is_draft_context = true);

[[nodiscard]] tf::Result<tf::Version> ParseVersion(std::string_view text);

[[nodiscard]] tf::Result<tf::Composition> ToComposition(
    const CompositionDto& dto);

[[nodiscard]] tf::Result<tf::CompositionDraft> ToCompositionDraft(
    const CompositionDto& dto, std::string project_key = "default");

}  // namespace cli
