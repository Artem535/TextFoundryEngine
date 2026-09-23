//
// Created by a.durynin on 29.01.2026.
//

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "../tf/logger.h"

#include "../tf/block.h"
#include "../tf/block_generation.h"
#include "../tf/block_ref.h"
#include "../tf/block_type.hpp"
#include "../tf/composition.h"
#include "../tf/engine.h"
#include "../tf/error.h"
#include "../tf/fragment.h"
#include "../tf/renderer.h"
#include "../tf/version.h"

using namespace tf;

// ==================== Test Fixture with Real Engine ====================

class EngineTestFixture {
 public:
  Engine engine;
  static int testIdCounter;
  int myTestId;

  EngineTestFixture() : myTestId(testIdCounter++) {
    // Use unique in-memory database for each test instance
    EngineConfig config;
    config.default_data_path = "memory:test_" + std::to_string(myTestId);
    engine = Engine(config);
  }

  // Helper to create and publish a block
  Block createAndPublishBlock(const BlockId& id, const std::string& templateStr,
                              const Params& defaults = {}) {
    BlockDraft draft = BlockDraftBuilder(id)
                           .WithTemplate(Template(templateStr))
                           .WithDefaults(defaults)
                           .build();
    auto pubResult =
        engine.PublishBlock(std::move(draft), Engine::VersionBump::Minor);
    REQUIRE(pubResult.HasValue());
    auto pub = pubResult.value();
    REQUIRE(pub.id() == id);
    auto blockResult = engine.LoadBlock(id, pub.version());
    REQUIRE(blockResult.HasValue());
    Block block = blockResult.value();
    REQUIRE(block.state() == BlockState::Published);
    return block;
  }

  // Helper to create and publish a composition
  Composition createAndPublishComposition(
      const CompositionId& id,
      const std::vector<std::pair<std::string, Version>>& blockRefs) {
    CompositionDraftBuilder builder(id);
    for (const auto& [blockId, ver] : blockRefs) {
      builder.AddBlockRef(blockId, ver.major, ver.minor);
    }
    CompositionDraft draft = builder.build();
    auto pubResult =
        engine.PublishComposition(std::move(draft), Engine::VersionBump::Minor);
    REQUIRE(pubResult.HasValue());
    auto pub = pubResult.value();
    REQUIRE(pub.id() == id);
    auto compResult = engine.LoadComposition(id, pub.version());
    REQUIRE(compResult.HasValue());
    Composition comp = compResult.value();
    REQUIRE(comp.state() == BlockState::Published);
    return comp;
  }
};

int EngineTestFixture::testIdCounter = 0;

namespace {

class FakeBlockGenerator final : public IBlockGenerator {
 public:
  explicit FakeBlockGenerator(Result<GeneratedBlockData> result)
      : result_(std::move(result)) {}
  explicit FakeBlockGenerator(Result<GeneratedBlockBatch> batch_result)
      : batch_result_(std::move(batch_result)) {}

  [[nodiscard]] Result<GeneratedBlockData> GenerateBlock(
      const BlockGenerationRequest&) const override {
    if (!result_.has_value()) {
      return Result<GeneratedBlockData>(
          Error{ErrorCode::StorageError, "Fake block generator has no single result"});
    }
    if (result_->HasError()) {
      return Result<GeneratedBlockData>(result_->error());
    }
    return Result<GeneratedBlockData>(result_->value());
  }

  [[nodiscard]] Result<GeneratedBlockBatch> GenerateBlocks(
      const PromptSlicingRequest&) const override {
    if (!batch_result_.has_value()) {
      return Result<GeneratedBlockBatch>(
          Error{ErrorCode::StorageError, "Fake block generator has no batch result"});
    }
    if (batch_result_->HasError()) {
      return Result<GeneratedBlockBatch>(batch_result_->error());
    }
    return Result<GeneratedBlockBatch>(batch_result_->value());
  }

 private:
  std::optional<Result<GeneratedBlockData>> result_;
  std::optional<Result<GeneratedBlockBatch>> batch_result_;
};

class FakeBlockNormalizer final : public IBlockNormalizer {
 public:
  explicit FakeBlockNormalizer(Result<NormalizedBlockData> result,
                               std::string fingerprint = "fake-block-normalizer")
      : result_(std::move(result)), fingerprint_(std::move(fingerprint)) {}

  [[nodiscard]] Result<NormalizedBlockData> NormalizeBlock(
      const BlockNormalizationRequest&) const override {
    ++call_count_;
    if (result_.HasError()) {
      return Result<NormalizedBlockData>(result_.error());
    }
    return Result<NormalizedBlockData>(result_.value());
  }

  [[nodiscard]] std::string Fingerprint() const override { return fingerprint_; }

  /**
   * Number of times NormalizeBlock has been called. Lets a test assert
   * that a cache-reuse path made zero additional LLM-backed calls.
   */
  [[nodiscard]] int call_count() const noexcept { return call_count_; }

 private:
  Result<NormalizedBlockData> result_;
  std::string fingerprint_;
  mutable int call_count_ = 0;
};

}  // namespace

// ==================== Version Tests ====================

TEST_SUITE("Version") {
  TEST_CASE("default version is 0.0") {
    Version v;
    CHECK(v.major == 0);
    CHECK(v.minor == 0);
  }

  TEST_CASE("version comparison") {
    Version v1{1, 0};
    Version v2{1, 5};
    Version v3{2, 0};

    CHECK(v1 < v2);
    CHECK(v2 < v3);
    CHECK(v1 < v3);
    CHECK(v1 == Version{1, 0});
    CHECK(v1 != v2);
  }

  TEST_CASE("version toString") {
    Version v{2, 5};
    CHECK(v.ToString() == "2.5");
  }
}

// ==================== Error Tests ====================

TEST_SUITE("Error") {
  TEST_CASE("success error has no error") {
    auto err = Error::success();
    CHECK(err.is_success());
    CHECK_FALSE(err.is_error());
  }

  TEST_CASE("missing param error") {
    auto err = Error::MissingParam("name");
    CHECK(err.is_error());
    CHECK(err.code == ErrorCode::MissingParam);
    CHECK(err.message.find("name") != std::string::npos);
  }

  TEST_CASE("version required error") {
    auto err = Error::VersionRequired();
    CHECK(err.is_error());
    CHECK(err.code == ErrorCode::VersionRequired);
  }
}

// ==================== Result Tests ====================

TEST_SUITE("Result") {
  TEST_CASE("Result with value") {
    Result<int> r(42);
    CHECK(r.HasValue());
    CHECK_FALSE(r.HasError());
    CHECK(*r == 42);
    CHECK(r.value() == 42);
  }

  TEST_CASE("Result with error") {
    Result<int> r(Error::MissingParam("test"));
    CHECK_FALSE(r.HasValue());
    CHECK(r.HasError());
    CHECK(r.error().code == ErrorCode::MissingParam);
  }
}

TEST_SUITE("BlockGeneration") {
  TEST_CASE("engine generates block draft through configured generator") {
    EngineTestFixture fixture;

    GeneratedBlockData data{
        .id = "role.reviewer",
        .type = BlockType::Role,
        .language = "en",
        .description = "Review instructions",
        .templ = "Review {{subject}} for {{focus}}.",
        .defaults = {{"focus", "correctness"}},
        .tags = {"review", "quality", "review"},
    };
    fixture.engine.SetBlockGenerator(
        std::make_shared<FakeBlockGenerator>(Result<GeneratedBlockData>(data)));

    BlockGenerationRequest request{
        .prompt = "Create a code review block",
        .preferred_type = BlockType::Role,
    };

    auto result = fixture.engine.GenerateBlockDraft(request);
    REQUIRE(result.HasValue());

    auto publish_result =
        fixture.engine.PublishBlock(std::move(result).value(),
                                    Engine::VersionBump::Minor);
    REQUIRE(publish_result.HasValue());

    auto block = fixture.engine.LoadBlock("role.reviewer");
    REQUIRE(block.HasValue());
    CHECK(block.value().type() == BlockType::Role);
    CHECK(block.value().templ().Content() == "Review {{subject}} for {{focus}}.");
    CHECK(block.value().defaults().at("focus") == "correctness");
    CHECK(block.value().tags().contains("review"));
    CHECK(block.value().tags().contains("quality"));
  }

  TEST_CASE("engine returns error when block generator is missing") {
    EngineTestFixture fixture;

    auto result = fixture.engine.GenerateBlockDraft(
        BlockGenerationRequest{.prompt = "Create a role block"});
    REQUIRE(result.HasError());
    CHECK(result.error().code == ErrorCode::StorageError);
  }

  TEST_CASE("engine rejects duplicate generated block id") {
    EngineTestFixture fixture;
    fixture.createAndPublishBlock("role.reviewer", "Existing {{subject}}");

    GeneratedBlockData data{
        .id = "role.reviewer",
        .type = BlockType::Role,
        .language = "en",
        .description = "Review instructions",
        .templ = "Review {{subject}}.",
    };
    fixture.engine.SetBlockGenerator(
        std::make_shared<FakeBlockGenerator>(Result<GeneratedBlockData>(data)));

    auto result = fixture.engine.GenerateBlockDraft(
        BlockGenerationRequest{.prompt = "Create a role block"});
    REQUIRE(result.HasError());
    CHECK(result.error().code == ErrorCode::DuplicateId);
  }

  TEST_CASE("engine generates multiple block drafts from prompt slicing") {
    EngineTestFixture fixture;

    GeneratedBlockBatch batch{
        .blocks =
            {
                GeneratedBlockData{
                    .id = "team.role.system",
                    .type = BlockType::Role,
                    .language = "en",
                    .description = "System role",
                    .templ = "You are {{assistant_name}}.",
                },
                GeneratedBlockData{
                    .id = "team.constraint.style",
                    .type = BlockType::Constraint,
                    .language = "en",
                    .description = "Style constraints",
                    .templ = "Be concise and structured.",
                },
            },
    };
    fixture.engine.SetBlockGenerator(
        std::make_shared<FakeBlockGenerator>(Result<GeneratedBlockBatch>(batch)));

    auto result = fixture.engine.GenerateBlockDrafts(
        PromptSlicingRequest{.source_text = "Long prompt to decompose"});
    REQUIRE(result.HasValue());
    CHECK(result.value().size() == 2);
  }
}

TEST_SUITE("CompositionNormalization") {
  TEST_CASE("engine creates normalized composition with rewritten block refs") {
    EngineTestFixture fixture;

    auto published = fixture.engine.PublishBlock(
        BlockDraftBuilder("prompt.system")
            .WithType(BlockType::System)
            .WithTemplate(Template("<SYSTEM>\nYou are {{name}}.\n</SYSTEM>"))
            .WithDefault("name", "Jane")
            .build(),
        Version{1, 0});
    REQUIRE(published.HasValue());

    CompositionDraftBuilder composition_builder("prompt.base");
    composition_builder.AddBlockRef(published.value());
    auto composition =
        fixture.engine.PublishComposition(composition_builder.build(),
                                         Version{1, 0});
    REQUIRE(composition.HasValue());

    fixture.engine.SetBlockNormalizer(std::make_shared<FakeBlockNormalizer>(
        Result<NormalizedBlockData>(NormalizedBlockData{
            .templ = "<SYSTEM>\nYou are {{name}}, a thoughtful assistant.\n</SYSTEM>",
            .description = std::nullopt,
            .language = std::nullopt,
        })));

    auto result = fixture.engine.NormalizeComposition(
        CompositionNormalizationRequest{
            .source_composition_id = "prompt.base",
            .style = SemanticStyle{.tone = std::string("warm")},
        });
    REQUIRE(result.HasValue());
    CHECK(result.value().composition_id == "norm.prompt.base");

    auto normalized_comp =
        fixture.engine.LoadComposition(result.value().composition_id);
    REQUIRE(normalized_comp.HasValue());
    REQUIRE(normalized_comp.value().fragmentCount() == 1);
    REQUIRE(normalized_comp.value().fragment(0).IsBlockRef());

    const auto& new_ref = normalized_comp.value().fragment(0).AsBlockRef();
    auto normalized_block =
        fixture.engine.LoadBlock(new_ref.GetBlockId(), *new_ref.version());
    REQUIRE(normalized_block.HasValue());
    CHECK(normalized_block.value().templ().Content().find("{{name}}") !=
          std::string::npos);
  }

  TEST_CASE("engine rejects normalized block when placeholders change") {
    EngineTestFixture fixture;

    auto published = fixture.engine.PublishBlock(
        BlockDraftBuilder("prompt.system")
            .WithType(BlockType::System)
            .WithTemplate(Template("You are {{name}}."))
            .build(),
        Version{1, 0});
    REQUIRE(published.HasValue());

    CompositionDraftBuilder composition_builder("prompt.base");
    composition_builder.AddBlockRef(published.value());
    auto composition =
        fixture.engine.PublishComposition(composition_builder.build(),
                                         Version{1, 0});
    REQUIRE(composition.HasValue());

    fixture.engine.SetBlockNormalizer(std::make_shared<FakeBlockNormalizer>(
        Result<NormalizedBlockData>(NormalizedBlockData{
            .templ = "You are helpful.",
            .description = std::nullopt,
            .language = std::nullopt,
        })));

    auto result = fixture.engine.NormalizeComposition(
        CompositionNormalizationRequest{
            .source_composition_id = "prompt.base",
            .style = SemanticStyle{.tone = std::string("warm")},
        });
    REQUIRE(result.HasError());
    CHECK(result.error().code == ErrorCode::InvalidParamType);
  }

  TEST_CASE(
      "NormalizeComposition recurses into Conditional branches and "
      "elseContent, rewriting nested BlockRefs and leaving nested "
      "StaticText in place (normalize_static_text defaults to false)") {
    EngineTestFixture fixture;

    Block expert_block =
        fixture.createAndPublishBlock("role.expert", "You are an expert.");
    Block beginner_block =
        fixture.createAndPublishBlock("role.beginner", "You are a beginner guide.");

    auto cond =
        ConditionalBuilder()
            .If(Condition{.attribute = "level", .allowedValues = {"none"}})
            .Then(Fragment::MakeStaticText("no particular expertise"))
            .If(Condition{.attribute = "level", .allowedValues = {"expert"}})
            .Then(Fragment::MakeBlockRef(
                BlockRef("role.expert", expert_block.version())))
            .Else(Fragment::MakeBlockRef(
                BlockRef("role.beginner", beginner_block.version())))
            .build();

    CompositionDraftBuilder composition_builder("prompt.cond");
    composition_builder.AddConditional(std::move(cond));
    auto composition = fixture.engine.PublishComposition(
        composition_builder.build(), Version{1, 0});
    REQUIRE(composition.HasValue());

    fixture.engine.SetBlockNormalizer(std::make_shared<FakeBlockNormalizer>(
        Result<NormalizedBlockData>(NormalizedBlockData{
            .templ = "normalized role text",
            .description = std::nullopt,
            .language = std::nullopt,
        })));

    auto result = fixture.engine.NormalizeComposition(
        CompositionNormalizationRequest{
            .source_composition_id = "prompt.cond",
            .style = SemanticStyle{.tone = std::string("warm")},
        });
    REQUIRE(result.HasValue());

    auto normalized_comp =
        fixture.engine.LoadComposition(result.value().composition_id);
    REQUIRE(normalized_comp.HasValue());
    REQUIRE(normalized_comp.value().fragmentCount() == 1);
    REQUIRE(normalized_comp.value().fragment(0).IsConditional());

    const Conditional& normalized_cond =
        normalized_comp.value().fragment(0).AsConditional();
    REQUIRE(normalized_cond.branches.size() == 2);

    REQUIRE(normalized_cond.branches[0].content.size() == 1);
    REQUIRE(normalized_cond.branches[0].content[0].IsStaticText());
    CHECK(normalized_cond.branches[0].content[0].AsStaticText().text() ==
          "no particular expertise");

    REQUIRE(normalized_cond.branches[1].content.size() == 1);
    REQUIRE(normalized_cond.branches[1].content[0].IsBlockRef());
    const auto& normalized_expert_ref =
        normalized_cond.branches[1].content[0].AsBlockRef();
    CHECK(normalized_expert_ref.GetBlockId() == "norm.role.expert");
    auto normalized_expert_block = fixture.engine.LoadBlock(
        normalized_expert_ref.GetBlockId(), *normalized_expert_ref.version());
    REQUIRE(normalized_expert_block.HasValue());
    CHECK(normalized_expert_block.value().templ().Content() ==
          "normalized role text");

    REQUIRE(normalized_cond.elseContent.has_value());
    REQUIRE(normalized_cond.elseContent->size() == 1);
    REQUIRE((*normalized_cond.elseContent)[0].IsBlockRef());
    CHECK((*normalized_cond.elseContent)[0].AsBlockRef().GetBlockId() ==
          "norm.role.beginner");

    REQUIRE(result.value().rewritten_blocks.size() == 2);
    bool has_expert_rewrite = false;
    bool has_beginner_rewrite = false;
    for (const auto& [from, to] : result.value().rewritten_blocks) {
      if (from == "role.expert" && to == "norm.role.expert") {
        has_expert_rewrite = true;
      }
      if (from == "role.beginner" && to == "norm.role.beginner") {
        has_beginner_rewrite = true;
      }
    }
    CHECK(has_expert_rewrite);
    CHECK(has_beginner_rewrite);
  }

  TEST_CASE(
      "NormalizeComposition recurses through a Conditional nested inside "
      "another Conditional's branch (two levels)") {
    EngineTestFixture fixture;

    auto inner_cond =
        ConditionalBuilder()
            .If(Condition{.attribute = "tone", .allowedValues = {"formal"}})
            .Then(Fragment::MakeStaticText("formal inner text"))
            .Else(Fragment::MakeStaticText("casual inner text"))
            .build();

    auto outer_cond =
        ConditionalBuilder()
            .If(Condition{.attribute = "level", .allowedValues = {"expert"}})
            .Then(Fragment::MakeConditional(std::move(inner_cond)))
            .Else(Fragment::MakeStaticText("outer else text"))
            .build();

    CompositionDraftBuilder composition_builder("prompt.nested_cond");
    composition_builder.AddConditional(std::move(outer_cond));
    auto composition = fixture.engine.PublishComposition(
        composition_builder.build(), Version{1, 0});
    REQUIRE(composition.HasValue());

    // No BlockRef anywhere in this tree, but NormalizeComposition still
    // requires a block normalizer to be configured before it does anything.
    fixture.engine.SetBlockNormalizer(std::make_shared<FakeBlockNormalizer>(
        Result<NormalizedBlockData>(NormalizedBlockData{
            .templ = "unused",
            .description = std::nullopt,
            .language = std::nullopt,
        })));

    auto result = fixture.engine.NormalizeComposition(
        CompositionNormalizationRequest{
            .source_composition_id = "prompt.nested_cond",
            .style = SemanticStyle{.tone = std::string("warm")},
        });
    REQUIRE(result.HasValue());

    auto normalized_comp =
        fixture.engine.LoadComposition(result.value().composition_id);
    REQUIRE(normalized_comp.HasValue());
    REQUIRE(normalized_comp.value().fragmentCount() == 1);
    REQUIRE(normalized_comp.value().fragment(0).IsConditional());

    const Conditional& outer = normalized_comp.value().fragment(0).AsConditional();
    REQUIRE(outer.branches.size() == 1);
    REQUIRE(outer.branches[0].content.size() == 1);
    REQUIRE(outer.branches[0].content[0].IsConditional());

    const Conditional& inner = outer.branches[0].content[0].AsConditional();
    REQUIRE(inner.branches.size() == 1);
    REQUIRE(inner.branches[0].content.size() == 1);
    CHECK(inner.branches[0].content[0].AsStaticText().text() ==
          "formal inner text");
    REQUIRE(inner.elseContent.has_value());
    REQUIRE(inner.elseContent->size() == 1);
    CHECK((*inner.elseContent)[0].AsStaticText().text() == "casual inner text");

    REQUIRE(outer.elseContent.has_value());
    REQUIRE(outer.elseContent->size() == 1);
    CHECK((*outer.elseContent)[0].AsStaticText().text() == "outer else text");
  }

  TEST_CASE(
      "a Conditional composition normalizes, publishes, and renders "
      "different branches for different RenderContexts") {
    EngineTestFixture fixture;

    auto cond =
        ConditionalBuilder()
            .If(Condition{.attribute = "level", .allowedValues = {"expert"}})
            .Then(Fragment::MakeStaticText("expert guidance"))
            .Else(Fragment::MakeStaticText("beginner guidance"))
            .build();

    CompositionDraftBuilder composition_builder("prompt.cond_render");
    composition_builder.AddConditional(std::move(cond));
    auto composition = fixture.engine.PublishComposition(
        composition_builder.build(), Version{1, 0});
    REQUIRE(composition.HasValue());

    fixture.engine.SetBlockNormalizer(std::make_shared<FakeBlockNormalizer>(
        Result<NormalizedBlockData>(NormalizedBlockData{
            .templ = "unused",
            .description = std::nullopt,
            .language = std::nullopt,
        })));

    auto normalize_result = fixture.engine.NormalizeComposition(
        CompositionNormalizationRequest{
            .source_composition_id = "prompt.cond_render",
            .style = SemanticStyle{.tone = std::string("warm")},
        });
    REQUIRE(normalize_result.HasValue());

    auto expert_render = fixture.engine.Render(
        normalize_result.value().composition_id,
        RenderContext{}.WithParam("level", "expert"));
    REQUIRE(expert_render.HasValue());
    CHECK(expert_render.value().text == "expert guidance");

    auto fallback_render = fixture.engine.Render(
        normalize_result.value().composition_id,
        RenderContext{}.WithParam("level", "novice"));
    REQUIRE(fallback_render.HasValue());
    CHECK(fallback_render.value().text == "beginner guidance");

    CHECK(expert_render.value().text != fallback_render.value().text);
  }

  TEST_CASE(
      "PreviewNormalizeComposition renders a Conditional as a labeled "
      "if/elif/else block") {
    EngineTestFixture fixture;

    auto cond =
        ConditionalBuilder()
            .If(Condition{.attribute = "language",
                          .allowedValues = {"ru", "en"}})
            .Then(Fragment::MakeStaticText("hello"))
            .If(Condition{.attribute = "tone",
                          .allowedValues = {"formal"},
                          .negate = true})
            .Then(Fragment::MakeStaticText("casual text"))
            .Else(Fragment::MakeStaticText("fallback"))
            .build();

    CompositionDraftBuilder composition_builder("prompt.cond_preview");
    composition_builder.AddConditional(std::move(cond));
    auto composition = fixture.engine.PublishComposition(
        composition_builder.build(), Version{1, 0});
    REQUIRE(composition.HasValue());

    fixture.engine.SetBlockNormalizer(std::make_shared<FakeBlockNormalizer>(
        Result<NormalizedBlockData>(NormalizedBlockData{
            .templ = "unused",
            .description = std::nullopt,
            .language = std::nullopt,
        })));

    auto preview = fixture.engine.PreviewNormalizeComposition(
        CompositionNormalizationRequest{
            .source_composition_id = "prompt.cond_preview",
            .style = SemanticStyle{.tone = std::string("warm")},
        });
    REQUIRE(preview.HasValue());

    const std::string& text = preview.value().preview_text;
    // allowedValues is an unordered_set, so a two-value set's rendered
    // order isn't guaranteed -- accept either.
    CHECK((text.find("[if language in {ru, en}]") != std::string::npos ||
          text.find("[if language in {en, ru}]") != std::string::npos));
    CHECK(text.find("hello") != std::string::npos);
    CHECK(text.find("[elif tone not in {formal}]") != std::string::npos);
    CHECK(text.find("casual text") != std::string::npos);
    CHECK(text.find("[else]") != std::string::npos);
    CHECK(text.find("fallback") != std::string::npos);
    CHECK(text.find("hello") < text.find("casual text"));
    CHECK(text.find("casual text") < text.find("fallback"));
  }

  TEST_CASE(
      "PreviewNormalizeComposition's reuse_cached_blocks fast path shows "
      "Conditional branches without calling the block normalizer again") {
    EngineTestFixture fixture;

    Block expert_block =
        fixture.createAndPublishBlock("role.expert", "Expert guide.");

    auto cond =
        ConditionalBuilder()
            .If(Condition{.attribute = "level", .allowedValues = {"expert"}})
            .Then(Fragment::MakeBlockRef(
                BlockRef("role.expert", expert_block.version())))
            .Else(Fragment::MakeStaticText("fallback text"))
            .build();

    CompositionDraftBuilder composition_builder("prompt.cond_cache");
    composition_builder.AddConditional(std::move(cond));
    auto composition = fixture.engine.PublishComposition(
        composition_builder.build(), Version{1, 0});
    REQUIRE(composition.HasValue());

    auto fake_normalizer = std::make_shared<FakeBlockNormalizer>(
        Result<NormalizedBlockData>(NormalizedBlockData{
            .templ = "Normalized expert guide.",
            .description = std::nullopt,
            .language = std::nullopt,
        }));
    fixture.engine.SetBlockNormalizer(fake_normalizer);

    CompositionNormalizationRequest request{
        .source_composition_id = "prompt.cond_cache",
        .style = SemanticStyle{.tone = std::string("warm")},
        .reuse_cached_blocks = true,
    };

    // First preview: no derived block and no derivative composition exist
    // yet, so this goes through the fresh path and calls the normalizer
    // exactly once (for the one BlockRef in the tree) to compute the text --
    // but does NOT publish a Block to storage (Preview is not allowed to
    // have persistent side effects; see the "does not publish" test below).
    auto first_preview = fixture.engine.PreviewNormalizeComposition(request);
    REQUIRE(first_preview.HasValue());
    CHECK(fake_normalizer->call_count() == 1);
    CHECK(first_preview.value().preview_text.find("Normalized expert guide.") !=
          std::string::npos);
    CHECK(first_preview.value().preview_text.find("fallback text") !=
          std::string::npos);
    CHECK(first_preview.value().preview_text.find("[if level in {expert}]") !=
          std::string::npos);
    CHECK(first_preview.value().preview_text.find("[else]") != std::string::npos);

    // Because the preview above didn't persist anything, NormalizeComposition
    // (apply) finds no pre-tagged cached block and must call the normalizer
    // again itself -- this is what actually publishes "norm.role.expert" for
    // the first time and tags it. Bringing call_count to 2 here (not 1) is
    // the point: it proves the first preview left no exploitable cache
    // behind.
    auto normalize_result = fixture.engine.NormalizeComposition(request);
    REQUIRE(normalize_result.HasValue());
    CHECK(fake_normalizer->call_count() == 2);

    // Second preview: the derivative composition now exists (published by
    // the apply call above) with a matching style, so this takes the
    // reuse_cached_blocks fast path -- reading straight from the stored,
    // already-normalized Conditional via FragmentTreeToPreviewText, which
    // never touches NormalizeFragments or the block normalizer at all.
    // call_count stays at 2, unchanged by this call.
    auto second_preview = fixture.engine.PreviewNormalizeComposition(request);
    REQUIRE(second_preview.HasValue());
    CHECK(second_preview.value().preview_text.find("Normalized expert guide.") !=
          std::string::npos);
    CHECK(second_preview.value().preview_text.find("fallback text") !=
          std::string::npos);
    CHECK(second_preview.value().preview_text.find("[if level in {expert}]") !=
          std::string::npos);
    CHECK(second_preview.value().preview_text.find("[else]") != std::string::npos);
    CHECK(fake_normalizer->call_count() == 2);
  }

  TEST_CASE(
      "PreviewNormalizeComposition's fresh path computes normalized text "
      "without publishing a new Block to storage") {
    EngineTestFixture fixture;

    Block expert_block =
        fixture.createAndPublishBlock("role.expert", "You are an expert.");

    CompositionDraftBuilder composition_builder("prompt.preview_no_persist");
    composition_builder.AddBlockRef(
        BlockRef("role.expert", expert_block.version()));
    auto composition = fixture.engine.PublishComposition(
        composition_builder.build(), Version{1, 0});
    REQUIRE(composition.HasValue());

    auto fake_normalizer = std::make_shared<FakeBlockNormalizer>(
        Result<NormalizedBlockData>(NormalizedBlockData{
            .templ = "normalized expert text",
            .description = std::nullopt,
            .language = std::nullopt,
        }));
    fixture.engine.SetBlockNormalizer(fake_normalizer);

    CompositionNormalizationRequest request{
        .source_composition_id = "prompt.preview_no_persist",
        .style = SemanticStyle{.tone = std::string("warm")},
    };

    auto preview = fixture.engine.PreviewNormalizeComposition(request);
    REQUIRE(preview.HasValue());
    CHECK(preview.value().preview_text == "normalized expert text");
    CHECK(fake_normalizer->call_count() == 1);

    // The real proof preview didn't persist: a real apply call right after
    // still has to call the normalizer itself, since there's no pre-tagged
    // cached block for it to reuse. If preview had silently published one
    // (the bug this test guards against), call_count would stay at 1 here.
    auto apply_result = fixture.engine.NormalizeComposition(request);
    REQUIRE(apply_result.HasValue());
    CHECK(fake_normalizer->call_count() == 2);
  }
}

// ==================== BlockType Tests ====================

TEST_SUITE("BlockType") {
  TEST_CASE("BlockType to string") {
    CHECK(BlockTypeToString(BlockType::Role) == "role");
    CHECK(BlockTypeToString(BlockType::System) == "system");
    CHECK(BlockTypeToString(BlockType::Mission) == "mission");
    CHECK(BlockTypeToString(BlockType::Safety) == "safety");
    CHECK(BlockTypeToString(BlockType::Constraint) == "constraint");
    CHECK(BlockTypeToString(BlockType::Style) == "style");
    CHECK(BlockTypeToString(BlockType::Domain) == "domain");
    CHECK(BlockTypeToString(BlockType::Meta) == "meta");
  }

  TEST_CASE("BlockType from string") {
    CHECK(BlockTypeFromString("role") == BlockType::Role);
    CHECK(BlockTypeFromString("system") == BlockType::System);
    CHECK(BlockTypeFromString("mission") == BlockType::Mission);
    CHECK(BlockTypeFromString("safety") == BlockType::Safety);
    CHECK(BlockTypeFromString("constraint") == BlockType::Constraint);
    CHECK(BlockTypeFromString("style") == BlockType::Style);
    CHECK(BlockTypeFromString("domain") == BlockType::Domain);
    CHECK(BlockTypeFromString("meta") == BlockType::Meta);
  }

  TEST_CASE("BlockType from invalid string throws") {
    CHECK_THROWS_AS(BlockTypeFromString("invalid"), std::invalid_argument);
  }
}

// ==================== Template Tests ====================

TEST_SUITE("Template") {
  TEST_CASE("extract param names") {
    Template t("Hello, {{name}}! Your score is {{score}}.");
    auto params = t.ExtractParamNames();
    CHECK(params.size() == 2);
    CHECK(params[0] == "name");
    CHECK(params[1] == "score");
  }

  TEST_CASE("expand template with all params") {
    Template t("Hello, {{name}}!");
    Params p{{"name", "World"}};
    auto result = t.Expand(p);
    CHECK(result.HasValue());
    CHECK(result.value() == "Hello, World!");
  }

  TEST_CASE("expand template with missing param returns error") {
    Template t("Hello, {{name}}!");
    Params p{};
    auto result = t.Expand(p);
    CHECK(result.HasError());
    CHECK(result.error().code == ErrorCode::MissingParam);
  }

  TEST_CASE("expand template with no params") {
    Template t("Hello, World!");
    Params p{};
    auto result = t.Expand(p);
    CHECK(result.HasValue());
    CHECK(result.value() == "Hello, World!");
  }
}

// ==================== Block Tests ====================

TEST_SUITE("Block") {
  TEST_CASE("create block with id") {
    Block b("greeting.hello");
    CHECK(b.Id() == "greeting.hello");
    CHECK(b.state() == BlockState::Draft);
    CHECK(b.type() == BlockType::Domain);
  }

  TEST_CASE("block defaults and resolution") {
    Block b("test.block");
    b.SetDefaults({{"name", "default_user"}});

    Params local{{"name", "local_user"}};
    Params runtime{{"name", "runtime_user"}};

    // Runtime has highest priority
    auto r1 = b.ResolveParam("name", local, runtime);
    CHECK(r1.HasValue());
    CHECK(r1.value() == "runtime_user");

    // Local has priority over defaults
    Params emptyRuntime;
    auto r2 = b.ResolveParam("name", local, emptyRuntime);
    CHECK(r2.HasValue());
    CHECK(r2.value() == "local_user");

    // Defaults used when no overrides
    Params emptyLocal;
    auto r3 = b.ResolveParam("name", emptyLocal, emptyRuntime);
    CHECK(r3.HasValue());
    CHECK(r3.value() == "default_user");
  }

  TEST_CASE("block validate params") {
    Block b("test.block");
    b.SetTemplate(Template("Hello, {{name}}!"));

    // Missing param
    auto err = b.ValidateParams({}, {});
    CHECK(err.is_error());
    CHECK(err.code == ErrorCode::MissingParam);

    // With default
    b.SetDefaults({{"name", "user"}});
    err = b.ValidateParams({}, {});
    CHECK(err.is_success());
  }

  TEST_CASE("block publish") {
    Block b("test.block");
    b.SetTemplate(Template("Hello!"));

    CHECK(b.state() == BlockState::Draft);

    auto err = b.publish(Version{1, 0});
    CHECK(err.is_success());
    CHECK(b.state() == BlockState::Published);
    CHECK(b.version() == Version{1, 0});
  }

  TEST_CASE("cannot publish without template") {
    Block b("test.block");
    auto err = b.publish(Version{1, 0});
    CHECK(err.is_error());
  }

  TEST_CASE("cannot publish already published block") {
    Block b("test.block");
    b.SetTemplate(Template("Hello!"));
    auto err = b.publish(Version{1, 0});
    CHECK(err.is_success());

    err = b.publish(Version{2, 0});
    CHECK(err.is_error());
  }

  TEST_CASE("block deprecate") {
    Block b("test.block");
    b.SetTemplate(Template("Hello!"));
    auto err = b.publish(Version{1, 0});
    CHECK(err.is_success());

    b.deprecate();
    CHECK(b.state() == BlockState::Deprecated);
  }
}

// ==================== BlockRef Tests ====================

TEST_SUITE("BlockRef") {
  TEST_CASE("BlockRef with version") {
    BlockRef ref("block.id", Version{1, 5});
    CHECK(ref.GetBlockId() == "block.id");
    CHECK(ref.version().has_value());
    CHECK(ref.version().value() == Version{1, 5});
    CHECK_FALSE(ref.UseLatest());
  }

  TEST_CASE("BlockRef with UseLatest") {
    BlockRef ref("block.id");
    CHECK(ref.GetBlockId() == "block.id");
    CHECK_FALSE(ref.version().has_value());
    CHECK(ref.UseLatest());
  }

  TEST_CASE("BlockRef validate in draft context allows UseLatest") {
    BlockRef ref("block.id");       // UseLatest = true
    auto err = ref.validate(true);  // isDraftContext = true
    CHECK(err.is_success());
  }

  TEST_CASE("BlockRef validate in non-draft context rejects UseLatest") {
    BlockRef ref("block.id");        // UseLatest = true
    auto err = ref.validate(false);  // isDraftContext = false
    CHECK(err.is_error());
    CHECK(err.code == ErrorCode::VersionRequired);
  }

  TEST_CASE("BlockRef with version validates in non-draft context") {
    BlockRef ref("block.id", Version{1, 0});
    auto err = ref.validate(false);
    CHECK(err.is_success());
  }

  TEST_CASE("BlockRef resolve params") {
    Block block("test.block");
    block.SetTemplate(Template("{{greeting}}, {{name}}!"));
    block.SetDefaults({{"greeting", "Hello"}});

    BlockRef ref("test.block", Version{1, 0}, {{"name", "World"}});

    Params runtime{{"greeting", "Hi"}};  // Runtime overrides block defaults
    auto result = ref.ResolveParams(block, runtime);

    CHECK(result.HasValue());
    CHECK(result.value()["greeting"] == "Hi");  // Runtime wins
    CHECK(result.value()["name"] == "World");   // Local override used
  }
}

// ==================== Fragment Tests ====================

TEST_SUITE("Condition") {
  TEST_CASE("matches when value is in allowedValues") {
    Condition c{.attribute = "audience", .allowedValues = {"expert", "advanced"}};
    CHECK(c.matches({{"audience", "expert"}}));
  }

  TEST_CASE("does not match when value is not in allowedValues") {
    Condition c{.attribute = "audience", .allowedValues = {"expert", "advanced"}};
    CHECK_FALSE(c.matches({{"audience", "beginner"}}));
  }

  TEST_CASE("negate inverts a matching value") {
    Condition c{.attribute = "audience", .allowedValues = {"expert"}, .negate = true};
    CHECK_FALSE(c.matches({{"audience", "expert"}}));
  }

  TEST_CASE("negate inverts a non-matching value") {
    Condition c{.attribute = "audience", .allowedValues = {"expert"}, .negate = true};
    CHECK(c.matches({{"audience", "beginner"}}));
  }

  TEST_CASE("missing attribute never matches, even with negate") {
    Condition c{.attribute = "audience", .allowedValues = {"expert"}};
    CHECK_FALSE(c.matches({}));

    Condition negated{.attribute = "audience", .allowedValues = {"expert"}, .negate = true};
    CHECK_FALSE(negated.matches({}));
  }
}

TEST_SUITE("Conditional") {
  TEST_CASE("valid conditional passes validation") {
    Conditional cond;
    cond.branches.push_back(Branch{
        .conditions = {Condition{.attribute = "audience", .allowedValues = {"expert"}}},
        .content = {Fragment::MakeStaticText("expert text")}});
    cond.elseContent = std::vector<Fragment>{Fragment::MakeStaticText("default text")};

    CHECK(cond.validate(false).is_success());
  }

  TEST_CASE("empty branches is an error") {
    Conditional cond;
    cond.elseContent = std::vector<Fragment>{};

    auto err = cond.validate(false);
    CHECK(err.is_error());
    CHECK(err.code == ErrorCode::EmptyConditional);
  }

  TEST_CASE("a branch with no conditions is an error") {
    Conditional cond;
    cond.branches.push_back(Branch{.conditions = {}, .content = {}});
    cond.elseContent = std::vector<Fragment>{};

    auto err = cond.validate(false);
    CHECK(err.is_error());
    CHECK(err.code == ErrorCode::EmptyBranchConditions);
  }

  TEST_CASE("missing else is an error") {
    Conditional cond;
    cond.branches.push_back(Branch{
        .conditions = {Condition{.attribute = "x", .allowedValues = {"y"}}},
        .content = {}});
    // elseContent left as std::nullopt

    auto err = cond.validate(false);
    CHECK(err.is_error());
    CHECK(err.code == ErrorCode::MissingElseBranch);
  }

  TEST_CASE("an explicitly empty else vector is valid") {
    Conditional cond;
    cond.branches.push_back(Branch{
        .conditions = {Condition{.attribute = "x", .allowedValues = {"y"}}},
        .content = {}});
    cond.elseContent = std::vector<Fragment>{};  // deliberately empty, not nullopt

    CHECK(cond.validate(false).is_success());
  }

  TEST_CASE("Fragment::validate propagates a bad Conditional's error") {
    Conditional cond;
    cond.elseContent = std::vector<Fragment>{};  // no branches -> EmptyConditional

    Fragment f = Fragment::MakeConditional(std::move(cond));
    auto err = f.validate(false);
    CHECK(err.is_error());
    CHECK(err.code == ErrorCode::EmptyConditional);
  }

  TEST_CASE("Composition::validate propagates a nested Conditional's error") {
    Composition comp("test.conditional.invalid");
    Conditional cond;
    cond.elseContent = std::vector<Fragment>{};  // no branches -> EmptyConditional
    comp.InsertFragment(0, Fragment::MakeConditional(std::move(cond)));

    auto err = comp.validate();
    CHECK(err.is_error());
    CHECK(err.code == ErrorCode::EmptyConditional);
  }
}

TEST_SUITE("ConditionalRendering") {
  TEST_CASE_FIXTURE(EngineTestFixture, "first matching branch wins among 3+ branches") {
    createAndPublishBlock("cond.expert", "expert content");
    createAndPublishBlock("cond.intermediate", "intermediate content");
    createAndPublishBlock("cond.beginner", "beginner content");

    // Built via ConditionalBuilder (Task 3), not aggregate-initialized, to
    // prove the full path connects: builder -> AddConditional -> publish
    // (which persists through ObjectBox storage) -> Render.
    auto cond =
        ConditionalBuilder()
            .If(Condition{.attribute = "level", .allowedValues = {"expert"}})
            .Then(Fragment::MakeBlockRef(BlockRef("cond.expert", Version{1, 0})))
            .If(Condition{.attribute = "level", .allowedValues = {"intermediate"}})
            .Then(Fragment::MakeBlockRef(
                BlockRef("cond.intermediate", Version{1, 0})))
            .Else(Fragment::MakeBlockRef(BlockRef("cond.beginner", Version{1, 0})))
            .build();

    CompositionDraftBuilder builder("cond.elif_chain");
    builder.AddConditional(std::move(cond));
    auto pubResult = engine.PublishComposition(builder.build(), Engine::VersionBump::Minor);
    REQUIRE(pubResult.HasValue());

    auto ctx = RenderContext{}.WithParam("level", "intermediate");
    auto result = engine.Render("cond.elif_chain", ctx);
    REQUIRE(result.HasValue());
    CHECK(result.value().text == "intermediate content");
  }

  TEST_CASE_FIXTURE(EngineTestFixture, "falls through to else when no branch matches") {
    createAndPublishBlock("cond.a", "a content");

    CompositionDraftBuilder builder("cond.fallthrough");
    Conditional cond;
    cond.branches.push_back(Branch{
        .conditions = {Condition{.attribute = "level", .allowedValues = {"expert"}}},
        .content = {Fragment::MakeBlockRef(BlockRef("cond.a", Version{1, 0}))}});
    cond.elseContent = std::vector<Fragment>{Fragment::MakeStaticText("default text")};
    builder.AddConditional(std::move(cond));
    auto pubResult = engine.PublishComposition(builder.build(), Engine::VersionBump::Minor);
    REQUIRE(pubResult.HasValue());

    auto ctx = RenderContext{}.WithParam("level", "unknown");
    auto result = engine.Render("cond.fallthrough", ctx);
    REQUIRE(result.HasValue());
    CHECK(result.value().text == "default text");
  }

  TEST_CASE_FIXTURE(EngineTestFixture, "a branch with multiple conditions requires all to match (AND)") {
    CompositionDraftBuilder builder("cond.and_branch");
    Conditional cond;
    cond.branches.push_back(Branch{
        .conditions = {Condition{.attribute = "level", .allowedValues = {"expert"}},
                       Condition{.attribute = "platform", .allowedValues = {"linux"}}},
        .content = {Fragment::MakeStaticText("expert linux text")}});
    cond.elseContent = std::vector<Fragment>{Fragment::MakeStaticText("default text")};
    builder.AddConditional(std::move(cond));
    auto pubResult = engine.PublishComposition(builder.build(), Engine::VersionBump::Minor);
    REQUIRE(pubResult.HasValue());

    // Only one of the two conditions matches -> branch does not match -> else.
    auto ctx = RenderContext{}.WithParam("level", "expert").WithParam("platform", "windows");
    auto result = engine.Render("cond.and_branch", ctx);
    REQUIRE(result.HasValue());
    CHECK(result.value().text == "default text");

    // Both conditions match -> branch matches.
    auto ctx2 = RenderContext{}.WithParam("level", "expert").WithParam("platform", "linux");
    auto result2 = engine.Render("cond.and_branch", ctx2);
    REQUIRE(result2.HasValue());
    CHECK(result2.value().text == "expert linux text");
  }

  TEST_CASE_FIXTURE(EngineTestFixture, "a single condition with multiple allowedValues matches any one (OR)") {
    CompositionDraftBuilder builder("cond.or_values");
    Conditional cond;
    cond.branches.push_back(Branch{
        .conditions = {Condition{.attribute = "platform",
                                 .allowedValues = {"linux", "macos"}}},
        .content = {Fragment::MakeStaticText("unix-like text")}});
    cond.elseContent = std::vector<Fragment>{Fragment::MakeStaticText("other text")};
    builder.AddConditional(std::move(cond));
    auto pubResult = engine.PublishComposition(builder.build(), Engine::VersionBump::Minor);
    REQUIRE(pubResult.HasValue());

    auto ctx = RenderContext{}.WithParam("platform", "macos");
    auto result = engine.Render("cond.or_values", ctx);
    REQUIRE(result.HasValue());
    CHECK(result.value().text == "unix-like text");
  }

  TEST_CASE_FIXTURE(EngineTestFixture, "a Conditional nested inside a selected branch's content is itself resolved") {
    CompositionDraftBuilder builder("cond.nested");
    Conditional inner;
    inner.branches.push_back(Branch{
        .conditions = {Condition{.attribute = "detail", .allowedValues = {"high"}}},
        .content = {Fragment::MakeStaticText("high detail")}});
    inner.elseContent = std::vector<Fragment>{Fragment::MakeStaticText("low detail")};

    Conditional outer;
    outer.branches.push_back(Branch{
        .conditions = {Condition{.attribute = "level", .allowedValues = {"expert"}}},
        .content = {Fragment::MakeConditional(std::move(inner))}});
    outer.elseContent = std::vector<Fragment>{Fragment::MakeStaticText("beginner text")};
    builder.AddConditional(std::move(outer));
    auto pubResult = engine.PublishComposition(builder.build(), Engine::VersionBump::Minor);
    REQUIRE(pubResult.HasValue());

    auto ctx = RenderContext{}.WithParam("level", "expert").WithParam("detail", "high");
    auto result = engine.Render("cond.nested", ctx);
    REQUIRE(result.HasValue());
    CHECK(result.value().text == "high detail");
  }

  TEST_CASE_FIXTURE(EngineTestFixture, "attribute missing from RenderContext falls through to else") {
    CompositionDraftBuilder builder("cond.missing_attr");
    Conditional cond;
    cond.branches.push_back(Branch{
        .conditions = {Condition{.attribute = "level", .allowedValues = {"expert"}}},
        .content = {Fragment::MakeStaticText("expert text")}});
    cond.elseContent = std::vector<Fragment>{Fragment::MakeStaticText("default text")};
    builder.AddConditional(std::move(cond));
    auto pubResult = engine.PublishComposition(builder.build(), Engine::VersionBump::Minor);
    REQUIRE(pubResult.HasValue());

    // No "level" param set at all.
    auto result = engine.Render("cond.missing_attr");
    REQUIRE(result.HasValue());
    CHECK(result.value().text == "default text");
  }

  TEST_CASE_FIXTURE(EngineTestFixture,
                    "explicitly empty elseContent survives an ObjectBox "
                    "store+load round trip as has_value()==true, not nullopt") {
    CompositionDraftBuilder builder("cond.empty_else_roundtrip");
    Conditional cond;
    cond.branches.push_back(Branch{
        .conditions = {Condition{.attribute = "level", .allowedValues = {"expert"}}},
        .content = {Fragment::MakeStaticText("expert text")}});
    cond.elseContent = std::vector<Fragment>{};  // deliberately empty, not nullopt
    builder.AddConditional(std::move(cond));
    auto pubResult = engine.PublishComposition(builder.build(), Engine::VersionBump::Minor);
    REQUIRE(pubResult.HasValue());

    // Reload from storage directly (not via Render, which would produce
    // the same empty-text output whether elseContent is an empty vector or
    // nullopt -- this must inspect the reloaded structure itself to prove
    // the distinction actually round-trips through JsonToConditional).
    auto loaded = engine.LoadComposition("cond.empty_else_roundtrip");
    REQUIRE(loaded.HasValue());
    REQUIRE(loaded.value().fragments().size() == 1);
    REQUIRE(loaded.value().fragments()[0].IsConditional());
    const Conditional& loadedCond = loaded.value().fragments()[0].AsConditional();
    REQUIRE(loadedCond.elseContent.has_value());
    CHECK(loadedCond.elseContent->empty());

    // A non-matching context still renders successfully (empty else content).
    auto ctx = RenderContext{}.WithParam("level", "unknown");
    auto result = engine.Render("cond.empty_else_roundtrip", ctx);
    REQUIRE(result.HasValue());
    CHECK(result.value().text.empty());
  }
}

TEST_SUITE("ConditionalBuilder") {
  TEST_CASE("If/Then/If/Then/Else builds the expected structure") {
    auto cond = ConditionalBuilder()
                    .If(Condition{.attribute = "level", .allowedValues = {"expert"}})
                    .Then(Fragment::MakeStaticText("expert"))
                    .If(Condition{.attribute = "level", .allowedValues = {"intermediate"}})
                    .Then(Fragment::MakeStaticText("intermediate"))
                    .Else(Fragment::MakeStaticText("beginner"))
                    .build();

    REQUIRE(cond.branches.size() == 2);
    CHECK(cond.branches[0].conditions.size() == 1);
    CHECK(cond.branches[0].conditions[0].attribute == "level");
    REQUIRE(cond.branches[0].content.size() == 1);
    CHECK(cond.branches[0].content[0].AsStaticText().text() == "expert");
    REQUIRE(cond.branches[1].content.size() == 1);
    CHECK(cond.branches[1].content[0].AsStaticText().text() == "intermediate");
    REQUIRE(cond.elseContent.has_value());
    REQUIRE(cond.elseContent->size() == 1);
    CHECK((*cond.elseContent)[0].AsStaticText().text() == "beginner");
  }

  TEST_CASE("And adds an additional condition to the current branch") {
    auto cond = ConditionalBuilder()
                    .If(Condition{.attribute = "level", .allowedValues = {"expert"}})
                    .And(Condition{.attribute = "platform", .allowedValues = {"linux"}})
                    .Then(Fragment::MakeStaticText("expert linux"))
                    .Else(Fragment::MakeStaticText("default"))
                    .build();

    REQUIRE(cond.branches.size() == 1);
    REQUIRE(cond.branches[0].conditions.size() == 2);
    CHECK(cond.branches[0].conditions[0].attribute == "level");
    CHECK(cond.branches[0].conditions[1].attribute == "platform");
  }

  TEST_CASE("repeated Then calls accumulate content in the current branch") {
    auto cond = ConditionalBuilder()
                    .If(Condition{.attribute = "level", .allowedValues = {"expert"}})
                    .Then(Fragment::MakeStaticText("part1"))
                    .Then(Fragment::MakeStaticText("part2"))
                    .Else(Fragment::MakeStaticText("default"))
                    .build();

    REQUIRE(cond.branches[0].content.size() == 2);
    CHECK(cond.branches[0].content[0].AsStaticText().text() == "part1");
    CHECK(cond.branches[0].content[1].AsStaticText().text() == "part2");
  }

  TEST_CASE("Else does not require a prior If") {
    auto cond = ConditionalBuilder()
                    .Else(Fragment::MakeStaticText("default"))
                    .build();

    CHECK(cond.branches.empty());
    REQUIRE(cond.elseContent.has_value());
    REQUIRE(cond.elseContent->size() == 1);
    CHECK((*cond.elseContent)[0].AsStaticText().text() == "default");
  }

  TEST_CASE("Then before any If throws EngineException") {
    CHECK_THROWS_AS(ConditionalBuilder().Then(Fragment::MakeStaticText("x")),
                    EngineException);
  }

  TEST_CASE("And before any If throws EngineException") {
    CHECK_THROWS_AS(
        ConditionalBuilder().And(Condition{.attribute = "x", .allowedValues = {"y"}}),
        EngineException);
  }
}

TEST_SUITE("Fragment") {
  TEST_CASE("Fragment BlockRef type") {
    BlockRef ref("block.id", Version{1, 0});
    Fragment f(Fragment::MakeBlockRef(std::move(ref)));

    CHECK(f.IsBlockRef());
    CHECK_FALSE(f.IsStaticText());
    CHECK_FALSE(f.IsSeparator());
    CHECK(f.type() == FragmentType::BlockRef);
  }

  TEST_CASE("Fragment StaticText type") {
    Fragment f = Fragment::MakeStaticText("Hello, World!");

    CHECK_FALSE(f.IsBlockRef());
    CHECK(f.IsStaticText());
    CHECK_FALSE(f.IsSeparator());
    CHECK(f.type() == FragmentType::StaticText);
    CHECK(f.AsStaticText().text() == "Hello, World!");
  }

  TEST_CASE("Fragment Separator type") {
    Fragment f = Fragment::MakeSeparator(SeparatorType::Newline);

    CHECK_FALSE(f.IsBlockRef());
    CHECK_FALSE(f.IsStaticText());
    CHECK(f.IsSeparator());
    CHECK(f.type() == FragmentType::Separator);
    CHECK(f.AsSeparator().toString() == "\n");
  }

  TEST_CASE("Separator toString variants") {
    CHECK(Separator(SeparatorType::Newline).toString() == "\n");
    CHECK(Separator(SeparatorType::Paragraph).toString() == "\n\n");
    CHECK(Separator(SeparatorType::Hr).toString() == "\n---\n");
  }

  TEST_CASE("Fragment validate delegates to BlockRef") {
    BlockRef ref("block.id");  // UseLatest = true
    Fragment f = Fragment::MakeBlockRef(ref);

    auto err = f.validate(true);  // Draft context - OK
    CHECK(err.is_success());

    err = f.validate(false);  // Non-draft context - Error
    CHECK(err.is_error());
  }
}

// ==================== Composition Tests ====================

TEST_SUITE("Composition") {
  TEST_CASE("create composition") {
    Composition c("test.composition");
    CHECK(c.id() == "test.composition");
    CHECK(c.state() == BlockState::Draft);
    CHECK(c.fragmentCount() == 0);
  }

  TEST_CASE("add fragments to composition") {
    Composition c("test.composition");

    c.AddStaticText("Hello");
    c.AddSeparator(SeparatorType::Newline);
    c.AddBlockRef("block.id", Version{1, 0});

    CHECK(c.fragmentCount() == 3);
    CHECK(c.fragment(0).IsStaticText());
    CHECK(c.fragment(1).IsSeparator());
    CHECK(c.fragment(2).IsBlockRef());
  }

  TEST_CASE("insert and remove fragments") {
    Composition c("test.composition");
    c.AddStaticText("First");
    c.AddStaticText("Third");

    c.InsertFragment(1, Fragment::MakeStaticText("Second"));
    CHECK(c.fragmentCount() == 3);
    CHECK(c.fragment(1).AsStaticText().text() == "Second");

    c.RemoveFragment(1);
    CHECK(c.fragmentCount() == 2);
    CHECK(c.fragment(1).AsStaticText().text() == "Third");
  }

  TEST_CASE("clear fragments") {
    Composition c("test.composition");
    c.AddStaticText("Text");
    c.AddBlockRef("block.id", Version{1, 0});

    CHECK(c.fragmentCount() == 2);
    c.ClearFragments();
    CHECK(c.fragmentCount() == 0);
  }

  TEST_CASE("composition validate requires id") {
    Composition c;  // No ID
    auto err = c.validate();
    CHECK(err.is_error());
  }

  TEST_CASE("composition validate with id succeeds") {
    Composition c("test.composition");
    c.AddStaticText("Text");
    auto err = c.validate();
    CHECK(err.is_success());
  }

  TEST_CASE("composition publish validates BlockRef versions") {
    Composition c("test.composition");
    c.AddBlockRefLatest("block.id");  // UseLatest = true

    auto err = c.publish(Version{1, 0});
    CHECK(err.is_error());  // Cannot publish with UseLatest
    CHECK(err.code == ErrorCode::VersionRequired);
  }

  TEST_CASE("composition publish with explicit versions succeeds") {
    Composition c("test.composition");
    c.AddBlockRef("block.id", Version{1, 0});

    auto err = c.publish(Version{1, 0});
    CHECK(err.is_success());
    CHECK(c.state() == BlockState::Published);
  }

  TEST_CASE("composition deprecate") {
    Composition c("test.composition");
    auto err = c.publish(Version{1, 0});
    CHECK(err.is_success());

    c.deprecate();
    CHECK(c.state() == BlockState::Deprecated);
  }

  TEST_CASE("SemanticStyle isEmpty") {
    SemanticStyle s;
    CHECK(s.isEmpty());

    s.tone = "formal";
    CHECK_FALSE(s.isEmpty());
  }

  TEST_CASE("RenderContext builder") {
    auto ctx = RenderContext{}
                   .WithParam("name", "value")
                   .WithLanguage("en")
                   .with_strict_mode(true);

    CHECK(ctx.params["name"] == "value");
    CHECK(ctx.targetLanguage == "en");
    CHECK(ctx.strictMode);
  }
}

// ==================== BlockDraftBuilder Tests ====================

TEST_SUITE("BlockDraftBuilder") {
  TEST_CASE("build block with builder") {
    EngineTestFixture fixture;
    BlockDraft draft = BlockDraftBuilder("greeting.hello")
                           .WithType(BlockType::Role)
                           .WithTemplate(Template("Hello, {{name}}!"))
                           .WithDefault("name", "World")
                           .WithTag("simple")
                           .WithLanguage("en")
                           .WithDescription("A simple greeting")
                           .build();
    auto pubBlock =
        fixture.engine
            .PublishBlock(std::move(draft), Engine::VersionBump::Minor)
            .value();
    auto block =
        fixture.engine.LoadBlock("greeting.hello", pubBlock.version()).value();

    CHECK(block.Id() == "greeting.hello");
    CHECK(block.type() == BlockType::Role);
    CHECK(block.defaults().at("name") == "World");
    CHECK(block.tags().contains("simple"));
    CHECK(block.language() == "en");
    CHECK(block.description() == "A simple greeting");
  }
}

// ==================== CompositionDraftBuilder Tests ====================

TEST_SUITE("CompositionDraftBuilder") {
  TEST_CASE("build composition with builder") {
    EngineTestFixture fixture;

    // Publish prerequisite block first
    BlockDraft helloDraft = BlockDraftBuilder("greeting.hello")
                                .WithTemplate(Template("Hello, {{name}}!"))
                                .build();
    auto helloPub =
        fixture.engine
            .PublishBlock(std::move(helloDraft), Engine::VersionBump::Minor)
            .value();

    auto builder = CompositionDraftBuilder("welcome.message")
                       .WithProjectKey("myproject")
                       .WithDescription("Welcome message")
                       .AddStaticText("# Welcome\n\n")
                       .AddBlockRef("greeting.hello", 1, 0, {{"name", "User"}})
                       .AddSeparator(SeparatorType::Paragraph)
                       .AddStaticText("Enjoy your stay!");
    auto draft = builder.build();
    auto pubComp =
        fixture.engine
            .PublishComposition(std::move(draft), Engine::VersionBump::Minor)
            .value();
    auto comp =
        fixture.engine.LoadComposition("welcome.message", pubComp.version())
            .value();

    CHECK(comp.id() == "welcome.message");
    CHECK(comp.ProjectKey() == "myproject");
    CHECK(comp.description() == "Welcome message");
    CHECK(comp.fragmentCount() == 4);
  }

  TEST_CASE(
      "AddBlockRef(BlockRef) preserves a UseLatest ref instead of silently "
      "pinning it to Version{0,0}") {
    EngineTestFixture fixture;

    BlockDraft helloDraft = BlockDraftBuilder("greeting.hello")
                                .WithTemplate(Template("Hello!"))
                                .build();
    fixture.engine.PublishBlock(std::move(helloDraft),
                                Engine::VersionBump::Minor);

    // BlockRef(id) alone -- no version -- defaults to UseLatest().
    auto draft = CompositionDraftBuilder("uses.latest")
                     .AddBlockRef(BlockRef("greeting.hello"))
                     .build();

    // PublishComposition's UseLatest guard must still see this ref as
    // UseLatest and reject it -- not silently publish a composition
    // pinned to the nonexistent Version{0,0}, which would be
    // unrenderable with no clear error explaining why.
    auto published = fixture.engine.PublishComposition(std::move(draft));
    REQUIRE(published.HasError());
    CHECK(published.error().code == ErrorCode::VersionRequired);
  }
}

// ==================== Renderer Tests (using real Engine) ====================

TEST_CASE_FIXTURE(EngineTestFixture, "render single block through engine") {
  // Create and publish block
  createAndPublishBlock("greeting.hello", "Hello, {{name}}!",
                        {{"name", "World"}});

  // Render through engine
  auto result = engine.RenderBlock("greeting.hello", Version{1, 0});

  CHECK(result.HasValue());
  CHECK(result.value() == "Hello, World!");
}

TEST_CASE_FIXTURE(EngineTestFixture,
                  "render block with runtime override through engine") {
  createAndPublishBlock("greeting.hello", "Hello, {{name}}!",
                        {{"name", "World"}});

  auto ctx = RenderContext{}.WithParam("name", "Alice");
  auto result = engine.RenderBlock("greeting.hello", Version{1, 0}, ctx);

  CHECK(result.HasValue());
  CHECK(result.value() == "Hello, Alice!");
}

TEST_CASE_FIXTURE(EngineTestFixture, "render block with missing param fails") {
  createAndPublishBlock("greeting.hello", "Hello, {{name}}!");  // No defaults

  auto result = engine.RenderBlock("greeting.hello", Version{1, 0});

  CHECK(result.HasError());
  CHECK(result.error().code == ErrorCode::MissingParam);
}

TEST_CASE_FIXTURE(EngineTestFixture,
                  "render composition with static text only") {
  CompositionDraftBuilder builder("test.comp");
  builder.AddStaticText("Hello, World!");
  auto draft = builder.build();
  auto pubResult =
      engine.PublishComposition(std::move(draft), Engine::VersionBump::Minor);
  REQUIRE(pubResult.HasValue());

  auto result = engine.Render("test.comp");

  CHECK(result.HasValue());
  CHECK(result.value().text == "Hello, World!");
  CHECK(result.value().compositionId == "test.comp");
}

TEST_CASE_FIXTURE(EngineTestFixture, "render unpublished composition fails") {
  // No composition published with this ID
  auto result = engine.Render("test.comp");

  CHECK(result.HasError());
  // Expect storage not found error, since no draft persistence
  // CHECK(result.error().code == ErrorCode::PublishedRequired); // Adjust based
  // on actual error
}

TEST_CASE_FIXTURE(EngineTestFixture, "render composition with separator") {
  CompositionDraftBuilder builder("test.comp");
  builder.AddStaticText("Line 1");
  builder.AddSeparator(SeparatorType::Newline);
  builder.AddStaticText("Line 2");
  auto draft = builder.build();
  engine.PublishComposition(std::move(draft), Engine::VersionBump::Minor);

  auto result = engine.Render("test.comp");

  CHECK(result.HasValue());
  CHECK(result.value().text == "Line 1\nLine 2");
}

TEST_CASE_FIXTURE(EngineTestFixture, "render composition with BlockRef") {
  // Create and publish block
  createAndPublishBlock("greeting.hello", "Hello, {{name}}!",
                        {{"name", "World"}});

  // Create and publish composition with BlockRef
  CompositionDraftBuilder builder("test.comp");
  builder.AddBlockRef("greeting.hello", 1, 0);
  auto draft = builder.build();
  engine.PublishComposition(std::move(draft), Engine::VersionBump::Minor);

  auto result = engine.Render("test.comp");

  CHECK(result.HasValue());
  CHECK(result.value().text == "Hello, World!");
  CHECK(result.value().blocksUsed.size() == 1);
  CHECK(result.value().blocksUsed[0].first == "greeting.hello");
  CHECK(result.value().blocksUsed[0].second == Version{1, 0});
}

TEST_CASE_FIXTURE(EngineTestFixture,
                  "render composition with BlockRef and runtime params") {
  createAndPublishBlock("greeting.hello", "Hello, {{name}}!",
                        {{"name", "World"}});

  CompositionDraftBuilder builder("test.comp");
  builder.AddBlockRef("greeting.hello", 1, 0);
  auto draft = builder.build();
  engine.PublishComposition(std::move(draft), Engine::VersionBump::Minor);

  auto ctx = RenderContext{}.WithParam("name", "Alice");
  auto result = engine.Render("test.comp", ctx);

  CHECK(result.HasValue());
  CHECK(result.value().text == "Hello, Alice!");
}

TEST_CASE_FIXTURE(EngineTestFixture,
                  "render composition with multiple fragments") {
  createAndPublishBlock("greeting.hello", "Hello, {{name}}!",
                        {{"name", "World"}});
  createAndPublishBlock("farewell.goodbye", "Goodbye, {{name}}!",
                        {{"name", "Friend"}});

  CompositionDraftBuilder builder("test.comp");
  builder.AddBlockRef("greeting.hello", 1, 0, {{"name", "Alice"}});
  builder.AddSeparator(SeparatorType::Paragraph);
  builder.AddBlockRef("farewell.goodbye", 1, 0, {{"name", "Alice"}});
  auto draft = builder.build();
  engine.PublishComposition(std::move(draft), Version{1, 0});

  auto result = engine.Render("test.comp", Version{1, 0});

  CHECK(result.HasValue());
  CHECK(result.value().text == "Hello, Alice!\n\nGoodbye, Alice!");
  CHECK(result.value().blocksUsed.size() == 2);
}

TEST_CASE_FIXTURE(EngineTestFixture, "render with missing block fails") {
  // Create composition referencing non-existent block
  CompositionDraftBuilder builder("test.comp");
  builder.AddBlockRef("missing.block", 1, 0);
  auto draft = builder.build();
  engine.PublishComposition(std::move(draft), Version{1, 0});

  auto result = engine.Render("test.comp", Version{1, 0});

  CHECK(result.HasError());
  CHECK(result.error().code == ErrorCode::BlockNotFound);
}

TEST_CASE_FIXTURE(EngineTestFixture, "render result contains metadata") {
  CompositionDraftBuilder builder("my.composition");
  builder.AddStaticText("Text");
  auto draft = builder.build();
  engine.PublishComposition(std::move(draft), Version{2, 5});

  auto result = engine.Render("my.composition", Version{2, 5});

  CHECK(result.HasValue());
  CHECK(result.value().compositionId == "my.composition");
  CHECK(result.value().compositionVersion == Version{2, 5});
}

// ==================== Engine Integration Tests ====================

TEST_CASE_FIXTURE(EngineTestFixture, "create and save block") {
  BlockDraft draft = BlockDraftBuilder("test.block")
                         .WithTemplate(Template("Hello, {{name}}!"))
                         .WithDefaults({{"name", "World"}})
                         .build();
  // Drafts are not saved separately; publish or use directly

  // Load after publish
  auto pub = engine.PublishBlock(std::move(draft), Version{1, 0}).value();
  auto result = engine.LoadBlock("test.block", pub.version());
  CHECK(result.HasValue());
  CHECK(result.value().Id() == "test.block");
  CHECK(result.value().templ().ExtractParamNames().size() == 1);
}

TEST_CASE_FIXTURE(EngineTestFixture, "publish block workflow") {
  BlockDraft draft =
      BlockDraftBuilder("test.block").WithTemplate(Template("Hello!")).build();
  auto result = engine.PublishBlock(std::move(draft), Version{1, 0});
  CHECK(result.HasValue());

  // Check published block
  auto block = engine.LoadBlock("test.block", Version{1, 0}).value();
  CHECK(block.state() == BlockState::Published);
  CHECK(block.version() == Version{1, 0});

  // Check latest version
  auto verResult = engine.GetLatestBlockVersion("test.block");
  CHECK(verResult.HasValue());
  CHECK(verResult.value() == Version{1, 0});
}

TEST_CASE_FIXTURE(EngineTestFixture, "list blocks") {
  createAndPublishBlock("block1", "Text 1", {});
  createAndPublishBlock("block2", "Text 2", {});

  auto blocks = engine.ListBlocks();
  CHECK(blocks.size() == 2);
}

TEST_CASE_FIXTURE(EngineTestFixture, "create and save composition") {
  CompositionDraftBuilder builder("test.comp");
  builder.AddStaticText("Hello");
  auto draft = builder.build();
  // Drafts not saved; test publish instead

  auto pub = engine.PublishComposition(std::move(draft), Version{1, 0}).value();
  auto result = engine.LoadComposition("test.comp", pub.version());
  CHECK(result.HasValue());
  CHECK(result.value().id() == "test.comp");
  CHECK(result.value().fragmentCount() == 1);
}

TEST_CASE_FIXTURE(EngineTestFixture, "publish composition workflow") {
  // First create and publish a block
  createAndPublishBlock("test.block", "Hello!", {});

  // Then create and publish composition referencing it
  CompositionDraftBuilder builder("test.comp");
  builder.AddBlockRef("test.block", 1, 0);
  auto draft = builder.build();
  auto result = engine.PublishComposition(std::move(draft), Version{1, 0});
  CHECK(result.HasValue());

  // List compositions
  auto comps = engine.ListCompositions();
  CHECK(comps.size() == 1);
  CHECK(comps[0] == "test.comp");
}

TEST_CASE_FIXTURE(EngineTestFixture, "validate block through engine") {
  BlockDraft draft = BlockDraftBuilder("test.block")
                         .WithTemplate(Template("Hello, {{name}}!"))
                         .WithDefaults({{"name", "World"}})
                         .build();
  auto pub = engine.PublishBlock(std::move(draft), Version{1, 0}).value();

  auto err = engine.ValidateBlock("test.block");
  CHECK(err.is_success());
}

TEST_CASE_FIXTURE(EngineTestFixture, "validate composition through engine") {
  createAndPublishBlock("test.block", "Hello!", {});

  CompositionDraftBuilder builder("test.comp");
  builder.AddBlockRef("test.block", 1, 0);
  auto draft = builder.build();
  auto pubResult = engine.PublishComposition(std::move(draft), Version{1, 0});
  REQUIRE(pubResult.HasValue());  // publish to validate

  auto err = engine.ValidateComposition("test.comp");
  CHECK(err.is_success());
}

// Logger init/shutdown run as ordinary statements inside main(), not via a
// global static object's constructor/destructor. spdlog::shutdown() (called
// by tf::Logger::shutdown()) must run before the C++ runtime's exit-time
// static-destructor sweep begins: doing it from a global object's destructor
// does not achieve that, since that destructor runs *during* the same
// unspecified-order sweep as spdlog's own registry singleton, and was
// observed to corrupt the heap / crash inside the registry's destructor on
// every platform once tf::Logger::shutdown() was made to touch it.
int main(int argc, char** argv) {
  tf::Logger::init(tf::LogLevel::Warn);  // Only warnings and errors during tests

  doctest::Context context;
  context.applyCommandLine(argc, argv);
  const int result = context.run();

  tf::Logger::shutdown();

  if (context.shouldExit()) {
    return result;
  }
  return result;
}
