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

target_compile_features(tfe PRIVATE cxx_std_23)

target_link_libraries(tfe
    PRIVATE
        textfoundry_core
        textfoundry_objectbox_store
        CLI11::CLI11
        ftxui::dom
        ftxui::screen
        reflectcpp::reflectcpp
)

if(UNIX AND TARGET objectbox)
    set_property(TARGET tfe PROPERTY BUILD_WITH_INSTALL_RPATH OFF)
    set_property(TARGET tfe APPEND PROPERTY
        BUILD_RPATH "$<TARGET_FILE_DIR:objectbox>"
    )
endif()

install(TARGETS tfe RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
