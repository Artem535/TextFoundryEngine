//
// Engine::FullInit(): wires an Engine to its default ObjectBox-backed
// repositories.
//
// Kept in its own translation unit, separate from engine.cc, so that
// linking textfoundry_core alone -- without textfoundry_objectbox_store --
// never requires resolving ObjectBox symbols. FullInit() is declared (and
// documented) in engine.h and is a normal member of Engine; only its
// out-of-line definition lives here. Static linking pulls in a whole .o
// per referenced symbol, so if this definition stayed in engine.cc, every
// consumer of that .o -- including ones that never call FullInit() --
// would drag in ObjectBox at link time.
//

#include <filesystem>

#include "engine.h"
#include "objectbox-model.h"
#include "objectbox_repository.h"

namespace tf {

void Engine::FullInit() {
  // Ensure the database directory exists
  const bool is_in_memory = config_.default_data_path.starts_with("memory:");
  if (!is_in_memory && !std::filesystem::exists(config_.default_data_path)) {
    std::filesystem::create_directories(config_.default_data_path);
  }

  obx::Options options;
  options.directory(config_.default_data_path);
  options.model(create_obx_model());

  auto store = std::make_shared<obx::Store>(options);
  const std::shared_ptr<IBlockRepository> block_repository =
      std::make_shared<BlockRepository>(store);
  const std::shared_ptr<ICompositionRepository> composition_repository =
      std::make_shared<CompositionRepository>(store);

  SetBlockRepository(block_repository);
  SetCompositionRepository(composition_repository);
}

}  // namespace tf
