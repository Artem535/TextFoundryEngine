cmake_minimum_required(VERSION 3.20)
project(TextFoundryEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)

include(GNUInstallDirs)
include(FetchContent)
include(CMakePackageConfigHelpers)

set(TEXTFOUNDRY_INSTALL_CMAKEDIR
    "${CMAKE_INSTALL_LIBDIR}/cmake/TextFoundry")

# ObjectBox
FetchContent_Declare(
        objectbox
        GIT_REPOSITORY https://github.com/objectbox/objectbox-c.git
        GIT_TAG v5.1.0
)

FetchContent_MakeAvailable(objectbox)
set(OBJECTBOX_LICENSE_FILE "${objectbox_SOURCE_DIR}/LICENSE.txt")

# Other lib
find_package(ctre CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(reflectcpp CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)


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
        tf/logger.h
        tf/logger.cc
        tf/objectbox_repository.cc
        tf/objectbox_repository.h
        tf/repository_block_cache.cpp
        tf/types.h
        tf/objectbox-model.h
        tf/database_scheme.obx.hpp
        tf/database_scheme.obx.cpp
)

target_include_directories(textfoundry_core
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(textfoundry_core
        PUBLIC
            ctre::ctre
            spdlog::spdlog
            objectbox
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

install(
        DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tf
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING
            PATTERN "*.h"
            PATTERN "*.hpp"
)

if(WIN32)
  set(TEXTFOUNDRY_OBJECTBOX_INSTALL_DIR "${CMAKE_INSTALL_BINDIR}")
else()
  set(TEXTFOUNDRY_OBJECTBOX_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}")
endif()

if(TARGET objectbox)
  install(FILES $<TARGET_FILE:objectbox>
    DESTINATION ${TEXTFOUNDRY_OBJECTBOX_INSTALL_DIR}
  )
endif()

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
