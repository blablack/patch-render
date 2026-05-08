#include <JuceHeader.h>

#include "Renderer.h"

#include <algorithm>
#include <iostream>

static RenderConfig parseConfig(const juce::var& v, juce::String& errorOut)
{
    RenderConfig cfg;
    auto* obj = v.getDynamicObject();
    if (!obj)
    {
        errorOut = "JSON root must be an object";
        return cfg;
    }

    cfg.pluginPath  = obj->getProperty("plugin").toString();
    cfg.rawState    = obj->getProperty("raw_state").toString();
    cfg.outputWav   = obj->getProperty("output").toString();
    cfg.bpm         = (double)obj->getProperty("bpm");
    cfg.sampleRate  = (double)obj->getProperty("sample_rate");
    cfg.durationSec = (double)obj->getProperty("duration");

    if (cfg.pluginPath.isEmpty() || cfg.outputWav.isEmpty() || cfg.durationSec <= 0.0)
    {
        errorOut = "Required fields: plugin, output, duration (> 0)";
        return cfg;
    }

    if (cfg.bpm <= 0.0)       cfg.bpm = 120.0;
    if (cfg.sampleRate <= 0.0) cfg.sampleRate = 44100.0;

    if (const auto* midiArr = obj->getProperty("midi").getArray())
    {
        for (const auto& ev : *midiArr)
        {
            auto* evObj = ev.getDynamicObject();
            if (!evObj) continue;

            const juce::String type = evObj->getProperty("type").toString();
            const int    pitch = (int)evObj->getProperty("pitch");
            const int    vel   = (int)evObj->getProperty("vel");
            const double time  = (double)evObj->getProperty("time");

            juce::MidiMessage msg;
            if (type == "note_on")
                msg = juce::MidiMessage::noteOn(1, pitch, (juce::uint8)vel);
            else if (type == "note_off")
                msg = juce::MidiMessage::noteOff(1, pitch, (juce::uint8)vel);
            else
                continue;

            cfg.midiEvents.push_back({time, msg});
        }

        std::sort(cfg.midiEvents.begin(), cfg.midiEvents.end(),
                  [](const MidiEvent& a, const MidiEvent& b) { return a.timeSec < b.timeSec; });
    }

    if (const auto* fxArr = obj->getProperty("fx_chain").getArray())
    {
        for (const auto& item : *fxArr)
        {
            auto* fxObj = item.getDynamicObject();
            if (!fxObj) continue;
            FxPlugin fx;
            fx.pluginPath = fxObj->getProperty("plugin").toString();
            fx.rawState   = fxObj->getProperty("raw_state").toString();
            if (fx.pluginPath.isNotEmpty())
                cfg.fxChain.push_back(std::move(fx));
        }
    }

    return cfg;
}

int main(int, char**)
{
    juce::ScopedJuceInitialiser_GUI init;

    juce::String input;
    {
        char buf[4096];
        while (std::cin.read(buf, sizeof(buf)) || std::cin.gcount() > 0)
            input += juce::String::fromUTF8(buf, (int)std::cin.gcount());
    }

    juce::var parsed;
    const juce::Result parseResult = juce::JSON::parse(input, parsed);
    if (parseResult.failed())
    {
        std::cerr << "[patch-render] JSON parse error: " << parseResult.getErrorMessage() << "\n";
        return 1;
    }

    juce::String errorOut;
    RenderConfig config = parseConfig(parsed, errorOut);
    if (errorOut.isNotEmpty())
    {
        std::cerr << "[patch-render] Config error: " << errorOut << "\n";
        return 1;
    }

    Renderer renderer;
    if (!renderer.render(config, errorOut))
    {
        std::cerr << "[patch-render] Render failed: " << errorOut << "\n";
        return 3;
    }

    return 0;
}
