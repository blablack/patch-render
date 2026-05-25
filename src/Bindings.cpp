#include <pybind11/pybind11.h>

#include <algorithm>
#include <mutex>
#include <optional>
#include <stdexcept>

#include <JuceHeader.h>

#include "ClapRenderer.h"
#include "Renderer.h"

#include <clap/clap.h>
#include <clap/factory/plugin-factory.h>
#include <dlfcn.h>

namespace py = pybind11;

static std::optional<juce::ScopedJuceInitialiser_GUI> g_juceInit;
static std::mutex g_renderMutex;

static ClapRenderConfig clapConfigFromDict(const py::dict& d)
{
    ClapRenderConfig cfg;
    cfg.pluginPath  = d["plugin"].cast<std::string>();
    cfg.pluginId    = d["plugin_id"].cast<std::string>();
    cfg.outputWav   = d["output"].cast<std::string>();
    cfg.durationSec = d["duration"].cast<double>();

    if (d.contains("bpm"))         cfg.bpm        = d["bpm"].cast<double>();
    if (d.contains("sample_rate")) cfg.sampleRate  = d["sample_rate"].cast<double>();
    if (d.contains("raw_state"))   cfg.rawState   = d["raw_state"].cast<std::string>();

    if (cfg.pluginPath.empty() || cfg.pluginId.empty() || cfg.outputWav.empty() || cfg.durationSec <= 0.0)
        throw std::invalid_argument("Required fields: plugin, plugin_id, output, duration (> 0)");

    if (cfg.bpm <= 0.0)        cfg.bpm = 120.0;
    if (cfg.sampleRate <= 0.0) cfg.sampleRate = 48000.0;

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

    return cfg;
}

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

    // Reset JUCE during Python's atexit phase, before module unload, to avoid
    // a crash in shutdownJuce_GUI() when the optional's destructor runs too late.
    py::module_::import("atexit").attr("register")(
        py::cpp_function([] { g_juceInit.reset(); }));

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

    m.def(
        "list_clap_plugins",
        [](const std::string& pluginPath) -> py::list
        {
            py::list result;

            void* lib = dlopen(pluginPath.c_str(), RTLD_LOCAL | RTLD_LAZY);
            if (!lib)
                throw std::runtime_error(std::string("dlopen failed: ") + dlerror());

            auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry"));
            if (!entry) { dlclose(lib); throw std::runtime_error("No clap_entry in: " + pluginPath); }

            if (!entry->init(pluginPath.c_str())) { dlclose(lib); throw std::runtime_error("clap_entry.init() failed"); }

            auto* factory = static_cast<const clap_plugin_factory_t*>(
                entry->get_factory(CLAP_PLUGIN_FACTORY_ID));

            if (factory)
            {
                const uint32_t count = factory->get_plugin_count(factory);
                for (uint32_t i = 0; i < count; ++i)
                {
                    const auto* desc = factory->get_plugin_descriptor(factory, i);
                    if (!desc) continue;
                    py::dict info;
                    info["id"]          = std::string(desc->id   ? desc->id   : "");
                    info["name"]        = std::string(desc->name ? desc->name : "");
                    info["vendor"]      = std::string(desc->vendor ? desc->vendor : "");
                    info["description"] = std::string(desc->description ? desc->description : "");
                    result.append(info);
                }
            }

            entry->deinit();
            dlclose(lib);
            return result;
        },
        py::arg("plugin_path"));

    m.def(
        "render_clap",
        [](py::dict payload)
        {
            ClapRenderConfig cfg = clapConfigFromDict(payload);
            py::gil_scoped_release release;
            std::lock_guard<std::mutex> lock(g_renderMutex);
            std::string errorOut;
            ClapRenderer renderer;
            if (!renderer.render(cfg, errorOut))
                throw std::runtime_error(errorOut);
        },
        py::arg("payload"));
}
