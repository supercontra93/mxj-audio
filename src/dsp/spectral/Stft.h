// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/edit/AudioState.h"
#include "dsp/process/Render.h"
#include "dsp/spectral/Fft.h"

#include <functional>

namespace mxj::dsp
{
    /** Short-time Fourier transform tools for the spectral editor and the repair
        effects (phase 4). Hann windows, 75 % overlap (hop = size / 4), so the
        audio comes back exactly when nothing is changed. */
    namespace stft
    {
        /** FFT order used for editing at a sample rate: about 43 ms windows
            (2048 points at 44.1 and 48 kHz). */
        int orderForRate (double sampleRate);

        /** One analysed window of one channel, handed to a Modify function. */
        struct Frame
        {
            int channel = 0;
            juce::int64 centre = 0;                       ///< frame at the middle of the window
            std::complex<float>* bins = nullptr;          ///< change these
            const std::complex<float>* other = nullptr;   ///< the same window of the `other` audio, if given
            int numBins = 0;
            double binHz = 0.0;                           ///< width of one bin in Hz
        };

        /** Changes the spectrum of one window. Must give the same result each
            time it is called with the same frame (windows near chunk edges are
            analysed twice). */
        using Modify = std::function<void (const Frame&)>;

        /** Analyses `audio`, lets `modify` change every window that touches
            `range`, and returns the re-synthesised audio of `range` only.
            Windows read the real audio around the range, so where `modify`
            changes nothing the result equals the input (seamless edges).
            `other`, if given, must have the same format and timeline; its windows
            are passed along (for mixing processed audio into a frequency band).
            Call on a background thread. Returns an empty sequence (0 channels) if
            cancelled or on a write error. */
        AudioSequence process (const AudioSequence& audio, FrameRange range, int order, const Modify& modify,
                               BlockStore& store, const WorkControl& control = {}, const AudioSequence* other = nullptr);

        /** Mean power (|X|^2 / window energy) per bin and channel over a range.
            A range shorter than one window gives one zero-padded window. */
        std::vector<std::vector<float>> averagePower (const AudioSequence& audio, FrameRange range, int order,
                                                      const WorkControl& control = {});
    }

    //==============================================================================
    /** Maps frequencies to a vertical position for the spectrogram and the
        spectral selection. Proportion 0 = bottom (lowest frequency), 1 = top (Nyquist). */
    struct FrequencyScale
    {
        enum class Kind { linear, logarithmic, mel };

        Kind kind = Kind::logarithmic;
        double nyquist = 24000.0;

        static constexpr double lowestLogHz = 20.0;

        double toProportion (double hz) const;
        double fromProportion (double proportion) const;

        static Kind fromName (const juce::String& name); ///< "linear", "log", "mel"
        static double hzToMel (double hz);
        static double melToHz (double mel);
    };

    //==============================================================================
    /** Power spectrum in dB of windows of audio, for drawing a spectrogram.
        0 dB = a full-scale sine. Allocates only in the constructor. */
    class SpectrumAnalyser
    {
    public:
        explicit SpectrumAnalyser (int order);

        int getSize() const noexcept    { return fft.getSize(); }
        int getNumBins() const noexcept { return fft.getNumBins(); }

        /** `samples`: getSize() values; `powerDb`: getNumBins() values. */
        void compute (const float* samples, float* powerDb);

    private:
        RealFft fft;
        std::vector<float> window, buffer;
        std::vector<std::complex<float>> bins;
        float scale = 1.0f;
    };
}
