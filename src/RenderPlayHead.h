#pragma once

#include <JuceHeader.h>

class RenderPlayHead : public juce::AudioPlayHead
{
public:
    RenderPlayHead(double bpm, double sampleRate) : bpm_(bpm), sampleRate_(sampleRate) {}

    void advance(int numSamples) { samplePosition_ += numSamples; }

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm(bpm_);
        info.setIsPlaying(true);
        info.setIsRecording(false);
        info.setIsLooping(false);
        info.setTimeInSamples(samplePosition_);
        info.setTimeInSeconds(samplePosition_ / sampleRate_);
        info.setPpqPosition((samplePosition_ / sampleRate_) * (bpm_ / 60.0));
        info.setTimeSignature(TimeSignature{4, 4});
        return info;
    }

private:
    double bpm_;
    double sampleRate_;
    int64_t samplePosition_ { 0 };
};
