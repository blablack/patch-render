#include "FxChain.h"

bool FxChain::load(const std::vector<FxPlugin>& chain, double sampleRate, int blockSize,
                   juce::AudioPlayHead* playHead, juce::String& errorOut)
{
    for (int i = 0; i < (int)chain.size(); ++i)
    {
        const auto& fx = chain[i];
        juce::String fxError;
        auto plugin = loader_.load(fx.pluginPath, sampleRate, blockSize, fxError, playHead);
        if (!plugin)
        {
            errorOut = "FX[" + juce::String(i) + "] failed to load (" + fx.pluginPath + "): " + fxError;
            return false;
        }
        // PluginLoader::load() configures plugins as 0-in/2-out (instrument), which leaves an
        // effect's input bus disabled and its output silent. Re-open it as stereo in/stereo out.
        plugin->releaseResources();
        auto layout = plugin->getBusesLayout();
        for (auto& bus : layout.inputBuses)
            bus = juce::AudioChannelSet::stereo();
        for (auto& bus : layout.outputBuses)
            bus = juce::AudioChannelSet::stereo();
        if (!plugin->setBusesLayout(layout))
        {
            errorOut = "FX[" + juce::String(i) + "] rejected stereo in/out bus layout (" + fx.pluginPath + ")";
            return false;
        }
        plugin->setPlayConfigDetails(2, 2, sampleRate, blockSize);
        plugin->prepareToPlay(sampleRate, blockSize);
        if (fx.rawState.isNotEmpty())
            PluginLoader::restoreState(*plugin, fx.rawState);
        plugins_.push_back(std::move(plugin));
    }
    return true;
}

void FxChain::process(juce::AudioBuffer<float>& buffer)
{
    juce::MidiBuffer emptyMidi;
    for (auto& fx : plugins_)
        fx->processBlock(buffer, emptyMidi);
}
