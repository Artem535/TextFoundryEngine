cmake_minimum_required(VERSION 3.20)
project(TextFoundryEngine VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)

include(GNUInstallDirs)
include(FetchContent)
include(CMakePackageConfigHelpers)

option(TEXTFOUNDRY_ENGINE_BUILD_OBJECTBOX_STORE
    "Build textfoundry_objectbox_store: the ObjectBox-backed repository \
implementation and Engine::FullInit(). Disable to consume just \
textfoundry_core (Block/Composition/Version/Renderer, no storage backend, \
no ObjectBox fetch/build) -- e.g. for a consumer that supplies its own \
IBlockRepository/ICompositionRepository."
    ON
)

set(TEXTFOUNDRY_INSTALL_CMAKEDIR
    "${CMAKE_INSTALL_LIBDIR}/cmake/TextFoundry")

# Other lib
find_package(ctre CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(reflectcpp CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

# ===== textfoundry_core: domain model (Block/Composition/Version/Renderer),
# no storage backend =====
add_library(textfoundry_core STATIC
        tf/engine.cc
        tf/engine.h
        tf/block_generation.h
        tf/block.cc
        tf/block.h
        tf/block_ref.cc
        tf/block_ref.h
        tf/fragment.cc
        tf/fragment.h
        tf/composition.cc
        tf/composition.h
        tf/renderer.cc
        tf/renderer.h
        tf/block_type.hpp
        tf/version.h
        tf/error.h
        tf/types.h
        tf/logger.h
        tf/logger.cc
        tf/repository_block_cache.cpp
        tf/repository_block_cache.h
)

target_include_directories(textfoundry_core
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(textfoundry_core PUBLIC cxx_std_23)

target_link_libraries(textfoundry_core
        PUBLIC
            ctre::ctre
            spdlog::spdlog
            reflectcpp::reflectcpp
            fmt::fmt
)

set_target_properties(textfoundry_core PROPERTIES EXPORT_NAME Core)

install(
        TARGETS textfoundry_core
        EXPORT TextFoundryTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# ===== textfoundry_objectbox_store: ObjectBox-backed IBlockRepository /
# ICompositionRepository, plus Engine::FullInit() which wires them up.
# Only this target (and the tests, which exercise FullInit()) ever touches
# ObjectBox -- consumers that disable TEXTFOUNDRY_ENGINE_BUILD_OBJECTBOX_STORE
# never fetch or build it. =====
if(TEXTFOUNDRY_ENGINE_BUILD_OBJECTBOX_STORE)
  FetchContent_Declare(
          objectbox
          GIT_REPOSITORY https://github.com/objectbox/objectbox-c.git
          GIT_TAG v5.1.0
  )
  FetchContent_MakeAvailable(objectbox)
  set(OBJECTBOX_LICENSE_FILE "${objectbox_SOURCE_DIR}/LICENSE.txt")

  add_library(textfoundry_objectbox_store STATIC
          tf/engine_objectbox_init.cc
          tf/objectbox_repository.cc
          tf/objectbox_repository.h
          tf/objectbox-model.h
          tf/database_scheme.obx.hpp
          tf/database_scheme.obx.cpp
          tf/obx_utils.hpp
  )

  target_include_directories(textfoundry_objectbox_store
          PUBLIC
              $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
              $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )

  target_compile_features(textfoundry_objectbox_store PUBLIC cxx_std_23)

  target_link_libraries(textfoundry_objectbox_store
          PUBLIC
              textfoundry_core
              objectbox
  )

  set_target_properties(textfoundry_objectbox_store PROPERTIES EXPORT_NAME ObjectBoxStore)

  install(
          TARGETS textfoundry_objectbox_store
          EXPORT TextFoundryTargets
          ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
          RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
          INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )

  if(WIN32)
    set(TEXTFOUNDRY_OBJECTBOX_INSTALL_DIR "${CMAKE_INSTALL_BINDIR}")
  else()
    set(TEXTFOUNDRY_OBJECTBOX_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}")
  endif()

  install(FILES $<TARGET_FILE:objectbox>
    DESTINATION ${TEXTFOUNDRY_OBJECTBOX_INSTALL_DIR}
  )

  if(EXISTS "${OBJECTBOX_LICENSE_FILE}")
    install(FILES "${OBJECTBOX_LICENSE_FILE}"
      DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/${PROJECT_NAME}
      RENAME objectbox-LICENSE.txt
    )
  endif()
endif()

install(
        DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tf
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING
            PATTERN "*.h"
            PATTERN "*.hpp"
)

enable_testing()
add_subdirectory(tests)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/TextFoundryConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/TextFoundryConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/TextFoundryConfig.cmake"
    INSTALL_DESTINATION "${TEXTFOUNDRY_INSTALL_CMAKEDIR}"
)

install(
    EXPORT TextFoundryTargets
    FILE TextFoundryTargets.cmake
    NAMESPACE TextFoundry::
    DESTINATION "${TEXTFOUNDRY_INSTALL_CMAKEDIR}"
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/TextFoundryConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/TextFoundryConfigVersion.cmake"
    DESTINATION "${TEXTFOUNDRY_INSTALL_CMAKEDIR}"
)
