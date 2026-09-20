#include <doctest/doctest.h>

#include <rfl/json.hpp>

#include <string>

#include "../cli/dto.h"
#include "../tf/engine.h"

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

TEST_CASE(
    "conditional composition DTO closes the loop: JSON in, through the real "
    "--from-json production path (ToCompositionDraft), published and "
    "rendered with different outcomes per RenderContext") {
  const auto source = MakeConditionalDto();
  const auto json = rfl::json::write(source);
  const auto parsed = rfl::json::read<cli::CompositionDto>(json);
  REQUIRE(parsed.has_value());

  auto draft = cli::ToCompositionDraft(parsed.value(), "default");
  REQUIRE(draft.HasValue());

  tf::EngineConfig config;
  config.default_data_path = "memory:cli_dto_roundtrip_render";
  tf::Engine engine(std::move(config));

  auto published = engine.PublishComposition(std::move(draft).value());
  REQUIRE(published.HasValue());

  auto matching = engine.Render(
      published.value().id(), tf::RenderContext{}.WithParam("language", "ru"));
  REQUIRE(matching.HasValue());
  CHECK(matching.value().text.find("hello") != std::string::npos);

  auto fallthrough = engine.Render(
      published.value().id(), tf::RenderContext{}.WithParam("language", "fr"));
  REQUIRE(fallthrough.HasValue());
  CHECK(fallthrough.value().text.find("fallback") != std::string::npos);

  CHECK(matching.value().text != fallthrough.value().text);
}

TEST_CASE("conditional DTO without else content is rejected") {
  auto source = MakeConditionalDto();
  source.fragments.front().conditional->else_content.reset();

  const auto converted = cli::ToComposition(source);

  REQUIRE(converted.HasError());
  CHECK(converted.error().code == tf::ErrorCode::MissingElseBranch);
}

TEST_CASE(
    "conditional DTO without else content is rejected through the real "
    "--from-json production path too, not just ToComposition") {
  auto source = MakeConditionalDto();
  source.fragments.front().conditional->else_content.reset();

  const auto converted = cli::ToCompositionDraft(source, "default");

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

TEST_CASE(
    "block_ref DTO with no version converts to a fragment that uses the "
    "latest published version, both directly and through --from-json") {
  cli::FragmentDto fragment;
  fragment.kind = "block_ref";
  fragment.block_ref = cli::BlockRefFragmentDto{"greeting", std::nullopt, {}};

  const auto converted = cli::ToFragment(fragment);
  REQUIRE(converted.HasValue());
  REQUIRE(converted.value().IsBlockRef());
  CHECK(converted.value().AsBlockRef().UseLatest());

  cli::CompositionDto composition;
  composition.id = "uses.latest";
  composition.fragments.push_back(fragment);
  const auto draft = cli::ToCompositionDraft(composition, "default");
  REQUIRE(draft.HasValue());
}
