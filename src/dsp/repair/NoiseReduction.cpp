// SPDX-License-Identifier: AGPL-3.0-only
#include "dsp/repair/NoiseReduction.h"
#include "dsp/effects/BuiltInEffects.h"
#include "dsp/spectral/Stft.h"

#include <array>
#include <cmath>

namespace mxj::dsp
{
    //==============================================================================
    float NoiseProfile::powerAt (int channel, double hz) const
    {
        if (! isValid())
            return 0.0f;

        const auto& p = power[(size_t) juce::jlimit (0, (int) power.size() - 1, channel)];
        const auto pos = hz * fftSize / sampleRate;
        const auto i = (int) std::floor (pos);
        if (i < 0)
            return p.front();
        if (i >= (int) p.size() - 1)
            return p.back();

        const auto t = (float) (pos - i);
        return p[(size_t) i] + (p[(size_t) i + 1] - p[(size_t) i]) * t;
    }

    NoiseProfile NoiseProfile::capture (const AudioSequence& audio, const WorkControl& control)
    {
        NoiseProfile profile;
        const auto order = stft::orderForRate (audio.getSampleRate());
        auto power = stft::averagePower (audio, { 0, audio.getNumFrames() }, order, control);
        if (power.empty() || control.cancelled())
            return profile;

        profile.sampleRate = audio.getSampleRate();
        profile.fftSize = 1 << order;
        profile.power = std::move (power);
        profile.seconds = audio.getLengthInSeconds();
        return profile;
    }

    namespace noisePrint
    {
        namespace
        {
            std::mutex lock;
            std::shared_ptr<const NoiseProfile> current;
        }

        void set (std::shared_ptr<const NoiseProfile> p)
        {
            const std::scoped_lock l (lock);
            current = std::move (p);
        }

        std::shared_ptr<const NoiseProfile> get()
        {
            const std::scoped_lock l (lock);
            return current;
        }
    }

    //==============================================================================
    namespace
    {
        constexpr int maxChannels = 8;

        /** Base of the effects that work on the spectrum while the audio plays:
            collects windows of the input, changes their spectrum and adds them
            back together. The output lags one window behind the input. Real-time
            safe after prepare(). */
        class StreamingSpectralProcessor : public EffectProcessor
        {
        public:
            using EffectProcessor::EffectProcessor;

            void prepare (double sampleRate, int, int numChannels) override
            {
                rate = sampleRate;
                channels = juce::jlimit (1, maxChannels, numChannels);
                fft = std::make_unique<RealFft> (stft::orderForRate (sampleRate));
                size = fft->getSize();
                hop = size / 4;
                numBins = fft->getNumBins();
                window = hannWindow (size);
                energy = 0.0f;
                for (auto w : window)
                    energy += w * w;

                for (auto& c : state)
                {
                    c.input.assign ((size_t) size, 0.0f);
                    c.accumulator.assign ((size_t) size, 0.0f);
                    c.queue.assign ((size_t) hop, 0.0f);
                }

                frame.assign ((size_t) size, 0.0f);
                bins.assign ((size_t) numBins, {});
                prepareSpectral();
                reset();
            }

            void reset() override
            {
                for (auto& c : state)
                {
                    std::fill (c.input.begin(), c.input.end(), 0.0f);
                    std::fill (c.accumulator.begin(), c.accumulator.end(), 0.0f);
                    std::fill (c.queue.begin(), c.queue.end(), 0.0f);
                }

                count = 0;
                resetSpectral();
            }

            int getLatencyFrames() const override { return size; }

            void process (float* const* data, int numChannels, int numFrames) override
            {
                if (fft == nullptr)
                    return;

                numChannels = juce::jmin (numChannels, channels);
                beginBlock();

                for (int i = 0; i < numFrames; ++i)
                {
                    for (int c = 0; c < numChannels; ++c)
                    {
                        auto& s = state[(size_t) c];
                        s.input[(size_t) (size - hop + count)] = data[c][i];
                        data[c][i] = s.queue[(size_t) count];
                    }

                    if (++count == hop)
                    {
                        count = 0;
                        for (int c = 0; c < numChannels; ++c)
                            processWindow (c);
                    }
                }
            }

        protected:
            /** Allocate per-bin state (prepare time). */
            virtual void prepareSpectral() {}
            virtual void resetSpectral() {}

            /** Read the parameters once per block. */
            virtual void beginBlock() {}

            /** Change the spectrum of one window of one channel. */
            virtual void processSpectrum (int channel, std::complex<float>* spectrum) = 0;

            double rate = 48000.0;
            int channels = 1, size = 2048, hop = 512, numBins = 1025;
            float energy = 1.0f;

        private:
            void processWindow (int c)
            {
                auto& s = state[(size_t) c];
                for (int k = 0; k < size; ++k)
                    frame[(size_t) k] = s.input[(size_t) k] * window[(size_t) k];

                fft->forward (frame.data(), bins.data());
                processSpectrum (c, bins.data());
                bins[0] = { bins[0].real(), 0.0f };
                bins[(size_t) numBins - 1] = { bins[(size_t) numBins - 1].real(), 0.0f };
                fft->inverse (bins.data(), frame.data());

                const float norm = 1.0f / 1.5f;
                for (int k = 0; k < size; ++k)
                    s.accumulator[(size_t) k] += frame[(size_t) k] * window[(size_t) k] * norm;

                std::copy (s.accumulator.begin(), s.accumulator.begin() + hop, s.queue.begin());
                std::copy (s.accumulator.begin() + hop, s.accumulator.end(), s.accumulator.begin());
                std::fill (s.accumulator.end() - hop, s.accumulator.end(), 0.0f);
                std::copy (s.input.begin() + hop, s.input.end(), s.input.begin());
            }

            struct ChannelState
            {
                std::vector<float> input, accumulator, queue;
            };

            std::unique_ptr<RealFft> fft;
            std::vector<float> window, frame;
            std::vector<std::complex<float>> bins;
            std::array<ChannelState, maxChannels> state;
            int count = 0;
        };

        //==============================================================================
        /** Spectral gate shared by both noise reduction effects: every frequency
            whose level stays under the noise level plus a margin is turned down,
            with a slow release in time and smoothing across frequencies against
            "musical noise". */
        class NoiseGateProcessor : public StreamingSpectralProcessor
        {
        public:
            enum { reduction, sensitivity, smoothing, release, removedOnly, adaptTime };

            NoiseGateProcessor (ParameterValuesPtr v, bool usesProfile)
                : StreamingSpectralProcessor (std::move (v)), profileMode (usesProfile) {}

        protected:
            void prepareSpectral() override
            {
                for (auto& c : bands)
                {
                    c.noise.assign ((size_t) numBins, 0.0f);
                    c.smoothed.assign ((size_t) numBins, 0.0f);
                    c.slow.assign ((size_t) numBins, 0.0f);
                    c.gainDb.assign ((size_t) numBins, 0.0f);
                    c.currentMin.assign ((size_t) numBins, 0.0f);
                    c.subMins.assign ((size_t) (numBins * subWindows), 0.0f);
                }

                gains.assign ((size_t) numBins, 0.0f);
                signal.assign ((size_t) numBins, false);
                lonely.assign ((size_t) numBins, false);
                hasNoise = false;

                if (profileMode)
                {
                    profile = noisePrint::get();
                    if (profile != nullptr && profile->isValid())
                    {
                        hasNoise = true;
                        const auto binHz = rate / size;
                        for (int c = 0; c < maxChannels; ++c)
                            for (int k = 0; k < numBins; ++k)
                                bands[(size_t) c].noise[(size_t) k] = profile->powerAt (c, k * binHz);
                    }
                }
            }

            void resetSpectral() override
            {
                for (auto& c : bands)
                {
                    std::fill (c.smoothed.begin(), c.smoothed.end(), 0.0f);
                    std::fill (c.slow.begin(), c.slow.end(), 0.0f);
                    std::fill (c.gainDb.begin(), c.gainDb.end(), 0.0f);
                    std::fill (c.currentMin.begin(), c.currentMin.end(), 1.0e30f);
                    std::fill (c.subMins.begin(), c.subMins.end(), 1.0e30f);
                    c.frames = 0;
                    c.subIndex = 0;
                    c.subCount = 0;
                    if (! profileMode)
                        std::fill (c.noise.begin(), c.noise.end(), 0.0f);
                }
            }

            void beginBlock() override
            {
                reductionDb = param (reduction);
                threshold = std::pow (10.0f, param (sensitivity) / 10.0f);
                smoothBins = juce::jlimit (0, 12, (int) std::lround (param (smoothing)));
                const auto releaseSeconds = juce::jmax (0.005f, param (release) / 1000.0f);
                releaseStep = reductionDb * (float) (hop / rate) / releaseSeconds;
                outputRemoved = param (removedOnly) > 0.5f;

                if (! profileMode)
                {
                    // Minimum statistics: the noise is the lowest level within the adaptation time.
                    const auto seconds = juce::jmax (0.2f, params->size() > adaptTime ? param (adaptTime) : 1.5f);
                    framesPerSub = juce::jmax (1, (int) std::lround (seconds * rate / hop / subWindows));
                }
            }

            void processSpectrum (int channel, std::complex<float>* spectrum) override
            {
                auto& b = bands[(size_t) channel];
                const float keep = 0.5f; // power smoothing between windows

                for (int k = 0; k < numBins; ++k)
                {
                    const auto p = std::norm (spectrum[k]) / energy;
                    b.smoothed[(size_t) k] = b.frames == 0 ? p : b.smoothed[(size_t) k] * keep + p * (1.0f - keep);
                    b.slow[(size_t) k] = b.frames == 0 ? p : b.slow[(size_t) k] * 0.9f + p * 0.1f;
                }

                if (! profileMode)
                    trackNoise (b);

                ++b.frames;

                if (! hasNoise && profileMode)
                    return; // no noise print: leave the audio alone

                // Which bins carry sound above the noise? A lone bin that only just
                // passes is a noise peak: counting it as sound makes "musical noise".
                for (int k = 0; k < numBins; ++k)
                    signal[(size_t) k] = b.smoothed[(size_t) k] > b.noise[(size_t) k] * threshold;

                for (int k = 1; k < numBins - 1; ++k)
                    if (signal[(size_t) k] && ! signal[(size_t) k - 1] && ! signal[(size_t) k + 1]
                        && b.smoothed[(size_t) k] < b.noise[(size_t) k] * threshold * 4.0f)
                        lonely[(size_t) k] = true;
                    else
                        lonely[(size_t) k] = false;

                // Gain per bin with a release (slow fall), so word endings are not cut.
                for (int k = 0; k < numBins; ++k)
                {
                    const bool isSignal = signal[(size_t) k] && ! (k > 0 && k < numBins - 1 && lonely[(size_t) k]);
                    auto& g = b.gainDb[(size_t) k];
                    g = juce::jmin (0.0f, juce::jmax (isSignal ? 0.0f : -reductionDb, g - releaseStep));
                }

                // Smoothing across neighbouring frequencies (in dB). It may only raise
                // a gain, so narrow sounds such as a single tone are never turned down.
                for (int k = 0; k < numBins; ++k)
                {
                    const int lo = juce::jmax (0, k - smoothBins), hi = juce::jmin (numBins - 1, k + smoothBins);
                    float sum = 0.0f;
                    for (int j = lo; j <= hi; ++j)
                        sum += b.gainDb[(size_t) j];
                    const auto db = juce::jmax (b.gainDb[(size_t) k], sum / (float) (hi - lo + 1));
                    gains[(size_t) k] = std::pow (10.0f, db / 20.0f);
                }

                for (int k = 0; k < numBins; ++k)
                    spectrum[k] *= outputRemoved ? 1.0f - gains[(size_t) k] : gains[(size_t) k];
            }

        private:
            static constexpr int subWindows = 8;

            struct Bands
            {
                std::vector<float> noise, smoothed, slow, gainDb, currentMin, subMins;
                int frames = 0, subIndex = 0, subCount = 0;
            };

            void trackNoise (Bands& b)
            {
                for (int k = 0; k < numBins; ++k)
                    b.currentMin[(size_t) k] = juce::jmin (b.currentMin[(size_t) k], b.slow[(size_t) k]);

                if (++b.subCount >= framesPerSub)
                {
                    b.subCount = 0;
                    auto* slot = b.subMins.data() + (size_t) (b.subIndex * numBins);
                    std::copy (b.currentMin.begin(), b.currentMin.end(), slot);
                    std::fill (b.currentMin.begin(), b.currentMin.end(), 1.0e30f);
                    b.subIndex = (b.subIndex + 1) % subWindows;
                }

                // The minimum is lower than the mean noise level: correct for that (bias about 2).
                constexpr float bias = 2.0f;
                for (int k = 0; k < numBins; ++k)
                {
                    auto m = b.currentMin[(size_t) k];
                    for (int s = 0; s < subWindows; ++s)
                        m = juce::jmin (m, b.subMins[(size_t) (s * numBins + k)]);
                    b.noise[(size_t) k] = b.frames < framesPerSub ? 0.0f : m * bias; // nothing known yet: leave it alone
                }

                hasNoise = true;
            }

            const bool profileMode;
            std::shared_ptr<const NoiseProfile> profile;
            std::array<Bands, maxChannels> bands;
            std::vector<float> gains;
            std::vector<bool> signal, lonely;
            bool hasNoise = false, outputRemoved = false;
            float reductionDb = 12.0f, threshold = 4.0f, releaseStep = 1.0f;
            int smoothBins = 3, framesPerSub = 10;
        };
    }

    //==============================================================================
    namespace builtin
    {
        EffectType noiseReduction()
        {
            using P = ParameterInfo;
            EffectType t;
            t.id = "noiseReduction";
            t.name = "Noise Reduction (Noise Print)";
            t.category = "Noise Reduction and Repair";
            t.description = "Remove steady background noise that was captured first with Capture Noise Print";
            t.parameters = {
                P::number ("reduction", "Reduction", 0.0f, 48.0f, 12.0f, "dB"),
                P::number ("sensitivity", "Sensitivity", 0.0f, 24.0f, 6.0f, "dB"),
                P::number ("smoothing", "Frequency smoothing", 0.0f, 12.0f, 3.0f, "", 0),
                P::number ("release", "Release", 10.0f, 1000.0f, 150.0f, "ms", 0, 150.0f),
                P::toggle ("removedOnly", "Listen to the removed noise only", false),
            };
            t.factoryPresets = {
                { "Light (6 dB)",    { 6, 6, 3, 150, 0 } },
                { "Medium (12 dB)",  { 12, 6, 3, 150, 0 } },
                { "Strong (24 dB)",  { 24, 8, 4, 200, 0 } },
            };
            t.create = [] (ParameterValuesPtr v) { return std::make_unique<NoiseGateProcessor> (std::move (v), true); };
            return t;
        }

        EffectType adaptiveNoiseReduction()
        {
            using P = ParameterInfo;
            EffectType t;
            t.id = "adaptiveNoiseReduction";
            t.name = "Adaptive Noise Reduction";
            t.category = "Noise Reduction and Repair";
            t.description = "Remove background noise without a noise print; follows noise that changes slowly";
            t.parameters = {
                P::number ("reduction", "Reduction", 0.0f, 40.0f, 10.0f, "dB"),
                P::number ("sensitivity", "Sensitivity", 0.0f, 24.0f, 6.0f, "dB"),
                P::number ("smoothing", "Frequency smoothing", 0.0f, 12.0f, 3.0f, "", 0),
                P::number ("release", "Release", 10.0f, 1000.0f, 150.0f, "ms", 0, 150.0f),
                P::toggle ("removedOnly", "Listen to the removed noise only", false),
                P::number ("adaptTime", "Adaptation time", 0.5f, 8.0f, 1.5f, "s"),
            };
            t.factoryPresets = {
                { "Light (6 dB)",    { 6, 6, 3, 150, 0, 1.5f } },
                { "Medium (10 dB)",  { 10, 6, 3, 150, 0, 1.5f } },
                { "Strong (20 dB)",  { 20, 8, 4, 200, 0, 2.0f } },
            };
            t.create = [] (ParameterValuesPtr v) { return std::make_unique<NoiseGateProcessor> (std::move (v), false); };
            return t;
        }
    }
}
