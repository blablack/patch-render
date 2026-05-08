juce_add_console_app(patch-render
    PRODUCT_NAME "patch-render"
)

juce_generate_juce_header(patch-render)

target_compile_features(patch-render PRIVATE cxx_std_17)

target_compile_definitions(patch-render
    PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_LADSPA=0
)

target_sources(patch-render PRIVATE
    Main.cpp
    PluginLoader.cpp
    Renderer.cpp
)

target_link_libraries(patch-render PRIVATE
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_gui_basics
    juce::juce_core
)
