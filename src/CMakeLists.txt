pybind11_add_module(patch_render MODULE
    Bindings.cpp
    PluginLoader.cpp
    Renderer.cpp
)

# Inject JUCE target properties (sets JUCE_GENERATED_SOURCES_DIRECTORY).
# _juce_initialise_target is private but stable across JUCE 7/8 (pinned 8.0.12).
_juce_initialise_target(patch_render PRODUCT_NAME "patch_render")
juce_generate_juce_header(patch_render)

target_compile_features(patch_render PRIVATE cxx_std_17)

target_compile_definitions(patch_render PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_LADSPA=0
)

target_link_libraries(patch_render PRIVATE
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_gui_basics
    juce::juce_core
)

install(TARGETS patch_render DESTINATION .)
