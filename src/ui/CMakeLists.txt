add_library(sampler_ui STATIC
    KeyboardRouter.cpp KeyboardRouter.h
    arrange/ArrangeGeometry.h
    arrange/ArrangeLogic.cpp arrange/ArrangeLogic.h
    arrange/ArrangementView.cpp arrange/ArrangementView.h
    arrange/ClipInspector.cpp arrange/ClipInspector.h
    arrange/HeaderBar.cpp arrange/HeaderBar.h
    browser/BrowserPanel.cpp browser/BrowserPanel.h
    browser/BrowserModel.cpp browser/BrowserModel.h
    look/Theme.h look/Fonts.cpp look/Fonts.h look/SamplerLookAndFeel.cpp look/SamplerLookAndFeel.h
    widgets/Toast.cpp widgets/Toast.h
    widgets/WidgetMath.cpp widgets/WidgetMath.h
    widgets/Widgets.cpp widgets/Widgets.h
    widgets/WidgetGallery.cpp widgets/WidgetGallery.h)
target_include_directories(sampler_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
# The UI reads the arrangement through the engine's Tracktion-free facade and edits only through commands.
target_link_libraries(sampler_ui PUBLIC sampler_juce sampler_model sampler_commands sampler_engine sampler_resources)
sampler_target_debuggable(sampler_ui)
sampler_target_warnings(sampler_ui)
