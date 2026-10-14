#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

/// Sound for SWF movies: Flash's ADPCM, and MP3 frames read for streaming.
/// Free of Qt.
namespace swf {

/// \a samples (16-bit, interleaved when \a channels is 2) as SWF's ADPCM
/// sound data: a two-bit code size, then packets of 4096 samples, each
/// starting from a full sample and step index per channel and going on in
/// \a bits bits a sample (2 to 5, the fewer the smaller and rougher).
std::vector<uint8_t> encodeAdpcm(const std::vector<int16_t>& samples, int channels, int bits);

/// SWF ADPCM data back to 16-bit samples, \a count a channel: what a player
/// hears.
std::vector<int16_t> decodeAdpcm(const std::vector<uint8_t>& data, int channels, size_t count);

/// One MPEG audio frame in an MP3 stream: where it is, how long, and how
/// many samples (a channel) it holds.
struct Mp3Frame
{
    size_t offset = 0;
    size_t length = 0;
    int samples = 0;
};

/// The frames of \a mp3, a bare MP3 stream (an ID3 tag at its start is
/// skipped), in order; what is not a frame ends the list.
std::vector<Mp3Frame> mp3Frames(const std::vector<uint8_t>& mp3);

/// Interleaved stereo float samples at \a from Hz as 16-bit samples at
/// \a rate and \a channels (mono mixes the two), each the average of the
/// samples it stands for.
std::vector<int16_t> pcm16(const std::vector<float>& stereo, int from, int rate, int channels);

/// A soundtrack streamed with a movie, cut into the data each frame
/// carries: its codec as SWF numbers it (1 ADPCM, 2 MP3, 3 raw), its rate
/// (5512, 11025, 22050 or 44100), channels, the samples a frame plays, and
/// a block a frame (empty when a frame has none).
struct SoundStream
{
    int codec = 3;
    int rate = 44100;
    bool stereo = true;
    int samplesPerFrame = 0;
    /// MP3 only: the first frame's samples the decoder throws away.
    int latency = 0;
    std::vector<std::vector<uint8_t>> blocks;
};

/// \a samples (16-bit, interleaved when \a channels is 2, at \a rate) cut
/// into \a frames frames at \a fps: raw (little-endian) or ADPCM in \a bits.
SoundStream rawStream(const std::vector<int16_t>& samples, int channels, int rate, double fps, int frames);
SoundStream adpcmStream(const std::vector<int16_t>& samples, int channels, int rate, int bits, double fps, int frames);

/// \a mp3, a bare MP3 stream at \a rate, its frames dealt out to \a frames
/// frames at \a fps so each frame's sound starts where the movie is.
/// \a delay is the encoder's silence before the sound (LAME's is 1105
/// samples), skipped by each block's seek.
SoundStream mp3Stream(const std::vector<uint8_t>& mp3, int channels, int rate, double fps, int frames, int delay = 0);

} // namespace swf
