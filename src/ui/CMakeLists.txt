add_library(sampler_ui STATIC
    timeline/TimelineComponent.cpp timeline/TimelineComponent.h timeline/TimelineGeometry.h
    browser/BrowserPanel.cpp browser/BrowserPanel.h
    browser/BrowserModel.cpp browser/BrowserModel.h
    look/Theme.h look/Fonts.cpp look/Fonts.h look/SamplerLookAndFeel.cpp look/SamplerLookAndFeel.h
    widgets/Toast.cpp widgets/Toast.h
    widgets/WidgetMath.cpp widgets/WidgetMath.h
    widgets/Widgets.cpp widgets/Widgets.h
    widgets/WidgetGallery.cpp widgets/WidgetGallery.h)
target_include_directories(sampler_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(sampler_ui PUBLIC sampler_juce sampler_model sampler_resources)
sampler_target_debuggable(sampler_ui)
sampler_target_warnings(sampler_ui)
