#pragma once

#include <memory>
#include <ostream>

#include <CLI/CLI.hpp>

#include "../tf/engine.h"

#include "app.h"
#include "output.h"

namespace cli {

struct AppState {
  AppConfig config;
  std::ostream& output;
  std::ostream& errors;
  std::unique_ptr<tf::Engine> engine;
  int result_code = 0;

  tf::Engine& EnsureEngine();
  void Emit(const IdListView& view);
  void Emit(const EntityView& view);
  void Emit(const RenderView& view);
  void Emit(const ValidationView& view);
  void EmitCompletionCandidates(std::vector<std::string> candidates);
  void Fail(const tf::Error& error);
};

void RegisterCommands(CLI::App& app, AppState& state,
                      bool include_dynamic_completion = false);

}  // namespace cli
