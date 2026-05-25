#pragma once

#include "Renderer.h" // for MidiEvent

#include <string>
#include <vector>

struct ClapRenderConfig
{
    std::string pluginPath;
    std::string pluginId;
    std::string outputWav;
    std::string rawState;     // base64-encoded clap_plugin_state bytes
    double bpm        { 120.0 };
    double sampleRate { 48000.0 };
    double durationSec{ 4.0 };
    std::vector<MidiEvent> midiEvents;
};

class ClapRenderer
{
public:
    bool render(const ClapRenderConfig& config, std::string& errorOut);
};
