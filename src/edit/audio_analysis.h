#pragma once

#include <vector>

namespace fla {

/// What a scene's sound is like frame by frame, for behaviors that dance
/// to it: how much of it is in each of a number of frequency bands (an
/// audio spectrum, low to high) and the shape of the wave over the frame.
/// Free of Qt.
namespace audio {

struct Analysis
{
    int frames = 0;
    int bands = 0;
    int points = 0;
    /// frames by bands, each 0 to 1 against the loudest any band gets.
    std::vector<float> spectrum;
    /// frames by points, each -1 to 1 against the loudest sample.
    std::vector<float> wave;

    /// The bands and the wave on \a frame; false past the end.
    bool at(int frame, std::vector<double>& bandsOut, std::vector<double>& waveOut) const;
};

/// Analyzes \a stereo (interleaved left and right samples at
/// \a sampleRate) as \a frames frames at \a fps: \a bands bands spaced
/// evenly in pitch from 40 Hz to 16 kHz, and \a points points of the wave.
Analysis analyze(const std::vector<float>& stereo, int sampleRate, double fps, int frames, int bands = 32, int points = 64);

} // namespace audio

} // namespace fla
