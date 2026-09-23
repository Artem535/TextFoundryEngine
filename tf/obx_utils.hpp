//
// Created by AI Assistant on 30.01.2026.
// Utilities for converting between ObjectBox entities and domain objects
//

#pragma once

#include <optional>
#include <rfl/json.hpp>
#include <unordered_set>
#include <vector>

#include "block.h"
#include "block_ref.h"
#include "block_type.hpp"
#include "composition.h"
#include "database_scheme.obx.hpp"
#include "fragment.h"
#include "version.h"

namespace tf::utils {
struct CompositionStoragePayload {
  std::optional<StyleProfile> style_profile;
  std::optional<std::string> revision_comment;
};

// ============================================================================
// Enums Conversion
// ============================================================================

/**
 * Convert ObxBlock type (int8_t) to BlockType enum
 */
inline BlockType ObxTypeToBlockType(int8_t type) {
  // BlockType enum:
  // Role=0, System=1, Mission=2, Safety=3, Constraint=4, Style=5, Domain=6, Meta=7
  switch (type) {
    case 0:
      return BlockType::Role;
    case 1:
      return BlockType::System;
    case 2:
      return BlockType::Mission;
    case 3:
      return BlockType::Safety;
    case 4:
      return BlockType::Constraint;
    case 5:
      return BlockType::Style;
    case 6:
      return BlockType::Domain;
    case 7:
      return BlockType::Meta;
    default:
      return BlockType::Domain;  // default fallback
  }
}

/**
 * Convert ObxBlock type (int8_t) to BlockType string representation
 */
inline std::string_view ObxTypeToBlockTypeString(int8_t type) {
  return BlockTypeToString(ObxTypeToBlockType(type));
}

/**
 * Convert BlockType enum to ObxBlock type (int8_t)
 */
inline int8_t BlockTypeToObxType(BlockType type) {
  switch (type) {
    case BlockType::Role:
      return 0;
    case BlockType::System:
      return 1;
    case BlockType::Mission:
      return 2;
    case BlockType::Safety:
      return 3;
    case BlockType::Constraint:
      return 4;
    case BlockType::Style:
      return 5;
    case BlockType::Domain:
      return 6;
    case BlockType::Meta:
      return 7;
  }
  return 6;  // Domain as default
}

/**
 * Convert ObxState code (int8_t) to BlockState enum
 * ObxState codes: Draft=0, Published=1, Deprecated=2
 */
inline BlockState ObxStateCodeToBlockState(int8_t code) {
  switch (code) {
    case 0:
      return BlockState::Draft;
    case 1:
      return BlockState::Published;
    case 2:
      return BlockState::Deprecated;
    default:
      return BlockState::Draft;
  }
}

/**
 * Convert BlockState enum to ObxState code (int8_t)
 */
inline int8_t BlockStateToObxStateCode(BlockState state) {
  switch (state) {
    case BlockState::Draft:
      return 0;
    case BlockState::Published:
      return 1;
    case BlockState::Deprecated:
      return 2;
  }
  return 0;  // Draft as default
}

/**
 * Convert ObxFragment separator type (int8_t) to SeparatorType enum
 * 0=None, 1=Newline, 2=Paragraph, 3=Hr
 */
inline SeparatorType ObxSeparatorTypeToSeparatorType(int8_t type) {
  switch (type) {
    case 1:
      return SeparatorType::Newline;
    case 2:
      return SeparatorType::Paragraph;
    case 3:
      return SeparatorType::Hr;
    default:
      return SeparatorType::Newline;  // default fallback
  }
}

/**
 * Convert SeparatorType enum to ObxFragment separator type (int8_t)
 */
inline int8_t SeparatorTypeToObxSeparatorType(SeparatorType type) {
  switch (type) {
    case SeparatorType::Newline:
      return 1;
    case SeparatorType::Paragraph:
      return 2;
    case SeparatorType::Hr:
      return 3;
  }
  return 1;  // Newline as default
}

/**
 * Convert ObxFragment fragment type (int8_t) to FragmentType enum
 * 0=BlockRef, 1=StaticText, 2=Separator, 3=Conditional
 */
inline FragmentType ObxFragmentTypeToFragmentType(int8_t type) {
  switch (type) {
    case 0:
      return FragmentType::BlockRef;
    case 1:
      return FragmentType::StaticText;
    case 2:
      return FragmentType::Separator;
    case 3:
      return FragmentType::Conditional;
    default:
      return FragmentType::StaticText;
  }
}

/**
 * Convert FragmentType enum to ObxFragment fragment type (int8_t)
 */
inline int8_t FragmentTypeToObxFragmentType(FragmentType type) {
  switch (type) {
    case FragmentType::BlockRef:
      return 0;
    case FragmentType::StaticText:
      return 1;
    case FragmentType::Separator:
      return 2;
    case FragmentType::Conditional:
      return 3;
  }
  return 1;  // StaticText as default
}

// ============================================================================
// Version Conversion
// ============================================================================

/**
 * Create Version from major/minor components
 */
inline Version ObxVersionToVersion(uint16_t major, uint16_t minor) {
  return Version{major, minor};
}

/**
 * Extract major version component from Version
 */
inline uint16_t VersionToMajor(const Version& version) { return version.major; }

/**
 * Extract minor version component from Version
 */
inline uint16_t VersionToMinor(const Version& version) { return version.minor; }

// ============================================================================
// Block Conversion
// ============================================================================

/**
 * Convert ObxBlock to domain Block
 * Note: This creates a basic Block. Tags and ParamSchema require additional
 * queries.
 */
inline Block ObxBlockToBlock(const ObxBlock& obxBlock) {
  Block block;
  block.SetId(obxBlock.blockId);
  block.SetVersion(Version{obxBlock.versionMajor, obxBlock.versionMinor});
  block.SetType(ObxTypeToBlockType(obxBlock.type));
  block.SetState(ObxStateCodeToBlockState(obxBlock.state));
  block.SetTemplate(Template(obxBlock.templateContent));
  block.SetLanguage(obxBlock.language);
  if (!obxBlock.tagsJson.empty()) {
    auto tags = rfl::json::read<std::vector<std::string>>(obxBlock.tagsJson)
                    .value_or(std::vector<std::string>{});
    std::unordered_set<std::string> tags_set(tags.begin(), tags.end());
    block.SetTags(std::move(tags_set));
  }
  block.SetDescription(obxBlock.description);
  block.SetRevisionComment(obxBlock.revisionComment);

  if (!obxBlock.defaultsJson.empty()) {
    block.SetDefaults(rfl::json::read<Params>(obxBlock.defaultsJson).value());
  }

  if (!obxBlock.paramsJson.empty()) {
    block.SetParamSchema(
        rfl::json::read<std::vector<ParamSchema>>(obxBlock.paramsJson).value());
  }

  return block;
}

/**
 * Convert domain Block to ObxBlock for storage
 * Note: Sets all fields except relations (projectId, languageId,
 * previousVersionId, nextVersionId) which must be set separately based on
 * context.
 */
inline ObxBlock BlockToObxBlock(const Block& block, const obx_id id = 0) {
  ObxBlock obxBlock;
  obxBlock.id = id;
  obxBlock.blockId = block.Id();
  obxBlock.versionMajor = block.version().major;
  obxBlock.versionMinor = block.version().minor;
  obxBlock.type = BlockTypeToObxType(block.type());
  obxBlock.state = BlockStateToObxStateCode(block.state());
  obxBlock.templateContent = block.templ().Content();
  obxBlock.defaultsJson = rfl::json::write(block.defaults());
  obxBlock.language = block.language();
  std::vector<std::string> tags_vec(block.tags().begin(), block.tags().end());
  obxBlock.tagsJson = rfl::json::write(tags_vec);
  obxBlock.description = block.description();
  obxBlock.revisionComment = block.revision_comment();
  // Note: createdAt, updatedAt should be set by storage layer
  return obxBlock;
}

// ============================================================================
// Composition Conversion
// ============================================================================

/**
 * Convert ObxComposition to domain Composition
 * Note: Fragments must be loaded and converted separately.
 */
inline Composition obx_composition_to_composition(
    const ObxComposition& obxComp) {
  Composition comp;
  comp.SetId(obxComp.compositionId);
  comp.SetProjectKey(obxComp.projectKey);
  comp.SetVersion(Version{obxComp.versionMajor, obxComp.versionMinor});
  comp.SetDescription(obxComp.description);
  comp.SetRevisionComment(obxComp.revisionComment);
  comp.SetState(ObxStateCodeToBlockState(obxComp.state));

  if (!obxComp.styleProfileJson.empty()) {
    const auto style = rfl::json::read<StyleProfile>(obxComp.styleProfileJson);
    if (style.has_value()) {
      comp.SetStyleProfile(std::move(*style));
    } else {
      const auto payload =
          rfl::json::read<CompositionStoragePayload>(obxComp.styleProfileJson);
      if (payload.has_value()) {
        if (payload->style_profile.has_value()) {
          comp.SetStyleProfile(std::move(*payload->style_profile));
        }
        if (comp.revision_comment().empty() &&
            payload->revision_comment.has_value()) {
          comp.SetRevisionComment(std::move(*payload->revision_comment));
        }
      }
    }
  }

  return comp;
}

/**
 * Convert domain Composition to ObxComposition for storage
 * Note: Sets basic fields. Relations (projectId, targetLanguageId,
 * previousVersionId, nextVersionId) must be set separately.
 */
inline ObxComposition composition_to_obx_composition(const Composition& comp,
                                                     obx_id id = 0) {
  ObxComposition obxComp;
  obxComp.id = id;
  obxComp.compositionId = comp.id();
  obxComp.projectKey = comp.ProjectKey();
  obxComp.versionMajor = comp.version().major;
  obxComp.versionMinor = comp.version().minor;
  obxComp.description = comp.description();
  obxComp.revisionComment = comp.revision_comment();
  if (comp.GetStyleProfile().has_value()) {
    obxComp.styleProfileJson = rfl::json::write(comp.GetStyleProfile().value());
  }
  return obxComp;
}

// ============================================================================
// Conditional JSON Encoding
//
// Unlike BlockRef/StaticText/Separator, a Conditional fragment holds a
// nested Fragment tree (Branch::content, Conditional::elseContent). Fragment
// cannot be reflected directly by reflect-cpp (it has a private
// std::variant member), so this encodes/decodes the tree by hand through
// rfl::Generic, a dynamic JSON value type, storing the result as a single
// JSON string in ObxFragment::conditionalJson.
// ============================================================================

inline rfl::Generic FragmentToGeneric(const Fragment& fragment);
inline Fragment GenericToFragment(const rfl::Generic& generic);

inline rfl::Generic ConditionToGeneric(const Condition& condition) {
  rfl::Generic::Object obj;
  obj["attribute"] = condition.attribute;
  rfl::Generic::Array allowedValues;
  for (const auto& value : condition.allowedValues) {
    allowedValues.emplace_back(rfl::Generic(value));
  }
  obj["allowedValues"] = allowedValues;
  obj["negate"] = condition.negate;
  return obj;
}

inline Condition GenericToCondition(const rfl::Generic& generic) {
  Condition condition;
  auto obj = generic.to_object().value();
  condition.attribute = obj.get("attribute").value().to_string().value();
  // Materialize into a named Array before looping: obj.get(...).value() is a
  // reference into a temporary rfl::Result, and to_array().value() is again a
  // reference into a second temporary Result wrapping the array. Iterating
  // the chain directly as a range-for's range-expression leaves the loop's
  // hidden `auto&& __range` bound to a subobject of a temporary that is
  // destroyed at the end of that statement (reference lifetime extension
  // does not propagate through a function call returning a reference) --
  // real UB that read as fine under GCC 15 locally but reliably corrupted
  // this exact data in CI under GCC 13 (see engine.cc /
  // NormalizeComposition and cli_dto_test.cc's DTO round-trip crashing with
  // std::bad_array_new_length / std::bad_alloc deep inside GenericToBranch).
  const auto allowedValues = obj.get("allowedValues").value().to_array().value();
  for (const auto& v : allowedValues) {
    condition.allowedValues.insert(v.to_string().value());
  }
  condition.negate = obj.get("negate").value().to_bool().value();
  return condition;
}

inline rfl::Generic BranchToGeneric(const Branch& branch) {
  rfl::Generic::Object obj;
  rfl::Generic::Array conditions;
  for (const auto& condition : branch.conditions) {
    conditions.emplace_back(ConditionToGeneric(condition));
  }
  obj["conditions"] = conditions;
  rfl::Generic::Array content;
  for (const auto& fragment : branch.content) {
    content.emplace_back(FragmentToGeneric(fragment));
  }
  obj["content"] = content;
  return obj;
}

inline Branch GenericToBranch(const rfl::Generic& generic) {
  Branch branch;
  auto obj = generic.to_object().value();
  // See the comment in GenericToCondition: materialize before looping.
  const auto conditions = obj.get("conditions").value().to_array().value();
  for (const auto& c : conditions) {
    branch.conditions.push_back(GenericToCondition(c));
  }
  const auto content = obj.get("content").value().to_array().value();
  for (const auto& f : content) {
    branch.content.push_back(GenericToFragment(f));
  }
  return branch;
}

inline rfl::Generic ConditionalToGeneric(const Conditional& cond) {
  rfl::Generic::Object obj;
  rfl::Generic::Array branches;
  for (const auto& branch : cond.branches) {
    branches.emplace_back(BranchToGeneric(branch));
  }
  obj["branches"] = branches;
  if (cond.elseContent.has_value()) {
    rfl::Generic::Array elseContent;
    for (const auto& fragment : *cond.elseContent) {
      elseContent.emplace_back(FragmentToGeneric(fragment));
    }
    obj["elseContent"] = elseContent;
  } else {
    obj["elseContent"] = rfl::Generic::Null;
  }
  return obj;
}

inline Conditional GenericToConditional(const rfl::Generic& generic) {
  Conditional cond;
  auto obj = generic.to_object().value();
  // See the comment in GenericToCondition: materialize before looping.
  const auto branches = obj.get("branches").value().to_array().value();
  for (const auto& b : branches) {
    cond.branches.push_back(GenericToBranch(b));
  }
  auto elseGeneric = obj.get("elseContent").value();
  if (!elseGeneric.is_null()) {
    std::vector<Fragment> elseContent;
    const auto elseArray = elseGeneric.to_array().value();
    for (const auto& f : elseArray) {
      elseContent.push_back(GenericToFragment(f));
    }
    cond.elseContent = std::move(elseContent);
  }
  return cond;
}

inline rfl::Generic FragmentToGeneric(const Fragment& fragment) {
  rfl::Generic::Object obj;
  switch (fragment.type()) {
    case FragmentType::BlockRef: {
      const BlockRef& ref = fragment.AsBlockRef();
      obj["type"] = std::string("block_ref");
      obj["blockId"] = ref.GetBlockId();
      obj["useLatest"] = ref.UseLatest();
      if (ref.version().has_value()) {
        obj["versionMajor"] = static_cast<int64_t>(ref.version()->major);
        obj["versionMinor"] = static_cast<int64_t>(ref.version()->minor);
      } else {
        obj["versionMajor"] = static_cast<int64_t>(0);
        obj["versionMinor"] = static_cast<int64_t>(0);
      }
      obj["localParamsJson"] = rfl::json::write(ref.LocalParams());
      break;
    }
    case FragmentType::StaticText: {
      obj["type"] = std::string("static_text");
      obj["text"] = fragment.AsStaticText().text();
      break;
    }
    case FragmentType::Separator: {
      obj["type"] = std::string("separator");
      obj["separatorType"] = static_cast<int64_t>(
          SeparatorTypeToObxSeparatorType(fragment.AsSeparator().type));
      break;
    }
    case FragmentType::Conditional: {
      obj["type"] = std::string("conditional");
      obj["conditional"] = ConditionalToGeneric(fragment.AsConditional());
      break;
    }
  }
  return obj;
}

inline Fragment GenericToFragment(const rfl::Generic& generic) {
  auto obj = generic.to_object().value();
  std::string type = obj.get("type").value().to_string().value();

  if (type == "block_ref") {
    BlockRef ref;
    ref.SetBlockId(obj.get("blockId").value().to_string().value());
    bool useLatest = obj.get("useLatest").value().to_bool().value();
    ref.SetUseLatest(useLatest);
    if (!useLatest) {
      auto major = obj.get("versionMajor").value().to_int().value();
      auto minor = obj.get("versionMinor").value().to_int().value();
      ref.SetVersion(Version{static_cast<uint16_t>(major),
                             static_cast<uint16_t>(minor)});
    }
    auto localParamsJson = obj.get("localParamsJson").value().to_string().value();
    if (!localParamsJson.empty()) {
      ref.SetLocalParams(rfl::json::read<Params>(localParamsJson).value());
    }
    return Fragment::MakeBlockRef(std::move(ref));
  }

  if (type == "static_text") {
    return Fragment::MakeStaticText(obj.get("text").value().to_string().value());
  }

  if (type == "separator") {
    auto sepTypeInt = obj.get("separatorType").value().to_int().value();
    return Fragment::MakeSeparator(
        ObxSeparatorTypeToSeparatorType(static_cast<int8_t>(sepTypeInt)));
  }

  if (type == "conditional") {
    return Fragment::MakeConditional(
        GenericToConditional(obj.get("conditional").value()));
  }

  // Fallback, matches ObxFragmentToFragment's own unknown-type fallback.
  return Fragment::MakeStaticText("");
}

inline std::string ConditionalToJson(const Conditional& cond) {
  return rfl::json::write(ConditionalToGeneric(cond));
}

inline Conditional JsonToConditional(const std::string& json) {
  return GenericToConditional(rfl::json::read<rfl::Generic>(json).value());
}

// ============================================================================
// Fragment Conversion
// ============================================================================

/**
 * Convert ObxFragment to domain Fragment
 */
inline Fragment ObxFragmentToFragment(const ObxFragment& obxFrag) {
  FragmentType fragType = ObxFragmentTypeToFragmentType(obxFrag.fragmentType);

  switch (fragType) {
    case FragmentType::BlockRef: {
      BlockRef ref;
      ref.SetBlockId(obxFrag.refBlockId);
      if (!obxFrag.refUseLatest) {
        ref.SetVersion(
            Version{obxFrag.refVersionMajor, obxFrag.refVersionMinor});
      }
      ref.SetUseLatest(obxFrag.refUseLatest);
      if (!obxFrag.refLocalParamsJson.empty()) {
        ref.SetLocalParams(
            rfl::json::read<Params>(obxFrag.refLocalParamsJson).value());
      }
      return Fragment::MakeBlockRef(std::move(ref));
    }

    case FragmentType::StaticText: {
      return Fragment::MakeStaticText(obxFrag.staticContent);
    }

    case FragmentType::Separator: {
      SeparatorType sepType =
          ObxSeparatorTypeToSeparatorType(obxFrag.separatorType);
      return Fragment::MakeSeparator(sepType);
    }

    case FragmentType::Conditional: {
      return Fragment::MakeConditional(
          JsonToConditional(obxFrag.conditionalJson));
    }
  }

  // Fallback
  return Fragment::MakeStaticText("");
}

/**
 * Convert domain Fragment to ObxFragment for storage
 * Note: compositionId must be set separately
 */
inline ObxFragment fragment_to_obx_fragment(const Fragment& fragment,
                                            uint32_t orderIndex,
                                            obx_id id = 0) {
  ObxFragment obxFrag;
  obxFrag.id = id;
  obxFrag.orderIndex = orderIndex;
  obxFrag.fragmentType = FragmentTypeToObxFragmentType(fragment.type());

  switch (fragment.type()) {
    case FragmentType::BlockRef: {
      const BlockRef& ref = fragment.AsBlockRef();
      obxFrag.refBlockId = ref.GetBlockId();
      obxFrag.refUseLatest = ref.UseLatest();
      if (ref.version().has_value()) {
        obxFrag.refVersionMajor = ref.version().value().major;
        obxFrag.refVersionMinor = ref.version().value().minor;
      } else {
        obxFrag.refVersionMajor = 0;
        obxFrag.refVersionMinor = 0;
      }
      // Note: localParams should be serialized to refLocalParamsJson
      obxFrag.refLocalParamsJson = rfl::json::write(ref.LocalParams());
      break;
    }

    case FragmentType::StaticText: {
      obxFrag.staticContent = fragment.AsStaticText().text();
      break;
    }

    case FragmentType::Separator: {
      obxFrag.separatorType =
          SeparatorTypeToObxSeparatorType(fragment.AsSeparator().type);
      break;
    }

    case FragmentType::Conditional: {
      obxFrag.conditionalJson = ConditionalToJson(fragment.AsConditional());
      break;
    }
  }

  return obxFrag;
}

// ============================================================================
// Project Conversion
// ============================================================================

/**
 * Convert ObxProject project entity
 * Note: Returns project key for use in domain objects
 */
inline std::string ObxProjectToProjectKey(const ObxProject& obxProject) {
  return obxProject.key;
}

/**
 * Create ObxProject for storage
 */
inline ObxProject project_key_to_obx_project(
    const std::string& key, const std::string& name = "",
    const std::string& description = "") {
  ObxProject obxProject;
  obxProject.key = key;
  obxProject.name = name.empty() ? key : name;
  obxProject.description = description;
  // Note: createdAt, updatedAt should be set by storage layer
  return obxProject;
}

// ============================================================================
// Language Conversion
// ============================================================================

/**
 * Convert ObxLanguage to language code string
 */
inline std::string ObxLanguageToCode(const ObxLanguage& obxLang) {
  return obxLang.code;
}

/**
 * Create ObxLanguage for storage
 */
inline ObxLanguage code_to_obx_language(const std::string& code,
                                        const std::string& nativeName = "") {
  ObxLanguage obxLang;
  obxLang.code = code;
  obxLang.nativeName = nativeName.empty() ? code : nativeName;
  return obxLang;
}

// ============================================================================
// Tag Conversion
// ============================================================================

/**
 * Convert ObxTag to tag name string
 */
inline std::string ObxTagToTagName(const ObxTag& obxTag) { return obxTag.name; }

/**
 * Create ObxTag for storage
 * Note: projectId must be set separately
 */
inline ObxTag TagNameToObxTag(const std::string& name) {
  ObxTag obxTag;
  obxTag.name = name;
  // Note: createdAt should be set by storage layer
  return obxTag;
}

/**
 * Convert vector of ObxTag to unordered_set of tag names
 */
inline std::unordered_set<std::string> obx_tags_to_tag_set(
    const std::vector<ObxTag>& obxTags) {
  std::unordered_set<std::string> tags;
  for (const auto& obxTag : obxTags) {
    tags.insert(obxTag.name);
  }
  return tags;
}

// ============================================================================
// Audit Log Conversion (optional)
// ============================================================================

/**
 * Create ObxAuditLog entry
 */
inline ObxAuditLog create_obx_audit_log(const std::string& entityType,
                                        uint64_t entityId,
                                        const std::string& operation,
                                        const std::string& userId = "",
                                        const std::string& detailsJson = "") {
  ObxAuditLog log;
  log.entityType = entityType;
  log.entityId = entityId;
  log.operation = operation;
  log.userId = userId;
  log.detailsJson = detailsJson;
  // Note: createdAt should be set by storage layer
  return log;
}
}  // namespace tf::utils
