//
// Created by a.durynin on 29.01.2026.
//

#include "renderer.h"

#include <sstream>

#include "logger.h"

namespace tf {

// RenderResult implementation
bool RenderResult::isEmpty() const noexcept { return text.empty(); }

// Renderer implementation
Renderer::Renderer(std::unique_ptr<IBlockCache> cache)
    : blockCache_(std::move(cache)) {}

void Renderer::SetBlockCache(std::unique_ptr<IBlockCache> cache) {
  blockCache_ = std::move(cache);
}

void Renderer::ClearCache() {
  if (blockCache_) {
    blockCache_->clear();
  }
}

Result<RenderResult> Renderer::Render(const Composition& composition,
                                      const RenderContext& context) const {
  TF_LOG_DEBUG("Rendering composition [id={}, version={}.{}]", composition.id(),
               composition.version().major, composition.version().minor);

  // Only Published compositions can be rendered
  if (composition.state() != BlockState::Published) {
    TF_LOG_ERROR(
        "Cannot render composition: only Published compositions can be "
        "rendered [id={}, state={}]",
        composition.id(), static_cast<int>(composition.state()));
    return Result<RenderResult>(
        Error{ErrorCode::PublishedRequired,
              "Only Published compositions can be rendered"});
  }

  // Render all fragments
  std::vector<std::string> fragmentTexts;
  std::vector<std::pair<BlockId, Version>> blocksUsed;

  auto resolvedFragments =
      ResolveConditionals(composition.fragments(), context.params);

  TF_LOG_TRACE("Rendering {} fragments", resolvedFragments.size());
  for (const auto& fragment : resolvedFragments) {
    auto result = RenderFragment(fragment, context, blocksUsed);
    if (result.HasError()) {
      TF_LOG_ERROR("Failed to render fragment: {}", result.error().message);
      return Result<RenderResult>(result.error());
    }
    fragmentTexts.push_back(std::move(result.value()));
  }

  // Apply structural style
  auto style = GetEffectiveStyle(composition);
  std::string finalText = ApplyStructuralStyle(fragmentTexts, style);

  RenderResult result;
  result.text = std::move(finalText);
  result.compositionId = composition.id();
  result.compositionVersion = composition.version();
  result.blocksUsed = std::move(blocksUsed);

  TF_LOG_DEBUG("Composition rendered successfully [id={}, blocks_used={}]",
               composition.id(), result.blocksUsed.size());

  return Result<RenderResult>(result);
}

Result<std::string> Renderer::RenderBlock(const Block& block,
                                          const RenderContext& context) {
  // Merge defaults with context params (context has priority)
  Params merged = block.defaults();
  for (const auto& [key, value] : context.params) {
    merged[key] = value;
  }

  return block.templ().Expand(merged);
}

std::string Renderer::ApplyStructuralStyle(
    const std::vector<std::string>& fragmentTexts,
    const StructuralStyle& style) {
  std::ostringstream result;

  // Add preamble
  if (style.preamble.has_value()) {
    result << style.preamble.value();
  }

  // Join fragments with delimiter
  for (size_t i = 0; i < fragmentTexts.size(); ++i) {
    std::string text = fragmentTexts[i];

    // Apply block wrapper if present
    if (style.blockWrapper.has_value()) {
      std::string wrapped = style.blockWrapper.value();
      // Replace {{content}} placeholder
      constexpr std::string_view content = "{{content}}";
      if (const size_t pos = wrapped.find(content); pos != std::string::npos) {
        wrapped.replace(pos, content.size(), text);
      }
      text = wrapped;
    }

    result << text;

    // Add delimiter between fragments (not after last)
    if (i < fragmentTexts.size() - 1 && style.delimiter.has_value()) {
      result << style.delimiter.value();
    }
  }

  // Add postamble
  if (style.postamble.has_value()) {
    result << style.postamble.value();
  }

  return result.str();
}

Result<std::string> Renderer::RenderFragment(
    const Fragment& fragment, const RenderContext& context,
    std::vector<std::pair<BlockId, Version>>& blocksUsed) const {
  switch (fragment.type()) {
    case FragmentType::BlockRef:
      return ExpandBlockRef(fragment.AsBlockRef(), context, blocksUsed);

    case FragmentType::StaticText:
      return Result<std::string>(fragment.AsStaticText().text());

    case FragmentType::Separator:
      return Result<std::string>(fragment.AsSeparator().toString());

    case FragmentType::Conditional:
      // Unreachable in practice: Render() always resolves Conditionals via
      // ResolveConditionals before this function ever sees a fragment.
      return Result<std::string>(
          Error{ErrorCode::InvalidParamType,
                "Conditional fragment reached RenderFragment unresolved"});

    case FragmentType::Group:
      return RenderGroup(fragment.AsGroup(), context, blocksUsed);

    case FragmentType::BlockElement:
      return RenderBlockElement(fragment.AsBlockElement(), context, blocksUsed);
  }
  return Result<std::string>(
      Error{ErrorCode::InvalidParamType, "Unknown fragment type"});
}

Result<std::string> Renderer::ExpandBlockRef(
    const BlockRef& blockRef, const RenderContext& context,
    std::vector<std::pair<BlockId, Version>>& blocksUsed) const {
  if (!blockCache_) {
    return Result<std::string>(
        Error{ErrorCode::StorageError, "No block cache available"});
  }

  const Block* block = nullptr;
  Version usedVersion;

  if (blockRef.UseLatest()) {
    block = blockCache_->GetLatestBlock(blockRef.GetBlockId());
    if (block) {
      usedVersion = block->version();
    }
  } else {
    usedVersion = blockRef.version().value_or(Version{0, 0});
    block = blockCache_->GetBlock(blockRef.GetBlockId(), usedVersion);
  }

  if (!block) {
    return Result<std::string>(Error::BlockNotFound(blockRef.GetBlockId()));
  }

  // Track block usage
  blocksUsed.emplace_back(blockRef.GetBlockId(), usedVersion);

  // Resolve parameters
  auto paramsResult = blockRef.ResolveParams(*block, context.params);
  if (paramsResult.HasError()) {
    return Result<std::string>(paramsResult.error());
  }

  // Expand template
  return block->templ().Expand(paramsResult.value());
}

// Why two functions, not one: the natural-looking single-function version
// (render each item to one string with `depth * 2` spaces of leading indent
// baked in, recursing into a nested Group at `depth + 1`, then joining) has a
// real bug: a nested Group's own lines already come back pre-indented for
// *their* depth, so splicing them into the parent item's line list and then
// *also* prefixing every non-first line with the parent's marker-width
// padding double-indents them. The clean fix is for RenderGroupLines to
// never bake in any indent for its own depth at all -- it always renders as
// if it were top-level ("1. salt", not "  1. salt") -- and exactly one place
// adds exactly one level of indent (marker.size() spaces) to every line of a
// finished Group's output *except* the first: the same uniform per-line rule
// already applied to a single item's own wrapped multi-line text. That rule
// applied once, at exactly the point where a rendered value (whether an
// ordinary fragment's multi-line text or a whole nested list) is folded into
// `raw`, is what gives correct, single-level indentation with no
// special-casing needed for "was this line's origin a nested list or not".
Result<std::vector<std::string>> Renderer::RenderGroupLines(
    const Group& group, const RenderContext& context,
    std::vector<std::pair<BlockId, Version>>& blocksUsed) const {
  std::vector<std::string> lines;

  for (size_t itemIndex = 0; itemIndex < group.items.size(); ++itemIndex) {
    const std::string marker = (group.kind == GroupKind::Numbered)
                                   ? (std::to_string(itemIndex + 1) + ". ")
                                   : std::string("- ");
    const std::string padding(marker.size(), ' ');

    // This item's own contributed lines, unindented and without the marker
    // -- built left to right. Consecutive plain fragments concatenate onto
    // the same running line (direct concatenation, per the design spec); a
    // nested Group always ends the current line and contributes its own
    // lines (also unindented at this point -- see the two-function split
    // above) as additional entries.
    std::vector<std::string> raw;
    std::ostringstream currentLine;
    bool haveOpenLine = false;

    for (const auto& fragment : group.items[itemIndex]) {
      if (fragment.IsGroup()) {
        auto nestedLines = RenderGroupLines(fragment.AsGroup(), context, blocksUsed);
        if (nestedLines.HasError()) {
          return nestedLines;
        }
        if (haveOpenLine) {
          raw.push_back(currentLine.str());
          currentLine.str("");
          haveOpenLine = false;
        }
        for (const auto& nestedLine : nestedLines.value()) {
          raw.push_back(nestedLine);
        }
        continue;
      }

      auto rendered = RenderFragment(fragment, context, blocksUsed);
      if (rendered.HasError()) {
        return Result<std::vector<std::string>>(rendered.error());
      }
      // A rendered fragment's own text might already contain '\n' (e.g. a
      // multi-line StaticText) -- split so each becomes its own raw line
      // rather than corrupting later marker alignment.
      std::istringstream renderedStream(rendered.value());
      std::string piece;
      bool firstPiece = true;
      while (std::getline(renderedStream, piece)) {
        if (!firstPiece) {
          raw.push_back(currentLine.str());
          currentLine.str("");
        }
        currentLine << piece;
        haveOpenLine = true;
        firstPiece = false;
      }
    }
    if (haveOpenLine) {
      raw.push_back(currentLine.str());
    }
    if (raw.empty()) {
      raw.push_back("");  // an item with zero fragments still gets a marker-only line
    }

    for (size_t i = 0; i < raw.size(); ++i) {
      lines.push_back((i == 0 ? marker : padding) + raw[i]);
    }
  }

  return Result<std::vector<std::string>>(std::move(lines));
}

Result<std::string> Renderer::RenderGroup(
    const Group& group, const RenderContext& context,
    std::vector<std::pair<BlockId, Version>>& blocksUsed) const {
  auto lines = RenderGroupLines(group, context, blocksUsed);
  if (lines.HasError()) {
    return Result<std::string>(lines.error());
  }
  std::ostringstream result;
  for (size_t i = 0; i < lines.value().size(); ++i) {
    if (i > 0) {
      result << "\n";
    }
    result << lines.value()[i];
  }
  return Result<std::string>(result.str());
}

Result<std::string> Renderer::RenderBlockElement(
    const BlockElement& element, const RenderContext& context,
    std::vector<std::pair<BlockId, Version>>& blocksUsed) const {
  std::ostringstream innerStream;
  for (const auto& fragment : element.content) {
    auto rendered = RenderFragment(fragment, context, blocksUsed);
    if (rendered.HasError()) {
      return rendered;
    }
    innerStream << rendered.value();
  }
  const std::string inner = innerStream.str();

  switch (element.kind) {
    case BlockElementKind::Heading: {
      // validate() guarantees ParsedHeadingLevel() succeeds for any
      // published Composition; Render() loads from storage rather than
      // validating again, so the fallback to level 1 below is defensive,
      // not a redundant check (same posture as ResolveConditionals's
      // elseContent check below it).
      const int level = element.ParsedHeadingLevel().value_or(1);
      return Result<std::string>(std::string(level, '#') + " " + inner);
    }
    case BlockElementKind::Quote: {
      std::istringstream innerLines(inner);
      std::ostringstream quoted;
      std::string line;
      bool first = true;
      while (std::getline(innerLines, line)) {
        if (!first) {
          quoted << "\n";
        }
        first = false;
        quoted << "> " << line;
      }
      return Result<std::string>(quoted.str());
    }
    case BlockElementKind::CodeBlock: {
      return Result<std::string>("```" + element.attr + "\n" + inner + "\n```");
    }
  }
  return Result<std::string>(
      Error{ErrorCode::InvalidParamType, "Unknown BlockElementKind"});
}

StructuralStyle Renderer::GetEffectiveStyle(const Composition& composition) {
  if (composition.GetStyleProfile().has_value()) {
    return composition.GetStyleProfile()->structural;
  }
  return {};
}

std::vector<Fragment> Renderer::ResolveConditionals(
    const std::vector<Fragment>& fragments, const Params& params) {
  std::vector<Fragment> resolved;
  resolved.reserve(fragments.size());

  for (const auto& fragment : fragments) {
    if (fragment.IsGroup()) {
      const Group& group = fragment.AsGroup();
      Group resolvedGroup{.kind = group.kind};
      resolvedGroup.items.reserve(group.items.size());
      for (const auto& item : group.items) {
        resolvedGroup.items.push_back(ResolveConditionals(item, params));
      }
      resolved.push_back(Fragment::MakeGroup(std::move(resolvedGroup)));
      continue;
    }

    if (fragment.IsBlockElement()) {
      const BlockElement& elem = fragment.AsBlockElement();
      resolved.push_back(Fragment::MakeBlockElement(BlockElement{
          .kind = elem.kind, .attr = elem.attr,
          .content = ResolveConditionals(elem.content, params)}));
      continue;
    }

    if (!fragment.IsConditional()) {
      resolved.push_back(fragment);
      continue;
    }

    const Conditional& cond = fragment.AsConditional();
    const std::vector<Fragment>* selected = nullptr;

    for (const auto& branch : cond.branches) {
      bool allMatch = true;
      for (const auto& condition : branch.conditions) {
        if (!condition.matches(params)) {
          allMatch = false;
          break;
        }
      }
      if (allMatch) {
        selected = &branch.content;
        break;
      }
    }

    if (selected == nullptr) {
      // validate() guarantees elseContent is set for any published
      // Composition, but Render() loads its input from storage rather
      // than validating the in-memory object again -- so this is a
      // defensive check, not a redundant one. Degrade to "renders
      // nothing" rather than dereferencing a possibly-disengaged
      // optional; it can't mask a real authoring bug, since validate()
      // already rejects this shape before anything can be published.
      if (!cond.elseContent.has_value()) {
        continue;
      }
      selected = &(*cond.elseContent);
    }

    auto branchResolved = ResolveConditionals(*selected, params);
    resolved.insert(resolved.end(),
                    std::make_move_iterator(branchResolved.begin()),
                    std::make_move_iterator(branchResolved.end()));
  }

  return resolved;
}

}  // namespace tf
