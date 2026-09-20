#include <doctest/doctest.h>

#include <rfl/json.hpp>

#include <string>

#include "../cli/dto.h"

namespace {

cli::CompositionDto MakeConditionalDto() {
  cli::FragmentDto greeting;
  greeting.kind = "static_text";
  greeting.static_text = cli::StaticTextFragmentDto{"hello"};

  cli::FragmentDto fallback;
  fallback.kind = "static_text";
  fallback.static_text = cli::StaticTextFragmentDto{"fallback"};

  cli::BranchDto branch;
  branch.conditions.push_back(
      cli::ConditionDto{"language", {"ru", "en"}, false});
  branch.content.push_back(greeting);

  cli::FragmentDto conditional;
  conditional.kind = "conditional";
  conditional.conditional = cli::ConditionalFragmentDto{
      {branch}, std::vector<cli::FragmentDto>{fallback}};

  cli::CompositionDto composition;
  composition.id = "welcome";
  composition.description = "conditional greeting";
  composition.fragments.push_back(std::move(conditional));
  return composition;
}

}  // namespace

TEST_CASE("conditional composition DTO round-trips and converts to a draft") {
  const auto source = MakeConditionalDto();
  const auto json = rfl::json::write(source);
  const auto parsed = rfl::json::read<cli::CompositionDto>(json);

  REQUIRE(parsed.has_value());
  const auto converted = cli::ToComposition(parsed.value());
  REQUIRE(converted.HasValue());
  CHECK(converted.value().validate().is_success());
  REQUIRE(converted.value().fragmentCount() == 1);
  CHECK(converted.value().fragment(0).IsConditional());
}

TEST_CASE("conditional DTO without else content is rejected") {
  auto source = MakeConditionalDto();
  source.fragments.front().conditional->else_content.reset();

  const auto converted = cli::ToComposition(source);

  REQUIRE(converted.HasError());
  CHECK(converted.error().code == tf::ErrorCode::MissingElseBranch);
}

TEST_CASE("fragment DTO rejects a discriminator with the wrong payload") {
  cli::FragmentDto fragment;
  fragment.kind = "static_text";
  fragment.block_ref = cli::BlockRefFragmentDto{"block", std::nullopt, {}};

  const auto converted = cli::ToFragment(fragment);

  REQUIRE(converted.HasError());
  CHECK(converted.error().code == tf::ErrorCode::InvalidParamType);
}
