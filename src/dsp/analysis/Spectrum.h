// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <complex>
#include <vector>

namespace mxj::dsp
{
    /** Magnitude spectrum of a block of samples (Hann window, radix-2 FFT).
        For the spectrum analyser; allocates only in the constructor. */
    class Spectrum
    {
    public:
        /** `order` 8 ... 15: the FFT has 2^order points. */
        explicit Spectrum (int order = 12);

        int getSize() const noexcept { return size; }

        /** Number of magnitude values: size / 2 + 1 (bin i is i * sampleRate / size Hz). */
        int getNumBins() const noexcept { return size / 2 + 1; }

        /** Computes dB magnitudes (0 dB = a full scale sine) of `getSize()` samples. */
        void compute (const float* samples, float* magnitudesDb);

        /** In-place complex FFT of 2^order points (forward). */
        static void fft (std::complex<float>* data, int order);

    private:
        int order, size;
        std::vector<float> window;
        std::vector<std::complex<float>> buffer;
        float windowGain = 1.0f;
    };
}
