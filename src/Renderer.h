#pragma once

#include <JuceHeader.h>
#include <vector>

struct MidiEvent
{
    double timeSec;
    juce::MidiMessage message;
};

struct FxPlugin
{
    juce::String pluginPath;
    juce::String rawState;
};

struct RenderConfig
{
    juce::String pluginPath;
    juce::String rawState;
    double bpm { 120.0 };
    double sampleRate { 44100.0 };
    double durationSec { 4.0 };
    juce::String outputWav;
    std::vector<MidiEvent> midiEvents;
    std::vector<FxPlugin> fxChain;
};

class Renderer
{
public:
    bool render(const RenderConfig& config, juce::String& errorOut);
};
