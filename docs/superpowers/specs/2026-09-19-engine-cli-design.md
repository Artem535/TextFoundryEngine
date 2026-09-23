# TextFoundryEngine Standalone CLI Design

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement the plan derived from this spec. This document is a design spec, not an implementation plan — see `docs/superpowers/plans/` for the plan once written.

**Goal:** Give `TextFoundryEngine` its own standalone CLI binary (`tfe`) that exercises the engine directly — no dependency on TextFoundry's `text_foundry_cli` or any TextFoundry code — so an agent (or a human) can create/publish blocks and compositions, including ones using the new `Conditional` fragment type, render them, and validate them, entirely from this repository. Every command supports a `--json` output mode for agent consumption and a human-readable table mode by default.

**Architecture:** A new `cli/` directory holds a small CLI11-based `App` that talks to `tf::Engine` directly (same pattern TextFoundry's own CLI uses, but reimplemented here rather than shared/extracted — see "Why not share code with `text_foundry_cli`" below). A thin DTO layer (`cli/dto.h`/`cli/dto.cc`) mirrors `Fragment`/`Composition`/`Block` as plain, `reflect-cpp`-serializable structs, used for two purposes: (1) parsing a `--from-json`/stdin document into a full `Composition` draft, including nested `Conditional` branches that flag-based input can't express; (2) building command results, which a single shared print helper renders either as JSON (`reflect-cpp`) or as an FTXUI `Table` depending on whether `--json` was passed. A shared command catalog is the source of truth for CLI11 registration and shell-completion generation, so the parser and generated Bash/Zsh/Fish completions cannot drift.

**Tech Stack:** C++23, `CLI11` (vcpkg dependency), `FTXUI`'s `dom` and `screen` modules (vcpkg dependency, current repository port), `reflectcpp` (existing dependency), `textfoundry_core` + `textfoundry_objectbox_store` (existing in-repo targets).

## Context

This follows directly from the `Conditional` (if/elif/else) feature (`docs/superpowers/specs/2026-09-18-renderer-conditional-logic-design.md`), which explicitly deferred CLI/JSON authoring as out of scope because TextFoundry's own `tf comp create` command cannot express nested branch structure — it builds a `Composition` from two independent flag lists (`-b`/`--block`, `-t`/`--text`) with no way to represent a `Conditional`, and no representation for `Separator` at all.

The original plan was to redesign `tf`'s own CLI (its `comp create` command in particular) to fix this. Mid-design, the direction changed: rather than reworking `text_foundry_cli` — which is TextFoundry's CLI, serving TextFoundry's own scope (AI-assisted workflows, GUI-adjacent commands, project settings) — build a **separate, engine-only CLI** that lives in `TextFoundryEngine` and depends on nothing but the engine itself. `text_foundry_cli` is explicitly untouched by this work.

This CLI is meant to be comfortable for an LLM agent to drive: structured JSON output on request, and full `Composition`/`Fragment` tree authoring (via JSON) so an agent isn't limited to what a flag list can express.

## Global Constraints

- Lives entirely in `TextFoundryEngine` (`/home/a.durynin/Projects/C++/TextFoundryEngine`), new `cli/` directory. No changes to `TextFoundry`'s `src/text_foundry_cli` or any other TextFoundry code.
- New binary target name: `tfe`. New CMake option `TEXTFOUNDRY_ENGINE_BUILD_CLI`, default `ON`.
- Depends on `textfoundry_objectbox_store` (needs a real, persisted `IBlockRepository`/`ICompositionRepository` — `EngineConfig::default_data_path` supports `memory:` for tests/dry-run, but the CLI's default is a real on-disk path, mirroring `tf`'s own default). Building `tfe` therefore requires `TEXTFOUNDRY_ENGINE_BUILD_OBJECTBOX_STORE=ON`; if a consumer sets that `OFF`, `TEXTFOUNDRY_ENGINE_BUILD_CLI` must also resolve to `OFF` (CMake `option` dependency, not a hard error) — this is checked during plan-writing against the exact CMake option-dependency pattern already used elsewhere in the codebase, if any, or done with a plain `if()` guard around `add_subdirectory(cli)`.
- New vcpkg dependencies: `cli11` and `ftxui`. The CLI must remain buildable from the manifest and must not fetch FTXUI through an independent `FetchContent` declaration.
- Command surface is the **non-AI subset** of `tf`'s commands, confirmed against `text_foundry_cli/application.cc`: `block create`, `block publish`, `block list`, `block deprecate`, `block inspect`, `comp create`, `comp list`, `comp deprecate`, `comp inspect`, `render`, `validate`, and `completion bash|zsh|fish`. No `tui` subcommand, no AI-assisted commands (those depend on `textfoundry_ai`, which is TextFoundry-only and not part of the engine).
- Output: every command supports a global `--json` flag. With `--json`, stdout is a single JSON document (`reflect-cpp`-serialized) and nothing else. Without it, stdout is an FTXUI-rendered table. Both modes report errors the same way (see "Error Output" below) and use the process exit code to signal success/failure — an agent parsing `--json` output never needs to distinguish success from failure by parsing text.
- `reflect-cpp` cannot serialize the engine's domain classes directly (`Fragment`/`Composition`/`Block` have private members) — confirmed in the prior CLI-redesign investigation by reading `reflect-cpp`'s actual headers. All JSON in and out of this CLI goes through the DTO layer in `cli/dto.h`/`cli/dto.cc`; the domain classes themselves are not touched.

## 1. DTO Layer and JSON Input

**File: `cli/dto.h`** — plain, public-field structs mirroring the engine's domain types, one per `Fragment` variant plus `Composition`:

```cpp
struct ConditionDto {
  std::string attribute;
  std::vector<std::string> allowed_values;
  bool negate = false;
};

struct BranchDto {
  std::vector<ConditionDto> conditions;
  std::vector<FragmentDto> content;  // see tagged-union note below
};

struct BlockRefFragmentDto {
  std::string block_id;
  std::optional<std::string> version;               // unset = latest published
  std::unordered_map<std::string, std::string> params;
};

struct StaticTextFragmentDto {
  std::string text;
};

struct SeparatorFragmentDto {
  std::string separator_type;  // "newline" | "paragraph" | "hr"
};

struct ConditionalFragmentDto {
  std::vector<BranchDto> branches;
  std::optional<std::vector<FragmentDto>> else_content;
};

// FragmentDto is a hand-rolled tagged union over the four variants above,
// discriminated by a "kind" field ("block_ref" | "static_text" |
// "separator" | "conditional"). Exactly one matching optional payload must
// be present; conversion validates this invariant before creating a domain
// Fragment. This keeps the public JSON contract explicit and independent of
// reflect-cpp's internal tagged-union representation.

struct CompositionDto {
  std::string id;
  std::optional<std::string> description;
  std::vector<FragmentDto> fragments;
};
```

Conversion functions, `cli/dto.cc`:

```cpp
tf::Result<tf::Conditional> FromDto(const ConditionalFragmentDto&);
tf::Fragment FromDto(const FragmentDto&);
tf::Result<std::vector<tf::Fragment>> FromDto(const std::vector<FragmentDto>&);
```

`FromDto` builds domain objects directly (`Fragment::MakeConditional`, `Fragment::MakeBlockRef`, etc.) — it does not go through `ConditionalBuilder`/`CompositionDraftBuilder`, since those are fluent APIs for hand-written C++ call sites, not bulk conversion. A conversion error (e.g. an unrecognized `type` tag) surfaces as an engine `Error`, not a thrown exception, consistent with the DTO boundary being a data/domain validity concern.

**`tfe comp create` implements the same flag-based input `tf comp create` uses today (`-b,--block`, `-t,--text`, `--desc` — new code in `cli/app.cc`, not shared with `text_foundry_cli`, per the Global Constraints), and gains one addition: a mutually-exclusive JSON input mode.**

```
tfe comp create <id> -b block1 -b block2 -t "static text" [existing flag form]
tfe comp create <id> --from-json <file>
tfe comp create <id> --from-json -                  # read JSON from stdin
```

`--from-json` takes a `CompositionDto` (id/description ignored in favor of the positional `<id>` and an optional `--desc`, to keep exactly one place — the CLI args — that names the composition being created; only `fragments` is read from the JSON body). This is the only way to author a `Conditional` from the CLI — the flag form (`-b`/`-t`) stays exactly as simple as `tf`'s today and does not grow a flag syntax for branches. The flag form and `--from-json` are mutually exclusive (CLI11 `excludes()`); passing both is a usage error caught before the engine is called.

## 2. Unified Output: `--json` and FTXUI Table

Every command builds one small, explicit **view struct** for its result — not a generic reflection-driven table over arbitrary domain types. With ~10 commands and result shapes that don't repeat, a generic table-from-struct renderer would be more machinery than the surface justifies; YAGNI. Example, for `block inspect`:

```cpp
struct BlockInspectView {
  std::string id;
  std::string version;
  std::string type;
  std::string template_text;
  std::vector<std::pair<std::string, std::string>> defaults;
  std::vector<std::string> tags;
  bool deprecated;
};
```

Two render functions per view struct, in `cli/output.h`/`cli/output.cc`:

```cpp
template <typename View>
std::string ToJson(const View&);      // rfl::json::write

template <typename View>
ftxui::Element ToTable(const View&);  // explicit rows, ftxui::Table
```

`ToJson` is generic (the view structs are exactly the plain, public-field shape `reflect-cpp` serializes directly — this is why results get their own small view structs instead of reusing DTOs: a view struct is shaped for display, e.g. flattening `defaults` into display pairs, where a DTO is shaped for round-tripping). `ToTable` is written by hand per view struct — for a single-entity result (`inspect`, `create`, `publish`) it's a 2-column key/value `ftxui::Table`; for a `list` result it's one row per item, columns matching the view struct's fields. This is the same amount of hand-written code either way (~10 small view structs, ~10 small `ToTable` functions), so no shared "generic list table" abstraction is introduced.

A single entry point ties both together, called by every command's `callback()`:

```cpp
template <typename View>
void PrintResult(const View& view, bool json_mode) {
  if (json_mode) {
    std::cout << ToJson(view) << "\n";
  } else {
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fit(ToTable(view), true));
    ftxui::Render(screen, ToTable(view));
    screen.Print();
    std::cout << "\n";
  }
}
```

### Error Output

Command failures (`Error`/`Result<T>` failure returned by the engine, or a CLI-level usage error like mutually-exclusive flags) go through the same two modes:

- `--json`: a single JSON document to **stdout**, `{"error": {"code": "<ErrorCode name>", "message": "<text>"}}`, and a non-zero exit code. Not split across stdout/stderr — an agent parsing `--json` output reads exactly one stream.
- table mode: a short `Error: <message>` line to **stderr** (not rendered as a table — a single line doesn't benefit from FTXUI framing), and the same non-zero exit code.

Exit code convention: `0` success, `1` engine/domain error (`Error` returned by the engine), `2` CLI usage error (bad flags, mutually-exclusive input, missing required argument). CLI11's native non-zero parse codes are normalized to `2` by `tfe` so shell scripts do not depend on CLI11's internal code table.

## 3. Shell Completion

CLI11 provides parsing, help, subcommands, and validation, but it does not
generate shell completion scripts. `cli/command_catalog.*` therefore defines
the root commands, subcommands, aliases, and options once, while
`cli/completion.*` renders that catalog for Bash, Zsh, and Fish.

The stable root aliases are `b` for `block` and `composition` for `comp`.
The root help footer is also generated from this catalog and contains the
aliases plus representative examples, so help text and completion do not
drift apart.

`tfe completion <shell>` writes only the generated script to stdout. The first
version completes command names, fixed choices, and JSON file paths. It does
not open the ObjectBox store or complete dynamic block/composition IDs; that
can be added later without changing the parser contract.

## 4. Build Wiring

**New directory: `cli/`**, new files `cli/main.cc`, `cli/app.h`/`cli/app.cc` (CLI11 setup, one `Setup*Commands` function per command group, mirroring `text_foundry_cli/application.cc`'s structure since that structure is proven, not because code is shared), `cli/dto.h`/`cli/dto.cc`, `cli/output.h`/`cli/output.cc`.

**`vcpkg.json`** — add:

```json
{
  "name": "cli11",
  "version>=": "2.6.1"
}
```

**Root `CMakeLists.txt`** — new option, placed next to the two existing `TEXTFOUNDRY_ENGINE_BUILD_*` options:

```cmake
option(TEXTFOUNDRY_ENGINE_BUILD_CLI
    "Build tfe, a standalone CLI for exercising the engine directly \
(block/comp/render/validate). Requires TEXTFOUNDRY_ENGINE_BUILD_OBJECTBOX_STORE."
    ON
)
```

FTXUI and CLI11 are resolved from the vcpkg manifest:

```cmake
if(TEXTFOUNDRY_ENGINE_BUILD_CLI AND TEXTFOUNDRY_ENGINE_BUILD_OBJECTBOX_STORE)
  find_package(CLI11 CONFIG REQUIRED)
  find_package(ftxui CONFIG REQUIRED)

  add_subdirectory(cli)
endif()
```

The exact `if()` guard for the "CLI needs ObjectBox store" dependency, and whether a warning is printed when `TEXTFOUNDRY_ENGINE_BUILD_CLI=ON` but `TEXTFOUNDRY_ENGINE_BUILD_OBJECTBOX_STORE=OFF`, is finalized during plan-writing — the constraint itself (CLI silently doesn't build rather than hard-erroring, since both default `ON` and a consumer turning off ObjectBox for their own use of `textfoundry_core` shouldn't be forced to also disable the CLI explicitly) is fixed here.

**`cli/CMakeLists.txt`**:

```cmake
add_executable(tfe
    main.cc
    app.cc
    app.h
    command_catalog.cc
    command_catalog.h
    commands.cc
    commands.h
    completion.cc
    completion.h
    dto.cc
    dto.h
    output.cc
    output.h
)

target_link_libraries(tfe
    PRIVATE
        textfoundry_core
        textfoundry_objectbox_store
        CLI11::CLI11
        ftxui::dom
        ftxui::screen
        reflectcpp::reflectcpp
)

install(TARGETS tfe RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
```

(The FTXUI vcpkg port exposes `ftxui::dom`, `ftxui::screen`, `ftxui::component`, and `ftxui::ftxui`; this CLI links only the `dom` and `screen` targets. The `dom`-module-without-`ScreenInteractive` capability was confirmed against FTXUI's installed headers and examples rather than assumed.)

### Global CLI Options

Mirroring `EngineConfig`'s fields (`tf/engine.h`), since `tfe` constructs an `Engine` directly:

```
    -d, --data <path>     Engine data path (default: platform-appropriate on-disk path, not memory:)
-P, --project <key>   Project key for namespacing (default: "default")
    --strict           Strict mode (fail render on missing params)
    --json              JSON output instead of a table (see Section 2)
```

No `-l/--lang` global flag — `render`'s existing repeatable `-p,--param key=value` mechanism already covers passing a language (or any other) runtime parameter; a dedicated flag for one specific parameter name would special-case something the generic mechanism already handles.

### Why Not Share Code With `text_foundry_cli`

Considered and rejected: extracting a shared CLI-scaffolding library both binaries link. Rejected because the two CLIs' command surfaces only partially overlap (TextFoundry's has AI commands and `tui`; this one is engine-only) and because `text_foundry_cli` is explicitly out of scope for this work per the user's direction — introducing a shared library would mean modifying `text_foundry_cli` to consume it, which is exactly what was ruled out. `tfe`'s CLI setup code will look structurally similar to `text_foundry_cli/application.cc` by design (proven structure) but is independent, duplicated code — acceptable at this scale (~10 subcommands) and consistent with this being a small, standalone tool rather than a shared framework.

## Testing

**File: `TextFoundryEngine/tests/cli_test_main.cc`** (new binary, gated behind `TEXTFOUNDRY_ENGINE_BUILD_CLI`, following the same split rationale as `core_tests`/`ai_tests` in TextFoundry: CLI tests need `textfoundry_objectbox_store` + CLI11 + FTXUI, so they don't belong in the base `core_tests` binary). The test suite must cover at minimum:

- DTO round-trip: a `CompositionDto` with a nested `Conditional` (branches + else) converts to a domain `Composition` whose `validate()` passes, and whose `Renderer::Render()` output differs across two `RenderContext`s — closing the loop from JSON input to rendered text.
- DTO error path: a `Conditional` JSON missing `else_content` converts to a domain value that fails `validate()` with `MissingElseBranch`, surfaced through the CLI's error-output path (JSON error document, non-zero exit).
- `comp create` flag-form and `--from-json` are mutually exclusive (CLI11 usage error, exit code `2`).
- `--json` output for at least one `list` command and one single-entity command parses as valid JSON and matches the expected view struct shape.
- Table-mode output for the same two commands produces non-empty stdout (rendering correctness beyond "non-empty" is not asserted — FTXUI's own layout is not this project's code to test).
- Bash, Zsh, and Fish completion output contains the catalog's canonical commands and options.

## Explicitly Out of Scope

- Any change to `TextFoundry`'s `src/text_foundry_cli`, `text_foundry_gui`, or `textfoundry_ai`.
- AI-assisted commands (generation, revision, slicing, normalization, rewrite) — these depend on `textfoundry_ai`, which stays TextFoundry-only.
- A `tui` subcommand or any interactive (FTXUI `component`-module) surface — `tfe` is non-interactive in this release, per the FTXUI `dom`-vs-`component` distinction already established. FTXUI Component remains the next UI layer if an interactive workflow becomes valuable.
- A generic reflection-driven table renderer — explicit per-command view structs and `ToTable` functions instead (see Section 2).
- Editing an existing `Composition`/`Block` via `--from-json` (only `comp create` gains JSON input in this spec) — `comp` has no `update` command in `tf` today either (publishing a new version is the existing revision model), so there is nothing to extend.
