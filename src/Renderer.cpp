#include "Renderer.h"

#include "PluginLoader.h"
#include "RenderPlayHead.h"

bool Renderer::render(const RenderConfig& config, juce::String& errorOut)
{
    PluginLoader loader;
    auto plugin = loader.load(config.pluginPath, config.sampleRate, 512, errorOut);
    if (!plugin)
        return false;

    if (config.rawState.isNotEmpty())
        PluginLoader::restoreState(*plugin, config.rawState);

    RenderPlayHead playHead(config.bpm, config.sampleRate);
    plugin->setPlayHead(&playHead);

    std::vector<std::unique_ptr<juce::AudioPluginInstance>> fxPlugins;
    for (int i = 0; i < (int)config.fxChain.size(); ++i)
    {
        const auto& fx = config.fxChain[i];
        juce::String fxError;
        auto fxPlugin = loader.load(fx.pluginPath, config.sampleRate, 512, fxError);
        if (!fxPlugin)
        {
            errorOut = "FX[" + juce::String(i) + "] failed to load (" + fx.pluginPath + "): " + fxError;
            return false;
        }
        fxPlugin->setPlayConfigDetails(2, 2, config.sampleRate, 512);
        fxPlugin->prepareToPlay(config.sampleRate, 512);
        if (fx.rawState.isNotEmpty())
            PluginLoader::restoreState(*fxPlugin, fx.rawState);
        fxPlugin->setPlayHead(&playHead);
        fxPlugins.push_back(std::move(fxPlugin));
    }

    juce::WavAudioFormat wavFormat;
    const juce::File outFile(config.outputWav);
    outFile.deleteFile();

    auto stream = std::unique_ptr<juce::OutputStream>(outFile.createOutputStream());
    if (!stream)
    {
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
        errorOut = "Cannot create WAV writer for: " + config.outputWav;
        return false;
    }

    const int blockSize = 512;
    const auto totalSamples = (int64_t)(config.durationSec * config.sampleRate);
    juce::AudioBuffer<float> buffer(2, blockSize);
    int64_t position = 0;

    while (position < totalSamples)
    {
        const int samplesThisBlock = (int)std::min((int64_t)blockSize, totalSamples - position);
        buffer.clear();

        juce::MidiBuffer midiBlock;
        for (const auto& ev : config.midiEvents)
        {
            const auto evSample = (int64_t)(ev.timeSec * config.sampleRate);
            if (evSample >= position && evSample < position + samplesThisBlock)
                midiBlock.addEvent(ev.message, (int)(evSample - position));
        }

        plugin->processBlock(buffer, midiBlock);

        juce::MidiBuffer emptyMidi;
        for (auto& fx : fxPlugins)
            fx->processBlock(buffer, emptyMidi);

        playHead.advance(samplesThisBlock);

        if (!writer->writeFromAudioSampleBuffer(buffer, 0, samplesThisBlock))
        {
            errorOut = "Failed writing audio at sample " + juce::String(position);
            return false;
        }

        position += samplesThisBlock;
    }

    return true;
}
