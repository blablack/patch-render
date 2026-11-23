#pragma once

#include "PluginLoader.h"
#include "Renderer.h" // FxPlugin

#include <memory>
#include <vector>

// Serial VST3 effects applied to a stereo buffer after the instrument.
// Shared by the VST3 renderer and the CLAP renderer.
class FxChain
{
public:
    // Loads and prepares every effect. Returns false and fills errorOut on failure.
    bool load(const std::vector<FxPlugin>& chain, double sampleRate, int blockSize,
              juce::AudioPlayHead* playHead, juce::String& errorOut);

    void process(juce::AudioBuffer<float>& buffer);

private:
    PluginLoader loader_;
    std::vector<std::unique_ptr<juce::AudioPluginInstance>> plugins_;
};
