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

if(WIN32 AND TARGET objectbox)
  set_tests_properties(core_tests PROPERTIES
    ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:$<TARGET_FILE_DIR:objectbox>;PATH=path_list_prepend:${CMAKE_BINARY_DIR}/bin;PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin"
  )
endif()
