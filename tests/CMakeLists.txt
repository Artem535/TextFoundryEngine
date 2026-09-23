find_package(doctest CONFIG REQUIRED)

if(NOT TARGET textfoundry_objectbox_store)
  message(STATUS
    "textfoundry_objectbox_store not built "
    "(TEXTFOUNDRY_ENGINE_BUILD_OBJECTBOX_STORE=OFF): skipping core_tests, "
    "which exercises Engine::FullInit() via EngineTestFixture."
  )
  return()
endif()

add_executable(core_tests
        test_main.cc
)

target_link_libraries(core_tests textfoundry_core textfoundry_objectbox_store doctest::doctest)

if(UNIX AND TARGET objectbox)
  set_property(TARGET core_tests PROPERTY BUILD_WITH_INSTALL_RPATH OFF)
  set_property(TARGET core_tests APPEND PROPERTY
    BUILD_RPATH "$<TARGET_FILE_DIR:objectbox>"
  )
endif()

add_test(NAME core_tests COMMAND core_tests)

if(TEXTFOUNDRY_ENGINE_BUILD_CLI)
  find_package(CLI11 CONFIG REQUIRED)
  find_package(ftxui CONFIG REQUIRED)

  add_executable(cli_tests
          cli_test_main.cc
          cli_dto_test.cc
          cli_completion_test.cc
          cli_output_test.cc
          cli_app_test.cc
          ../cli/command_catalog.cc
          ../cli/completion.cc
          ../cli/dto.cc
          ../cli/output.cc
          ../cli/app.cc
          ../cli/commands.cc
  )

  target_compile_features(cli_tests PRIVATE cxx_std_23)
  target_link_libraries(cli_tests
          PRIVATE
            textfoundry_core
            textfoundry_objectbox_store
            CLI11::CLI11
            ftxui::dom
            ftxui::screen
            reflectcpp::reflectcpp
            doctest::doctest
  )

  if(UNIX AND TARGET objectbox)
    set_property(TARGET cli_tests PROPERTY BUILD_WITH_INSTALL_RPATH OFF)
    set_property(TARGET cli_tests APPEND PROPERTY
      BUILD_RPATH "$<TARGET_FILE_DIR:objectbox>"
    )
  endif()

  add_test(NAME cli_tests COMMAND cli_tests)

  add_test(
    NAME tfe_smoke
    COMMAND ${CMAKE_COMMAND}
      -DTFE=$<TARGET_FILE:tfe>
      -DDATA=${CMAKE_CURRENT_BINARY_DIR}/tfe-smoke-data
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tfe_smoke.cmake
  )
endif()

if(WIN32 AND TARGET objectbox)
  set_tests_properties(core_tests PROPERTIES
    ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:$<TARGET_FILE_DIR:objectbox>;PATH=path_list_prepend:${CMAKE_BINARY_DIR}/bin;PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin"
  )
endif()
