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
