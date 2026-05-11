#include "PluginLoader.h"

std::unique_ptr<juce::AudioPluginInstance> PluginLoader::load(
    const juce::String& pluginPath,
    double sampleRate,
    int blockSize,
    juce::String& errorOut,
    juce::AudioPlayHead* playHead)
{
    juce::addDefaultFormatsToManager(formatManager_);

    juce::OwnedArray<juce::PluginDescription> descs;
    for (auto* format : formatManager_.getFormats())
    {
        juce::OwnedArray<juce::PluginDescription> tmp;
        format->findAllTypesForFile(tmp, pluginPath);
        if (!tmp.isEmpty())
        {
            descs.addCopiesOf(tmp);
            break;
        }
    }

    if (descs.isEmpty())
    {
        errorOut = "No plugin found at: " + pluginPath;
        return nullptr;
    }

    auto plugin = formatManager_.createPluginInstance(*descs.getFirst(), sampleRate, blockSize, errorOut);
    if (!plugin)
        return nullptr;

    plugin->setPlayConfigDetails(0, 2, sampleRate, blockSize);
    if (playHead)
        plugin->setPlayHead(playHead);
    plugin->prepareToPlay(sampleRate, blockSize);
    return plugin;
}

void PluginLoader::restoreState(juce::AudioPluginInstance& plugin, const juce::String& base64State)
{
    if (base64State.isEmpty())
        return;
    juce::MemoryOutputStream out;
    juce::Base64::convertFromBase64(out, base64State);
    plugin.setStateInformation(out.getData(), (int)out.getDataSize());
}
