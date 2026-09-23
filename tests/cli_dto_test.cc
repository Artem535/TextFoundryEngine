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

cli::CompositionDto MakeGroupDto() {
  cli::FragmentDto first;
  first.kind = "static_text";
  first.static_text = cli::StaticTextFragmentDto{"first"};

  cli::FragmentDto second;
  second.kind = "static_text";
  second.static_text = cli::StaticTextFragmentDto{"second"};

  cli::GroupItemDto item1;
  item1.content.push_back(first);
  cli::GroupItemDto item2;
  item2.content.push_back(second);

  cli::FragmentDto group;
  group.kind = "group";
  group.group = cli::GroupFragmentDto{"bulleted", {item1, item2}};

  cli::CompositionDto composition;
  composition.id = "shopping_list";
  composition.fragments.push_back(std::move(group));
  return composition;
}

cli::CompositionDto MakeBlockElementDto() {
  cli::FragmentDto text;
  text.kind = "static_text";
  text.static_text = cli::StaticTextFragmentDto{"Title"};

  cli::FragmentDto heading;
  heading.kind = "block_element";
  heading.block_element = cli::BlockElementFragmentDto{"heading", "2", {text}};

  cli::CompositionDto composition;
  composition.id = "doc.with_heading";
  composition.fragments.push_back(std::move(heading));
  return composition;
}

}  // namespace

TEST_CASE("group composition DTO round-trips and converts to a draft") {
  const auto source = MakeGroupDto();
  const auto json = rfl::json::write(source);
  const auto parsed = rfl::json::read<cli::CompositionDto>(json);

  REQUIRE(parsed.has_value());
  const auto converted = cli::ToComposition(parsed.value());
  REQUIRE(converted.HasValue());
  CHECK(converted.value().validate().is_success());
  REQUIRE(converted.value().fragmentCount() == 1);
  REQUIRE(converted.value().fragment(0).IsGroup());
  const tf::Group& group = converted.value().fragment(0).AsGroup();
  CHECK(group.kind == tf::GroupKind::Bulleted);
  REQUIRE(group.items.size() == 2);
  CHECK(group.items[0][0].AsStaticText().text() == "first");
  CHECK(group.items[1][0].AsStaticText().text() == "second");
}

TEST_CASE(
    "group composition DTO closes the loop through the real --from-json "
    "production path (ToCompositionDraft), published and rendered") {
  const auto source = MakeGroupDto();
  const auto json = rfl::json::write(source);
  const auto parsed = rfl::json::read<cli::CompositionDto>(json);
  REQUIRE(parsed.has_value());

  auto draft = cli::ToCompositionDraft(parsed.value(), "default");
  REQUIRE(draft.HasValue());

  tf::EngineConfig config;
  config.default_data_path = "memory:cli_dto_group_roundtrip_render";
  tf::Engine engine(std::move(config));

  auto published = engine.PublishComposition(std::move(draft).value());
  REQUIRE(published.HasValue());

  auto result = engine.Render(published.value().id());
  REQUIRE(result.HasValue());
  CHECK(result.value().text == "- first\n- second");
}

TEST_CASE("block_element composition DTO round-trips and converts to a draft") {
  const auto source = MakeBlockElementDto();
  const auto json = rfl::json::write(source);
  const auto parsed = rfl::json::read<cli::CompositionDto>(json);

  REQUIRE(parsed.has_value());
  const auto converted = cli::ToComposition(parsed.value());
  REQUIRE(converted.HasValue());
  CHECK(converted.value().validate().is_success());
  REQUIRE(converted.value().fragmentCount() == 1);
  REQUIRE(converted.value().fragment(0).IsBlockElement());
  const tf::BlockElement& element = converted.value().fragment(0).AsBlockElement();
  CHECK(element.kind == tf::BlockElementKind::Heading);
  CHECK(element.attr == "2");
  CHECK(element.content[0].AsStaticText().text() == "Title");
}

TEST_CASE("an invalid heading level DTO is rejected") {
  auto source = MakeBlockElementDto();
  source.fragments.front().block_element->attr = "9";

  const auto converted = cli::ToComposition(source);

  REQUIRE(converted.HasError());
  CHECK(converted.error().code == tf::ErrorCode::InvalidHeadingLevel);
}

TEST_CASE("fragment DTO rejects a group discriminator with the wrong payload") {
  cli::FragmentDto fragment;
  fragment.kind = "group";
  fragment.static_text = cli::StaticTextFragmentDto{"oops"};

  const auto converted = cli::ToFragment(fragment);

  REQUIRE(converted.HasError());
  CHECK(converted.error().code == tf::ErrorCode::InvalidParamType);
}

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
    "latest published version, and the resulting draft genuinely preserves "
    "that -- not silently pinned to a nonexistent Version{0,0} -- verified "
    "by publishing it through a real Engine") {
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
  auto draft = cli::ToCompositionDraft(composition, "default");
  REQUIRE(draft.HasValue());

  // A composition can never be published while any block_ref floats to
  // "latest" (Composition::publish's UseLatest guard) -- compositions
  // always pin explicit versions. So the meaningful assertion here is not
  // "publishing succeeds", it's that the ref's UseLatest survived
  // ToCompositionDraft intact and is still caught by that guard: if
  // CompositionDraftBuilder::AddBlockRef had instead silently pinned the
  // ref to Version{0,0} (a real regression this test caught once), the
  // guard would never fire and this composition would publish
  // successfully while being permanently unrenderable.
  tf::EngineConfig config;
  config.default_data_path = "memory:cli_dto_uses_latest_rejected";
  tf::Engine engine(std::move(config));
  auto published = engine.PublishComposition(std::move(draft).value());
  REQUIRE(published.HasError());
  CHECK(published.error().code == tf::ErrorCode::VersionRequired);
}
