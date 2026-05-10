#include <pybind11/pybind11.h>

#include <algorithm>
#include <mutex>
#include <optional>
#include <stdexcept>

#include <JuceHeader.h>

#include "Renderer.h"

namespace py = pybind11;

static std::optional<juce::ScopedJuceInitialiser_GUI> g_juceInit;
static std::mutex g_renderMutex;

static RenderConfig configFromDict(const py::dict& d)
{
    RenderConfig cfg;
    cfg.pluginPath  = juce::String(d["plugin"].cast<std::string>());
    cfg.outputWav   = juce::String(d["output"].cast<std::string>());
    cfg.durationSec = d["duration"].cast<double>();

    if (d.contains("bpm"))         cfg.bpm        = d["bpm"].cast<double>();
    if (d.contains("sample_rate")) cfg.sampleRate  = d["sample_rate"].cast<double>();
    if (d.contains("raw_state"))   cfg.rawState   = juce::String(d["raw_state"].cast<std::string>());

    if (cfg.pluginPath.isEmpty() || cfg.outputWav.isEmpty() || cfg.durationSec <= 0.0)
        throw std::invalid_argument("Required fields: plugin, output, duration (> 0)");

    if (cfg.bpm <= 0.0)        cfg.bpm = 120.0;
    if (cfg.sampleRate <= 0.0) cfg.sampleRate = 44100.0;

    if (d.contains("midi") && !d["midi"].is_none())
    {
        for (const auto& ev : d["midi"].cast<py::list>())
        {
            auto evd  = ev.cast<py::dict>();
            std::string type = evd["type"].cast<std::string>();
            int pitch = evd["pitch"].cast<int>();
            int vel   = evd["vel"].cast<int>();
            double t  = evd["time"].cast<double>();

            juce::MidiMessage msg;
            if (type == "note_on")
                msg = juce::MidiMessage::noteOn(1, pitch, (juce::uint8)vel);
            else if (type == "note_off")
                msg = juce::MidiMessage::noteOff(1, pitch, (juce::uint8)vel);
            else
                continue;

            cfg.midiEvents.push_back({t, msg});
        }

        std::sort(cfg.midiEvents.begin(), cfg.midiEvents.end(),
                  [](const MidiEvent& a, const MidiEvent& b) { return a.timeSec < b.timeSec; });
    }

    if (d.contains("fx_chain") && !d["fx_chain"].is_none())
    {
        for (const auto& item : d["fx_chain"].cast<py::list>())
        {
            auto fxd = item.cast<py::dict>();
            FxPlugin fx;
            fx.pluginPath = juce::String(fxd["plugin"].cast<std::string>());
            if (fxd.contains("raw_state"))
                fx.rawState = juce::String(fxd["raw_state"].cast<std::string>());
            if (fx.pluginPath.isNotEmpty())
                cfg.fxChain.push_back(std::move(fx));
        }
    }

    return cfg;
}

PYBIND11_MODULE(patch_render, m)
{
    g_juceInit.emplace();

    m.def(
        "render",
        [](py::dict payload)
        {
            RenderConfig cfg = configFromDict(payload);
            py::gil_scoped_release release;
            std::lock_guard<std::mutex> lock(g_renderMutex);
            juce::String errorOut;
            Renderer renderer;
            if (!renderer.render(cfg, errorOut))
                throw std::runtime_error(errorOut.toStdString());
        },
        py::arg("payload"));
}
