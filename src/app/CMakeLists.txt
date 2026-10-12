# The window and the controller behind it, as a library so component tests can drive them without a process of their own.
add_library(sampler_app_core STATIC
    AppController.cpp AppController.h
    MainComponent.cpp MainComponent.h
    MainWindow.cpp MainWindow.h
    RecordingController.cpp RecordingController.h
    RecordingPolicy.cpp RecordingPolicy.h)
target_include_directories(sampler_app_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(sampler_app_core PUBLIC sampler_engine sampler_io sampler_commands sampler_model sampler_ui sampler_juce)
sampler_target_debuggable(sampler_app_core)
sampler_target_warnings(sampler_app_core)

# The Sampler app.
juce_add_gui_app(Sampler PRODUCT_NAME "Sampler")
target_sources(Sampler PRIVATE Main.cpp)
target_compile_definitions(Sampler PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Sampler,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(Sampler PRIVATE sampler_app_core)
sampler_target_debuggable(Sampler)
sampler_target_warnings(Sampler)
