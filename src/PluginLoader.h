#pragma once

#include <JuceHeader.h>

class PluginLoader
{
public:
    std::unique_ptr<juce::AudioPluginInstance> load(
        const juce::String& pluginPath,
        double sampleRate,
        int blockSize,
        juce::String& errorOut,
        juce::AudioPlayHead* playHead = nullptr);

    static void restoreState(juce::AudioPluginInstance& plugin, const juce::String& base64State);

private:
    juce::AudioPluginFormatManager formatManager_;
};
