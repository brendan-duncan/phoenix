#include "audio_analysis.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace fla::audio {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr size_t kWindow = 2048;

/// An in-place radix-2 FFT of a power-of-two length.
void fft(std::vector<std::complex<double>>& data)
{
    const size_t n = data.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(data[i], data[j]);
    }
    for (size_t length = 2; length <= n; length <<= 1)
    {
        const double angle = -2.0 * kPi / static_cast<double>(length);
        const std::complex<double> step(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += length)
        {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < length / 2; ++k)
            {
                const std::complex<double> even = data[i + k];
                const std::complex<double> odd = data[i + k + length / 2] * w;
                data[i + k] = even + odd;
                data[i + k + length / 2] = even - odd;
                w *= step;
            }
        }
    }
}

} // namespace

bool Analysis::at(int frame, std::vector<double>& bandsOut, std::vector<double>& waveOut) const
{
    if (frame < 0 || frame >= frames)
        return false;
    bandsOut.assign(spectrum.begin() + static_cast<long>(frame) * bands, spectrum.begin() + static_cast<long>(frame + 1) * bands);
    waveOut.assign(wave.begin() + static_cast<long>(frame) * points, wave.begin() + static_cast<long>(frame + 1) * points);
    return true;
}

Analysis analyze(const std::vector<float>& stereo, int sampleRate, double fps, int frames, int bands, int points)
{
    Analysis out;
    if (frames <= 0 || sampleRate <= 0 || fps <= 0.0 || bands <= 0 || points <= 0)
        return out;
    out.frames = frames;
    out.bands = bands;
    out.points = points;
    out.spectrum.assign(static_cast<size_t>(frames) * bands, 0.0f);
    out.wave.assign(static_cast<size_t>(frames) * points, 0.0f);
    // One channel, the two mixed.
    std::vector<float> mono(stereo.size() / 2);
    for (size_t i = 0; i < mono.size(); ++i)
        mono[i] = 0.5f * (stereo[i * 2] + stereo[i * 2 + 1]);
    if (mono.empty())
        return out;
    const double perFrame = sampleRate / fps;
    // Which FFT bins each band takes, spaced evenly in pitch.
    const double lowest = 40.0;
    const double highest = std::min(16000.0, sampleRate / 2.0);
    std::vector<std::pair<size_t, size_t>> bins(static_cast<size_t>(bands));
    for (int b = 0; b < bands; ++b)
    {
        const double from = lowest * std::pow(highest / lowest, static_cast<double>(b) / bands);
        const double to = lowest * std::pow(highest / lowest, static_cast<double>(b + 1) / bands);
        size_t first = static_cast<size_t>(std::floor(from * kWindow / sampleRate));
        size_t last = static_cast<size_t>(std::ceil(to * kWindow / sampleRate));
        first = std::clamp<size_t>(first, 1, kWindow / 2 - 1);
        last = std::clamp<size_t>(last, first + 1, kWindow / 2);
        bins[static_cast<size_t>(b)] = {first, last};
    }
    std::vector<double> hann(kWindow);
    for (size_t i = 0; i < kWindow; ++i)
        hann[i] = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / (kWindow - 1));
    std::vector<std::complex<double>> data(kWindow);
    std::vector<double> levels(static_cast<size_t>(frames) * bands, -200.0);
    double loudestLevel = -200.0;
    float peak = 0.0f;
    for (float sample : mono)
        peak = std::max(peak, std::fabs(sample));
    for (int f = 0; f < frames; ++f)
    {
        // The window centered on the middle of the frame.
        const double middle = (f + 0.5) * perFrame;
        const long start = static_cast<long>(std::llround(middle)) - static_cast<long>(kWindow / 2);
        bool any = false;
        for (size_t i = 0; i < kWindow; ++i)
        {
            const long at = start + static_cast<long>(i);
            const double sample = at >= 0 && at < static_cast<long>(mono.size()) ? mono[static_cast<size_t>(at)] : 0.0;
            any = any || sample != 0.0;
            data[i] = std::complex<double>(sample * hann[i], 0.0);
        }
        if (any)
        {
            fft(data);
            for (int b = 0; b < bands; ++b)
            {
                double sum = 0.0;
                const auto [first, last] = bins[static_cast<size_t>(b)];
                for (size_t k = first; k < last; ++k)
                    sum += std::abs(data[k]);
                const double magnitude = sum / static_cast<double>(last - first);
                const double level = 20.0 * std::log10(magnitude + 1e-9);
                levels[static_cast<size_t>(f) * bands + b] = level;
                loudestLevel = std::max(loudestLevel, level);
            }
        }
        // The wave over the frame, a point at a time.
        for (int p = 0; p < points; ++p)
        {
            const double at = f * perFrame + perFrame * (p + 0.5) / points;
            const size_t index = static_cast<size_t>(at);
            const float sample = index < mono.size() ? mono[index] : 0.0f;
            out.wave[static_cast<size_t>(f) * points + p] = peak > 1e-6f ? sample / peak : 0.0f;
        }
    }
    // The bands from 60 dB under the loudest (silent) to the loudest (1).
    constexpr double kRange = 60.0;
    if (loudestLevel <= -199.0)
        return out;
    for (size_t i = 0; i < levels.size(); ++i)
        out.spectrum[i] = static_cast<float>(std::clamp((levels[i] - (loudestLevel - kRange)) / kRange, 0.0, 1.0));
    return out;
}

} // namespace fla::audio
