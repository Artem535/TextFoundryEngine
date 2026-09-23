#include "commands.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <rfl/json.hpp>

#include "command_catalog.h"
#include "completion.h"
#include "dto.h"

namespace cli {
namespace {

struct BlockWriteArgs {
  std::string id;
  std::string template_text;
  std::string type = "domain";
  std::string description;
  std::string language = "en";
  std::string revision_comment;
  std::string version;
  std::string bump = "minor";
  std::vector<std::string> defaults;
  std::vector<std::string> tags;
};

struct BlockInspectArgs {
  std::string id;
  std::string version;
};

struct CompositionCreateArgs {
  std::string id;
  std::string description;
  std::string from_json;
  std::vector<std::string> blocks;
  std::vector<std::string> texts;
};

struct EntityVersionArgs {
  std::string id;
  std::string version;
};

struct RenderArgs {
  std::string kind;
  std::string id;
  std::string version;
  std::vector<std::string> params;
};

const CommandSpec& FindRootCommand(std::string_view name) {
  return FindByName(RootCommands(), name, CommandSpecName, "command");
}

const CommandSpec& FindSubcommand(const CommandSpec& parent,
                                  std::string_view name) {
  return FindByName(parent.subcommands, name, CommandSpecName, "subcommand");
}

void ApplyAliases(CLI::App& command, const CommandSpec& spec) {
  for (const auto& alias : spec.aliases) {
    command.alias(alias);
  }
}

tf::Result<tf::Params> ParseParams(const std::vector<std::string>& values) {
  tf::Params params;
  for (const auto& value : values) {
    const auto separator = value.find('=');
    if (separator == std::string::npos || separator == 0) {
      return tf::Result<tf::Params>(tf::Error{
          tf::ErrorCode::InvalidParamType,
          "Parameter must use key=value format: " + value});
    }
    params[value.substr(0, separator)] = value.substr(separator + 1);
  }
  return tf::Result<tf::Params>(std::move(params));
}

tf::Result<tf::BlockType> ParseBlockType(const std::string& value) {
  try {
    return tf::Result<tf::BlockType>(tf::BlockTypeFromString(value));
  } catch (const std::invalid_argument& error) {
    return tf::Result<tf::BlockType>(tf::Error{
        tf::ErrorCode::InvalidParamType, error.what()});
  }
}

tf::Result<std::pair<std::string, tf::Version>> ParseBlockSpec(
    const std::string& value) {
  const auto separator = value.rfind('@');
  if (separator == std::string::npos || separator == 0 ||
      separator + 1 == value.size()) {
    return tf::Result<std::pair<std::string, tf::Version>>(
        tf::Error::VersionRequired());
  }
  auto version = ParseVersion(value.substr(separator + 1));
  if (version.HasError()) {
    return tf::Result<std::pair<std::string, tf::Version>>(version.error());
  }
  return tf::Result<std::pair<std::string, tf::Version>>(
      std::make_pair(value.substr(0, separator), version.value()));
}

tf::Result<CompositionDto> ReadCompositionDto(
    const std::string& path) {
  std::string json;
  if (path == "-") {
    json.assign(std::istreambuf_iterator<char>(std::cin),
                std::istreambuf_iterator<char>());
  } else {
    std::ifstream input(path);
    if (!input) {
      return tf::Result<CompositionDto>(tf::Error{
          tf::ErrorCode::StorageError, "Cannot open JSON input: " + path});
    }
    json.assign(std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>());
  }

  const auto parsed = rfl::json::read<CompositionDto>(json);
  if (!parsed.has_value()) {
    return tf::Result<CompositionDto>(tf::Error{
        tf::ErrorCode::InvalidParamType,
        "Invalid composition JSON: " + std::string(parsed.error().what())});
  }
  return tf::Result<CompositionDto>(parsed.value());
}

EntityView ToEntityView(std::string kind, const tf::PublishedBlock& block) {
  return EntityView{std::move(kind), block.id(), block.version().ToString(),
                    "published"};
}

EntityView ToEntityView(std::string kind,
                        const tf::PublishedComposition& composition) {
  return EntityView{std::move(kind), composition.id(),
                    composition.version().ToString(), "published"};
}

void AddBlockWriteCommand(CLI::App& parent, AppState& state,
                          std::string_view name, bool update) {
  const auto& spec = FindSubcommand(FindRootCommand("block"), name);
  auto* command =
      parent.add_subcommand(spec.name, spec.description);
  command->fallthrough();
  auto args = std::make_shared<BlockWriteArgs>();
  command->add_option("id", args->id, "Block identifier")->required();
  auto* template_option = command->add_option(
      "-t,--template", args->template_text,
      update ? "Template text (defaults to the current published template)"
             : "Template text");
  auto* type_option = command->add_option(
      "--type", args->type,
      update ? "Block type (defaults to the current published type)"
             : "Block type");
  auto* description_option = command->add_option(
      "--description", args->description,
      update ? "Description (defaults to the current published description)"
             : "Description");
  auto* language_option = command->add_option(
      "--language", args->language,
      update ? "Language (defaults to the current published language)"
             : "Language");
  command->add_option("--revision-comment", args->revision_comment,
                      "Revision comment");
  auto* defaults_option =
      command
          ->add_option("--default", args->defaults,
                       update ? "Default parameter as key=value (defaults "
                                "to the current published defaults)"
                              : "Default parameter as key=value")
          ->allow_extra_args(false)
          ->expected(1);
  auto* tags_option = command->add_option(
      "--tag", args->tags,
      update ? "Block tag (defaults to the current published tags)"
             : "Block tag");
  if (update) {
    command
        ->add_option("--bump", args->bump,
                     "Version bump for the new version: major or minor "
                     "(default: minor)")
        ->check(CLI::IsMember({"major", "minor"}));
  } else {
    template_option->required();
    command->add_option("--version", args->version,
                        "Explicit major.minor version");
  }

  command->callback([&state, args, update, template_option, type_option,
                     description_option, language_option, defaults_option,
                     tags_option] {
    std::optional<tf::Block> existing;
    if (update) {
      auto loaded = state.EnsureEngine().LoadBlock(args->id);
      if (loaded.HasError()) {
        state.Fail(loaded.error());
        return;
      }
      existing = std::move(loaded.value());
    }

    tf::Result<tf::BlockType> type_result =
        (type_option->count() > 0 || !existing.has_value())
            ? ParseBlockType(args->type)
            : tf::Result<tf::BlockType>(existing->type());
    if (type_result.HasError()) {
      state.Fail(type_result.error());
      return;
    }
    const tf::BlockType resolved_type = type_result.value();

    tf::Result<tf::Params> defaults_result =
        (defaults_option->count() > 0 || !existing.has_value())
            ? ParseParams(args->defaults)
            : tf::Result<tf::Params>(existing->defaults());
    if (defaults_result.HasError()) {
      state.Fail(defaults_result.error());
      return;
    }
    const tf::Params& resolved_defaults = defaults_result.value();

    const std::string resolved_template =
        (template_option->count() > 0 || !existing.has_value())
            ? args->template_text
            : existing->templ().Content();
    if (resolved_template.empty()) {
      state.Fail(tf::Error{tf::ErrorCode::InvalidParamType,
                           "--template is required"});
      return;
    }
    const std::string resolved_description =
        (description_option->count() > 0 || !existing.has_value())
            ? args->description
            : existing->description();
    const std::string resolved_language =
        (language_option->count() > 0 || !existing.has_value())
            ? args->language
            : existing->language();

    tf::BlockDraftBuilder builder(args->id);
    builder.WithType(resolved_type)
        .WithTemplate(tf::Template(resolved_template))
        .WithDefaults(resolved_defaults)
        .WithDescription(resolved_description)
        .WithLanguage(resolved_language)
        .WithRevisionComment(args->revision_comment);
    // The CLI has no flag to author a param schema, so on publish it must
    // always carry the currently published block's schema forward --
    // otherwise `tfe block publish` would silently drop it, even though
    // every other field either has an explicit override or falls back to
    // the published value.
    if (existing.has_value()) {
      builder.WithParamSchema(existing->param_schema());
    }
    if (tags_option->count() > 0 || !existing.has_value()) {
      for (const auto& tag : args->tags) {
        builder.WithTag(tag);
      }
    } else {
      for (const auto& tag : existing->tags()) {
        builder.WithTag(tag);
      }
    }

    auto& engine = state.EnsureEngine();
    tf::Result<tf::PublishedBlock> result = [&]() {
      if (update) {
        const auto bump = args->bump == "major"
                              ? tf::Engine::VersionBump::Major
                              : tf::Engine::VersionBump::Minor;
        return engine.UpdateBlock(std::move(builder).build(), bump);
      }
      if (args->version.empty()) {
        return engine.PublishBlock(std::move(builder).build());
      }
      auto version = ParseVersion(args->version);
      if (version.HasError()) {
        return tf::Result<tf::PublishedBlock>(version.error());
      }
      return engine.PublishBlock(std::move(builder).build(), version.value());
    }();
    if (result.HasError()) {
      state.Fail(result.error());
      return;
    }
    state.Emit(ToEntityView("block", result.value()));
  });
}

void AddBlockCommands(CLI::App& app, AppState& state) {
  const auto& spec = FindRootCommand("block");
  auto* command = app.add_subcommand(spec.name, spec.description);
  ApplyAliases(*command, spec);
  command->fallthrough();

  AddBlockWriteCommand(*command, state, "create", false);
  AddBlockWriteCommand(*command, state, "publish", true);

  auto* list = command->add_subcommand(
      FindSubcommand(spec, "list").name, "List blocks");
  list->fallthrough();
  list->callback([&state] {
    state.Emit(IdListView{"blocks", state.EnsureEngine().ListBlocks()});
  });

  auto inspect_args = std::make_shared<BlockInspectArgs>();
  auto* inspect = command->add_subcommand(
      FindSubcommand(spec, "inspect").name, "Inspect a block");
  inspect->fallthrough();
  inspect->add_option("id", inspect_args->id)->required();
  inspect->add_option("--version", inspect_args->version);
  inspect->callback([&state, inspect_args] {
    std::optional<tf::Version> version;
    if (!inspect_args->version.empty()) {
      auto parsed = ParseVersion(inspect_args->version);
      if (parsed.HasError()) {
        state.Fail(parsed.error());
        return;
      }
      version = parsed.value();
    }
    auto block =
        state.EnsureEngine().LoadBlock(inspect_args->id, version);
    if (block.HasError()) {
      state.Fail(block.error());
      return;
    }
    state.Emit(EntityView{"block", block.value().Id(),
                          block.value().version().ToString(),
                          std::string(tf::BlockStateToString(
                              block.value().state()))});
  });

  auto version_args = std::make_shared<EntityVersionArgs>();
  auto* deprecate = command->add_subcommand(
      FindSubcommand(spec, "deprecate").name, "Deprecate a block version");
  deprecate->fallthrough();
  deprecate->add_option("id", version_args->id)->required();
  deprecate->add_option("--version", version_args->version)->required();
  deprecate->callback([&state, version_args] {
    auto version = ParseVersion(version_args->version);
    if (version.HasError()) {
      state.Fail(version.error());
      return;
    }
    const auto error = state.EnsureEngine().DeprecateBlock(
        version_args->id, version.value());
    if (error.is_error()) {
      state.Fail(error);
      return;
    }
    state.Emit(EntityView{"block", version_args->id,
                          version.value().ToString(), "deprecated"});
  });
}

void AddCompositionCommands(CLI::App& app, AppState& state) {
  const auto& spec = FindRootCommand("comp");
  auto* command = app.add_subcommand(spec.name, spec.description);
  ApplyAliases(*command, spec);
  command->fallthrough();

  auto create_args = std::make_shared<CompositionCreateArgs>();
  auto* create = command->add_subcommand(
      FindSubcommand(spec, "create").name, "Create a composition");
  create->fallthrough();
  create->add_option("id", create_args->id)->required();
  auto* block_option = create->add_option(
      "-b,--block", create_args->blocks, "Block reference id@major.minor");
  auto* text_option =
      create->add_option("-t,--text", create_args->texts, "Static text");
  create->add_option("--desc", create_args->description, "Description");
  auto* json_option = create->add_option(
      "--from-json", create_args->from_json, "Composition JSON file or - for stdin");
  json_option->excludes(block_option, text_option);
  create->callback([&state, create_args] {
    const auto publish_draft =
        [&state](tf::Result<tf::CompositionDraft> draft) {
          if (draft.HasError()) {
            state.Fail(draft.error());
            return;
          }
          auto published = state.EnsureEngine().PublishComposition(
              std::move(draft).value());
          if (published.HasError()) {
            state.Fail(published.error());
            return;
          }
          state.Emit(ToEntityView("composition", published.value()));
        };

    if (!create_args->from_json.empty()) {
      if (!create_args->blocks.empty() || !create_args->texts.empty()) {
        state.Fail(tf::Error{tf::ErrorCode::InvalidParamType,
                             "--from-json excludes --block and --text"});
        return;
      }
      auto dto = ReadCompositionDto(create_args->from_json);
      if (dto.HasError()) {
        state.Fail(dto.error());
        return;
      }
      // Per spec: only `fragments` is read from the JSON body. id and
      // description always come from the CLI args, even when --desc was
      // not passed, so there is exactly one place naming the composition.
      dto.value().id = create_args->id;
      dto.value().description = create_args->description;
      publish_draft(ToCompositionDraft(dto.value(), state.config.project_key));
      return;
    } else {
      tf::CompositionDraftBuilder builder(create_args->id);
      builder.WithProjectKey(state.config.project_key)
          .WithDescription(create_args->description);
      for (const auto& block_spec : create_args->blocks) {
        auto parsed = ParseBlockSpec(block_spec);
        if (parsed.HasError()) {
          state.Fail(parsed.error());
          return;
        }
        builder.AddBlockRef(parsed.value().first, parsed.value().second.major,
                            parsed.value().second.minor);
      }
      for (const auto& text : create_args->texts) {
        builder.AddStaticText(text);
      }
      publish_draft(tf::Result<tf::CompositionDraft>(builder.build()));
    }
  });

  auto* list = command->add_subcommand(
      FindSubcommand(spec, "list").name, "List compositions");
  list->fallthrough();
  list->callback([&state] {
    state.Emit(
        IdListView{"compositions", state.EnsureEngine().ListCompositions()});
  });

  auto inspect_args = std::make_shared<EntityVersionArgs>();
  auto* inspect = command->add_subcommand(
      FindSubcommand(spec, "inspect").name, "Inspect a composition");
  inspect->fallthrough();
  inspect->add_option("id", inspect_args->id)->required();
  inspect->add_option("--version", inspect_args->version);
  inspect->callback([&state, inspect_args] {
    std::optional<tf::Version> version;
    if (!inspect_args->version.empty()) {
      auto parsed = ParseVersion(inspect_args->version);
      if (parsed.HasError()) {
        state.Fail(parsed.error());
        return;
      }
      version = parsed.value();
    }
    auto composition =
        state.EnsureEngine().LoadComposition(inspect_args->id, version);
    if (composition.HasError()) {
      state.Fail(composition.error());
      return;
    }
    state.Emit(EntityView{
        "composition", composition.value().id(),
        composition.value().version().ToString(),
        std::string(tf::BlockStateToString(composition.value().state()))});
  });

  auto version_args = std::make_shared<EntityVersionArgs>();
  auto* deprecate = command->add_subcommand(
      FindSubcommand(spec, "deprecate").name,
      "Deprecate a composition version");
  deprecate->fallthrough();
  deprecate->add_option("id", version_args->id)->required();
  deprecate->add_option("--version", version_args->version)->required();
  deprecate->callback([&state, version_args] {
    auto version = ParseVersion(version_args->version);
    if (version.HasError()) {
      state.Fail(version.error());
      return;
    }
    const auto error = state.EnsureEngine().DeprecateComposition(
        version_args->id, version.value());
    if (error.is_error()) {
      state.Fail(error);
      return;
    }
    state.Emit(EntityView{"composition", version_args->id,
                          version.value().ToString(), "deprecated"});
  });
}

void AddRenderCommand(CLI::App& app, AppState& state) {
  const auto& spec = FindRootCommand("render");
  auto* command = app.add_subcommand(spec.name, spec.description);
  ApplyAliases(*command, spec);
  command->fallthrough();
  auto args = std::make_shared<RenderArgs>();
  command->add_option("kind", args->kind, "block or composition")
      ->required()
      ->check(CLI::IsMember({"block", "composition"}));
  command->add_option("id", args->id)->required();
  command->add_option("--version", args->version);
  command->add_option("-p,--param", args->params, "Runtime parameter key=value");
  command->callback([&state, args] {
    auto params = ParseParams(args->params);
    if (params.HasError()) {
      state.Fail(params.error());
      return;
    }
    tf::RenderContext context;
    context.params = params.value();
    context.strictMode = state.config.strict;

    if (args->kind == "block") {
      tf::Result<std::string> result(tf::Error{
          tf::ErrorCode::StorageError, "render result not initialized"});
      if (args->version.empty()) {
        result = state.EnsureEngine().RenderBlock(args->id, context);
      } else {
        auto version = ParseVersion(args->version);
        if (version.HasError()) {
          state.Fail(version.error());
          return;
        }
        result =
            state.EnsureEngine().RenderBlock(args->id, version.value(), context);
      }
      if (result.HasError()) {
        state.Fail(result.error());
        return;
      }
      state.Emit(RenderView{"render", result.value(), args->id,
                            args->version});
      return;
    }

    tf::Result<tf::RenderResult> result(tf::Error{
        tf::ErrorCode::StorageError, "render result not initialized"});
    if (args->version.empty()) {
      result = state.EnsureEngine().Render(args->id, context);
    } else {
      auto version = ParseVersion(args->version);
      if (version.HasError()) {
        state.Fail(version.error());
        return;
      }
      result = state.EnsureEngine().Render(args->id, version.value(), context);
    }
    if (result.HasError()) {
      state.Fail(result.error());
      return;
    }
    state.Emit(RenderView{"render", result.value().text, result.value().compositionId,
                          result.value().compositionVersion.ToString()});
  });
}

void AddValidateCommand(CLI::App& app, AppState& state) {
  const auto& spec = FindRootCommand("validate");
  auto* command = app.add_subcommand(spec.name, spec.description);
  ApplyAliases(*command, spec);
  command->fallthrough();
  auto args = std::make_shared<RenderArgs>();
  command->add_option("kind", args->kind, "block or composition")
      ->required()
      ->check(CLI::IsMember({"block", "composition"}));
  command->add_option("id", args->id)->required();
  command->callback([&state, args] {
    // ValidateBlock/ValidateComposition conflate two different situations
    // into one Error: the entity couldn't even be loaded (wrong id, no
    // such version, storage trouble), or it loaded fine but is
    // structurally invalid. Those need different treatment: a load
    // failure is a genuine command failure (the check never ran) and
    // must use the documented {"error":...} contract with a non-zero
    // exit, matching every other command's not-found behavior (e.g.
    // `block inspect`). A structural-invalid result means the check DID
    // run and completed successfully -- that's a ValidationView, not an
    // error document, so it must not claim a command failure either.
    // The two cases are distinguishable by error code: none of
    // Composition::validate()/Fragment::validate()/BlockRef::validate()'s
    // codes overlap with the lookup-failure codes below.
    const auto error = args->kind == "block"
                           ? state.EnsureEngine().ValidateBlock(args->id)
                           : state.EnsureEngine().ValidateComposition(args->id);
    if (error.is_error()) {
      const bool entity_unavailable =
          error.code == tf::ErrorCode::BlockNotFound ||
          error.code == tf::ErrorCode::CompositionNotFound ||
          error.code == tf::ErrorCode::VersionNotFound ||
          error.code == tf::ErrorCode::StorageError;
      if (entity_unavailable) {
        state.Fail(error);
        return;
      }
      state.Emit(ValidationView{args->kind, args->id, false, error.message});
      return;
    }
    state.Emit(ValidationView{args->kind, args->id, true, "valid"});
  });
}

void AddCompletionCommand(CLI::App& app, AppState& state) {
  const auto& spec = FindRootCommand("completion");
  auto* command = app.add_subcommand(spec.name, spec.description);
  ApplyAliases(*command, spec);
  command->fallthrough();
  auto shell = std::make_shared<std::string>();
  command->add_option("shell", *shell, "bash, zsh, or fish")
      ->required()
      ->check(CLI::IsMember({"bash", "zsh", "fish"}));
  command->callback([&state, shell] {
    const auto parsed = TryParseShell(*shell);
    if (!parsed.has_value()) {
      state.Fail(tf::Error{tf::ErrorCode::InvalidParamType,
                           "Unsupported shell: " + *shell});
      return;
    }
    state.output << GenerateCompletion(*parsed) << std::flush;
  });
}

}  // namespace

tf::Engine& AppState::EnsureEngine() {
  if (!engine) {
    tf::EngineConfig config;
    config.ProjectKey = this->config.project_key;
    config.strict_mode = this->config.strict;
    config.default_data_path = this->config.data_path;
    engine = std::make_unique<tf::Engine>(std::move(config));
  }
  return *engine;
}

void AppState::Emit(const IdListView& view) {
  PrintResult(view, config.json, output);
}

void AppState::Emit(const EntityView& view) {
  PrintResult(view, config.json, output);
}

void AppState::Emit(const RenderView& view) {
  PrintResult(view, config.json, output);
}

void AppState::Emit(const ValidationView& view) {
  PrintResult(view, config.json, output);
}

void AppState::Fail(const tf::Error& error) {
  result_code = PrintError(error, config.json, output, errors);
}

void RegisterCommands(CLI::App& app, AppState& state) {
  AddBlockCommands(app, state);
  AddCompositionCommands(app, state);
  AddRenderCommand(app, state);
  AddValidateCommand(app, state);
  AddCompletionCommand(app, state);
}

}  // namespace cli
