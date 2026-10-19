#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// The formats File > Share writes for the web and social sites, and how
/// each is encoded: GIF and animated PNG by Phoenix itself, animated WebP,
/// MP4 and WebM by the ffmpeg that ships with it. Free of Qt.
namespace share {

enum class Format
{
    Gif,
    WebP,
    Apng,
    Mp4,
    WebM
};

/// Every format, in the order the Share dialog shows them.
const std::vector<Format>& formats();

/// The file name ending (no dot), the name to show and a line on what the
/// format is good for.
const char* suffix(Format format);
const char* title(Format format);
const char* blurb(Format format);

/// Whether the format can have a see-through background, and whether it
/// can play over and over (a video plays once; a site loops it).
bool canBeTransparent(Format format);
bool canLoop(Format format);

/// The ffmpeg encoder the format needs, or empty when Phoenix writes it
/// itself.
std::string encoderFor(Format format);

/// What the quality slider (1 to 100) means for each format.
int gifColors(int quality);
/// The constant rate factor for MP4 (18 at best) and WebM (24 at best).
int videoCrf(Format format, int quality);

/// The ffmpeg arguments that follow the inputs (the picture as raw BGRA,
/// and the sound when \a audio): the encoder, its quality, its looping and
/// \a output. \a plays is how many times the animation plays, 0 for ever.
std::vector<std::string> encoderArguments(Format format, int quality, bool transparent, int plays, bool audio,
    const std::string& output);

/// The sides a frame is drawn at: MP4 and WebM in 4:2:0 color want even
/// sides.
void frameSize(Format format, int& width, int& height);

/// What a written file's first bytes say it is, for checking one: the
/// format whose signature it has, or false.
bool sniff(const std::vector<uint8_t>& head, Format& format);

} // namespace share
