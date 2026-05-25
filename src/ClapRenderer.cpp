#include "ClapRenderer.h"

#include "ClapHost.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/state.h>
#include <clap/factory/plugin-factory.h>
#include <JuceHeader.h>
#include <dlfcn.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

//==============================================================================
// Memory-backed istream for state loading
//==============================================================================
struct MemStream
{
    const uint8_t* data;
    uint64_t       size;
    uint64_t       pos;
};

static int64_t CLAP_ABI streamRead(const clap_istream_t* s, void* buf, uint64_t len)
{
    auto* ms      = static_cast<MemStream*>(s->ctx);
    uint64_t avail = ms->size - ms->pos;
    uint64_t n     = std::min(len, avail);
    memcpy(buf, ms->data + ms->pos, n);
    ms->pos += n;
    return (int64_t)n;
}

//==============================================================================
// Output event list: silently discard all plugin-generated events
//==============================================================================
static bool CLAP_ABI outTryPush(const clap_output_events_t*, const clap_event_header_t*)
{
    return true; // accepted (discarded)
}

//==============================================================================
// Input event list backed by a flat vector of note events
//==============================================================================
struct InputEventList
{
    std::vector<clap_event_note_t> events;
    clap_input_events_t            list;

    InputEventList()
    {
        list.ctx  = this;
        list.size = [](const clap_input_events_t* l) CLAP_ABI -> uint32_t {
            return (uint32_t)static_cast<InputEventList*>(l->ctx)->events.size();
        };
        list.get = [](const clap_input_events_t* l, uint32_t i) CLAP_ABI -> const clap_event_header_t* {
            return &static_cast<InputEventList*>(l->ctx)->events[i].header;
        };
    }

    void addNote(bool on, int key, double vel, uint32_t offset)
    {
        clap_event_note_t ev{};
        ev.header.size     = sizeof(clap_event_note_t);
        ev.header.time     = offset;
        ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.header.type     = on ? (uint16_t)CLAP_EVENT_NOTE_ON : (uint16_t)CLAP_EVENT_NOTE_OFF;
        ev.header.flags    = 0;
        ev.note_id         = -1;
        ev.port_index      = 0;
        ev.channel         = 0;
        ev.key             = (int16_t)key;
        ev.velocity        = vel;
        events.push_back(ev);
    }
};

} // namespace

//==============================================================================
// ClapRenderer::render
//==============================================================================
bool ClapRenderer::render(const ClapRenderConfig& config, std::string& errorOut)
{
    // --- Load .clap shared library ---
    void* lib = dlopen(config.pluginPath.c_str(), RTLD_LOCAL | RTLD_LAZY);
    if (!lib)
    {
        errorOut = std::string("dlopen failed: ") + dlerror();
        return false;
    }

    auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry"));
    if (!entry)
    {
        dlclose(lib);
        errorOut = "No clap_entry symbol in: " + config.pluginPath;
        return false;
    }

    if (!entry->init(config.pluginPath.c_str()))
    {
        dlclose(lib);
        errorOut = "clap_entry.init() failed for: " + config.pluginPath;
        return false;
    }

    auto* factory = static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!factory)
    {
        entry->deinit();
        dlclose(lib);
        errorOut = "No plugin factory in: " + config.pluginPath;
        return false;
    }

    // --- Find plugin by ID ---
    ClapHost host;
    const clap_plugin_t* plugin = nullptr;
    const uint32_t count = factory->get_plugin_count(factory);
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto* desc = factory->get_plugin_descriptor(factory, i);
        if (desc && config.pluginId == desc->id)
        {
            plugin = factory->create_plugin(factory, host.get(), desc->id);
            break;
        }
    }

    if (!plugin)
    {
        entry->deinit();
        dlclose(lib);
        errorOut = "Plugin ID not found: " + config.pluginId;
        return false;
    }

    if (!plugin->init(plugin))
    {
        plugin->destroy(plugin);
        entry->deinit();
        dlclose(lib);
        errorOut = "plugin->init() failed";
        return false;
    }

    // --- Query audio output port count and channel count ---
    uint32_t outChannels = 2; // default: stereo
    auto* audioPorts = static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    if (audioPorts && audioPorts->count(plugin, false) > 0)
    {
        clap_audio_port_info_t info{};
        if (audioPorts->get(plugin, 0, false, &info))
            outChannels = info.channel_count;
    }
    if (outChannels < 1) outChannels = 2;

    const int blockSize = 512;
    if (!plugin->activate(plugin, config.sampleRate, 1, (uint32_t)blockSize))
    {
        plugin->destroy(plugin);
        entry->deinit();
        dlclose(lib);
        errorOut = "plugin->activate() failed";
        return false;
    }

    // --- Load state ---
    if (!config.rawState.empty())
    {
        auto* stateExt = static_cast<const clap_plugin_state_t*>(
            plugin->get_extension(plugin, CLAP_EXT_STATE));
        if (stateExt)
        {
            juce::MemoryBlock decoded;
            juce::MemoryOutputStream mos(decoded, false);
            juce::Base64::convertFromBase64(mos, juce::String(config.rawState));
            mos.flush();
            MemStream ms { static_cast<const uint8_t*>(decoded.getData()),
                           (uint64_t)decoded.getSize(), 0 };
            clap_istream_t stream { &ms, streamRead };
            stateExt->load(plugin, &stream);
        }
    }

    if (!plugin->start_processing(plugin))
    {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        entry->deinit();
        dlclose(lib);
        errorOut = "plugin->start_processing() failed";
        return false;
    }

    // --- Open WAV output ---
    juce::WavAudioFormat wavFormat;
    const juce::File outFile(juce::String(config.outputWav));
    outFile.deleteFile();
    auto stream = std::unique_ptr<juce::OutputStream>(outFile.createOutputStream());
    if (!stream)
    {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        entry->deinit();
        dlclose(lib);
        errorOut = "Cannot write to: " + config.outputWav;
        return false;
    }

    const auto writerOptions = juce::AudioFormatWriterOptions{}
        .withSampleRate(config.sampleRate)
        .withNumChannels(2)
        .withBitsPerSample(24);
    auto writer = wavFormat.createWriterFor(stream, writerOptions);
    if (!writer)
    {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        entry->deinit();
        dlclose(lib);
        errorOut = "Cannot create WAV writer for: " + config.outputWav;
        return false;
    }

    // --- Allocate per-channel audio buffers ---
    std::vector<std::vector<float>> outBufs(outChannels, std::vector<float>(blockSize, 0.0f));
    std::vector<float*> outPtrs(outChannels);
    for (uint32_t c = 0; c < outChannels; ++c)
        outPtrs[c] = outBufs[c].data();

    clap_audio_buffer_t audioOut{};
    audioOut.data32        = outPtrs.data();
    audioOut.channel_count = outChannels;

    clap_audio_buffer_t audioIn{};  // synths have no audio input
    audioIn.data32        = nullptr;
    audioIn.channel_count = 0;

    // Transport: constant BPM, playing from the start
    clap_event_transport_t transport{};
    transport.header.size     = sizeof(clap_event_transport_t);
    transport.header.time     = 0;
    transport.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    transport.header.type     = (uint16_t)CLAP_EVENT_TRANSPORT;
    transport.header.flags    = 0;
    transport.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_IS_PLAYING |
                      CLAP_TRANSPORT_HAS_BEATS_TIMELINE | CLAP_TRANSPORT_HAS_SECONDS_TIMELINE |
                      CLAP_TRANSPORT_HAS_TIME_SIGNATURE;
    transport.tempo      = config.bpm;
    transport.tempo_inc  = 0.0;
    transport.tsig_num   = 4;
    transport.tsig_denom = 4;

    clap_output_events_t outEvs{};
    outEvs.ctx      = nullptr;
    outEvs.try_push = outTryPush;

    const auto totalSamples = (int64_t)(config.durationSec * config.sampleRate);
    int64_t position = 0;

    // --- Render loop ---
    while (position < totalSamples)
    {
        const uint32_t framesThisBlock =
            (uint32_t)std::min((int64_t)blockSize, totalSamples - position);

        for (auto& ch : outBufs)
            std::fill(ch.begin(), ch.begin() + framesThisBlock, 0.0f);

        const double secPerSample = 1.0 / config.sampleRate;
        const double bpsPerSample = config.bpm / 60.0 / config.sampleRate;

        transport.song_pos_seconds =
            (clap_sectime)((double)position * secPerSample * CLAP_SECTIME_FACTOR);
        transport.song_pos_beats =
            (clap_beattime)((double)position * bpsPerSample * CLAP_BEATTIME_FACTOR);

        InputEventList inEvs;
        for (const auto& ev : config.midiEvents)
        {
            const int64_t evSample = (int64_t)(ev.timeSec * config.sampleRate);
            if (evSample >= position && evSample < position + framesThisBlock)
            {
                const bool on  = ev.message.isNoteOn();
                const int key  = ev.message.getNoteNumber();
                const double v = ev.message.getFloatVelocity();
                inEvs.addNote(on, key, v, (uint32_t)(evSample - position));
            }
        }

        clap_process_t proc{};
        proc.steady_time         = position;
        proc.frames_count        = framesThisBlock;
        proc.transport           = &transport;
        proc.audio_inputs        = &audioIn;
        proc.audio_outputs       = &audioOut;
        proc.audio_inputs_count  = 0;
        proc.audio_outputs_count = 1;
        proc.in_events           = &inEvs.list;
        proc.out_events          = &outEvs;

        plugin->process(plugin, &proc);

        // Downmix to stereo if needed (take first two channels)
        juce::AudioBuffer<float> buf(2, (int)framesThisBlock);
        buf.copyFrom(0, 0, outPtrs[0], (int)framesThisBlock);
        buf.copyFrom(1, 0, outPtrs[std::min(1u, outChannels - 1)], (int)framesThisBlock);

        if (!writer->writeFromAudioSampleBuffer(buf, 0, (int)framesThisBlock))
        {
            errorOut = "Failed writing audio at sample " + std::to_string(position);
            break;
        }

        position += framesThisBlock;
    }

    // --- Cleanup (always reached) ---
    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();
    dlclose(lib);

    return errorOut.empty();
}
