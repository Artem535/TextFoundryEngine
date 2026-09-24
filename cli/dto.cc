#include "dto.h"

#include <charconv>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>

namespace cli {
namespace {

tf::Error InvalidDto(std::string message) {
  return tf::Error{tf::ErrorCode::InvalidParamType, std::move(message)};
}

tf::Result<tf::Version> ParseVersionImpl(std::string_view text) {
  const auto separator = text.find('.');
  if (separator == std::string_view::npos ||
      text.find('.', separator + 1) != std::string_view::npos) {
    return tf::Result<tf::Version>(tf::Error{
        tf::ErrorCode::InvalidVersion,
        "Version must use the major.minor format: " + std::string(text)});
  }

  const auto parse_part = [](std::string_view part,
                             uint16_t& result) -> bool {
    if (part.empty()) {
      return false;
    }
    uint32_t value = 0;
    const auto* begin = part.data();
    const auto* end = begin + part.size();
    const auto parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end ||
        value > std::numeric_limits<uint16_t>::max()) {
      return false;
    }
    result = static_cast<uint16_t>(value);
    return true;
  };

  tf::Version version;
  if (!parse_part(text.substr(0, separator), version.major) ||
      !parse_part(text.substr(separator + 1), version.minor)) {
    return tf::Result<tf::Version>(tf::Error{
        tf::ErrorCode::InvalidVersion,
        "Version must use the major.minor format: " + std::string(text)});
  }
  return tf::Result<tf::Version>(version);
}

tf::Result<tf::SeparatorType> ParseSeparator(std::string_view text) {
  if (text == "newline") {
    return tf::Result<tf::SeparatorType>(tf::SeparatorType::Newline);
  }
  if (text == "paragraph") {
    return tf::Result<tf::SeparatorType>(tf::SeparatorType::Paragraph);
  }
  if (text == "hr") {
    return tf::Result<tf::SeparatorType>(tf::SeparatorType::Hr);
  }
  return tf::Result<tf::SeparatorType>(InvalidDto(
      "Unknown separator_type: " + std::string(text)));
}

template <typename T>
tf::Result<std::vector<tf::Fragment>> ToFragments(
    const std::vector<T>& source, bool is_draft_context) {
  std::vector<tf::Fragment> fragments;
  fragments.reserve(source.size());
  for (const auto& dto : source) {
    auto converted = ToFragment(dto, is_draft_context);
    if (converted.HasError()) {
      return tf::Result<std::vector<tf::Fragment>>(converted.error());
    }
    fragments.push_back(std::move(converted.value()));
  }
  return tf::Result<std::vector<tf::Fragment>>(std::move(fragments));
}

tf::Result<tf::Conditional> ToConditional(
    const ConditionalFragmentDto& dto, bool is_draft_context) {
  tf::Conditional conditional;
  conditional.branches.reserve(dto.branches.size());

  for (const auto& branch_dto : dto.branches) {
    tf::Branch branch;
    branch.conditions.reserve(branch_dto.conditions.size());
    for (const auto& condition_dto : branch_dto.conditions) {
      tf::Condition condition;
      condition.attribute = condition_dto.attribute;
      condition.allowedValues.insert(condition_dto.allowed_values.begin(),
                                     condition_dto.allowed_values.end());
      condition.negate = condition_dto.negate;
      branch.conditions.push_back(std::move(condition));
    }

    auto content = ToFragments(branch_dto.content, is_draft_context);
    if (content.HasError()) {
      return tf::Result<tf::Conditional>(content.error());
    }
    branch.content = std::move(content.value());
    conditional.branches.push_back(std::move(branch));
  }

  if (dto.else_content.has_value()) {
    auto else_content = ToFragments(*dto.else_content, is_draft_context);
    if (else_content.HasError()) {
      return tf::Result<tf::Conditional>(else_content.error());
    }
    conditional.elseContent = std::move(else_content.value());
  }

  const auto validation = conditional.validate(is_draft_context);
  if (validation.is_error()) {
    return tf::Result<tf::Conditional>(validation);
  }
  return tf::Result<tf::Conditional>(std::move(conditional));
}

tf::Result<tf::Group> ToGroup(const GroupFragmentDto& dto, bool is_draft_context) {
  tf::Group group;
  group.kind = (dto.kind == "numbered") ? tf::GroupKind::Numbered
                                        : tf::GroupKind::Bulleted;
  group.items.reserve(dto.items.size());

  for (const auto& item_dto : dto.items) {
    auto content = ToFragments(item_dto.content, is_draft_context);
    if (content.HasError()) {
      return tf::Result<tf::Group>(content.error());
    }
    group.items.push_back(std::move(content.value()));
  }

  const auto validation = group.validate(is_draft_context);
  if (validation.is_error()) {
    return tf::Result<tf::Group>(validation);
  }
  return tf::Result<tf::Group>(std::move(group));
}

tf::Result<tf::BlockElement> ToBlockElement(const BlockElementFragmentDto& dto,
                                            bool is_draft_context) {
  tf::BlockElement element;
  if (dto.kind == "heading") {
    element.kind = tf::BlockElementKind::Heading;
  } else if (dto.kind == "code_block") {
    element.kind = tf::BlockElementKind::CodeBlock;
  } else if (dto.kind == "quote") {
    element.kind = tf::BlockElementKind::Quote;
  } else {
    return tf::Result<tf::BlockElement>(
        InvalidDto("Unknown block_element kind: " + dto.kind));
  }
  element.attr = dto.attr;

  auto content = ToFragments(dto.content, is_draft_context);
  if (content.HasError()) {
    return tf::Result<tf::BlockElement>(content.error());
  }
  element.content = std::move(content.value());

  const auto validation = element.validate(is_draft_context);
  if (validation.is_error()) {
    return tf::Result<tf::BlockElement>(validation);
  }
  return tf::Result<tf::BlockElement>(std::move(element));
}

// Appends an already-converted, already-validated domain Fragment to a
// draft builder. This is the only place a converted Fragment is unpacked
// back into CompositionDraftBuilder's typed Add* calls, so ToCompositionDraft
// shares exactly the same conversion (ToFragment, below) that ToComposition
// and the DTO tests use -- there is no second, divergent JSON->domain path
// that could enforce the tagged-union invariant differently or accept
// shapes (e.g. a block_ref with no version, meaning "use latest") that the
// other path rejects.
tf::Error AppendFragmentToDraft(tf::CompositionDraftBuilder& builder,
                                tf::Fragment fragment) {
  switch (fragment.type()) {
    case tf::FragmentType::BlockRef:
      builder.AddBlockRef(std::move(fragment.AsBlockRef()));
      return tf::Error::success();
    case tf::FragmentType::StaticText:
      builder.AddStaticText(std::move(fragment.AsStaticText().content));
      return tf::Error::success();
    case tf::FragmentType::Separator:
      builder.AddSeparator(fragment.AsSeparator().type);
      return tf::Error::success();
    case tf::FragmentType::Conditional:
      builder.AddConditional(std::move(fragment.AsConditional()));
      return tf::Error::success();
    case tf::FragmentType::Group:
      builder.AddGroup(std::move(fragment.AsGroup()));
      return tf::Error::success();
    case tf::FragmentType::BlockElement:
      builder.AddBlockElement(std::move(fragment.AsBlockElement()));
      return tf::Error::success();
  }
  return InvalidDto("Unknown fragment type");
}

}  // namespace

tf::Result<tf::Version> ParseVersion(std::string_view text) {
  return ParseVersionImpl(text);
}

tf::Result<tf::Fragment> ToFragment(const FragmentDto& dto,
                                     bool is_draft_context) {
  const auto payload_count = static_cast<int>(dto.block_ref.has_value()) +
                              static_cast<int>(dto.static_text.has_value()) +
                              static_cast<int>(dto.separator.has_value()) +
                              static_cast<int>(dto.conditional.has_value()) +
                              static_cast<int>(dto.group.has_value()) +
                              static_cast<int>(dto.block_element.has_value());

  if (payload_count != 1) {
    return tf::Result<tf::Fragment>(InvalidDto(
        "Fragment must contain exactly one payload for kind: " + dto.kind));
  }

  if (dto.kind == "block_ref") {
    if (!dto.block_ref.has_value()) {
      return tf::Result<tf::Fragment>(InvalidDto(
          "Fragment kind block_ref requires block_ref payload"));
    }
    tf::BlockRef ref(dto.block_ref->block_id);
    ref.SetLocalParams(dto.block_ref->params);
    if (dto.block_ref->version.has_value()) {
      auto version = ParseVersionImpl(*dto.block_ref->version);
      if (version.HasError()) {
        return tf::Result<tf::Fragment>(version.error());
      }
      ref.SetVersion(version.value());
    }
    const auto validation = ref.validate(is_draft_context);
    if (validation.is_error()) {
      return tf::Result<tf::Fragment>(validation);
    }
    return tf::Result<tf::Fragment>(
        tf::Fragment::MakeBlockRef(std::move(ref)));
  }

  if (dto.kind == "static_text") {
    if (!dto.static_text.has_value()) {
      return tf::Result<tf::Fragment>(InvalidDto(
          "Fragment kind static_text requires static_text payload"));
    }
    return tf::Result<tf::Fragment>(
        tf::Fragment::MakeStaticText(dto.static_text->text));
  }

  if (dto.kind == "separator") {
    if (!dto.separator.has_value()) {
      return tf::Result<tf::Fragment>(InvalidDto(
          "Fragment kind separator requires separator payload"));
    }
    auto separator = ParseSeparator(dto.separator->separator_type);
    if (separator.HasError()) {
      return tf::Result<tf::Fragment>(separator.error());
    }
    return tf::Result<tf::Fragment>(
        tf::Fragment::MakeSeparator(separator.value()));
  }

  if (dto.kind == "conditional") {
    if (!dto.conditional.has_value()) {
      return tf::Result<tf::Fragment>(InvalidDto(
          "Fragment kind conditional requires conditional payload"));
    }
    auto conditional = ToConditional(*dto.conditional, is_draft_context);
    if (conditional.HasError()) {
      return tf::Result<tf::Fragment>(conditional.error());
    }
    return tf::Result<tf::Fragment>(
        tf::Fragment::MakeConditional(std::move(conditional.value())));
  }

  if (dto.kind == "group") {
    if (!dto.group.has_value()) {
      return tf::Result<tf::Fragment>(
          InvalidDto("Fragment kind group requires group payload"));
    }
    auto group = ToGroup(*dto.group, is_draft_context);
    if (group.HasError()) {
      return tf::Result<tf::Fragment>(group.error());
    }
    return tf::Result<tf::Fragment>(tf::Fragment::MakeGroup(std::move(group.value())));
  }

  if (dto.kind == "block_element") {
    if (!dto.block_element.has_value()) {
      return tf::Result<tf::Fragment>(
          InvalidDto("Fragment kind block_element requires block_element payload"));
    }
    auto element = ToBlockElement(*dto.block_element, is_draft_context);
    if (element.HasError()) {
      return tf::Result<tf::Fragment>(element.error());
    }
    return tf::Result<tf::Fragment>(
        tf::Fragment::MakeBlockElement(std::move(element.value())));
  }

  return tf::Result<tf::Fragment>(
      InvalidDto("Unknown fragment kind: " + dto.kind));
}

tf::Result<tf::Composition> ToComposition(const CompositionDto& dto) {
  tf::Composition composition(dto.id);
  if (dto.description.has_value()) {
    composition.SetDescription(*dto.description);
  }

  for (const auto& fragment_dto : dto.fragments) {
    auto fragment = ToFragment(fragment_dto, true);
    if (fragment.HasError()) {
      return tf::Result<tf::Composition>(fragment.error());
    }
    composition.InsertFragment(composition.fragmentCount(),
                               std::move(fragment.value()));
  }

  const auto validation = composition.validate();
  if (validation.is_error()) {
    return tf::Result<tf::Composition>(validation);
  }
  return tf::Result<tf::Composition>(std::move(composition));
}

tf::Result<tf::CompositionDraft> ToCompositionDraft(
    const CompositionDto& dto, std::string project_key) {
  tf::CompositionDraftBuilder builder(dto.id);
  builder.WithProjectKey(std::move(project_key));
  if (dto.description.has_value()) {
    builder.WithDescription(*dto.description);
  }

  for (const auto& fragment_dto : dto.fragments) {
    auto fragment = ToFragment(fragment_dto, /*is_draft_context=*/true);
    if (fragment.HasError()) {
      return tf::Result<tf::CompositionDraft>(fragment.error());
    }
    const auto error =
        AppendFragmentToDraft(builder, std::move(fragment.value()));
    if (error.is_error()) {
      return tf::Result<tf::CompositionDraft>(error);
    }
  }

  return tf::Result<tf::CompositionDraft>(builder.build());
}

}  // namespace cli
