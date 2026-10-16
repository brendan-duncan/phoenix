#pragma once

#include <string>

namespace fla {

/// The FLA's publish settings (PublishSettings.xml in the package): which
/// formats File > Publish writes, their file names and their options.
/// Phoenix reads the current profile, publishes what it can of it, and
/// edits it in place, so every setting it does not use goes back into the
/// file unchanged. Free of Qt.
namespace publish {

struct Settings
{
    /// The profile's name, as the file records it.
    std::string profile = "Default";

    /// The formats to publish.
    bool flash = true;
    bool html = true;
    bool png = false;
    bool jpeg = false;
    bool gif = false;
    bool svg = false;

    /// File names; with defaultNames, each is the document's name with the
    /// format's extension.
    bool defaultNames = true;
    std::string flashFile;
    std::string htmlFile;
    std::string pngFile;
    std::string jpegFile;
    std::string gifFile;
    std::string svgFile;

    /// The images' size: the stage's (matchMovie), or their own.
    bool matchMovie = true;
    int width = 0;
    int height = 0;

    /// PNG: "24-bit with Alpha", "24-bit" or "8-bit", and an 8-bit
    /// image's colors.
    std::string pngBitDepth = "24-bit with Alpha";
    int pngColors = 255;
    /// JPEG: quality 0 to 100, progressive.
    int jpegQuality = 80;
    bool jpegProgressive = false;
    /// GIF: animated, looping (forever, or loopCount times), colors.
    bool gifAnimated = false;
    bool gifLoop = true;
    int gifLoopCount = 0;
    int gifColors = 255;
    /// SVG: hidden layers drawn, images embedded rather than written beside
    /// it.
    bool svgHiddenLayers = true;
    bool svgEmbed = false;

    /// SWF sound: the soundtrack streamed with the movie, as "mp3",
    /// "adpcm", "raw" or "none" (silent); an MP3's bit rate in kbps, ADPCM's
    /// bits a sample (2 to 5), the sample rate (5512, 11025, 22050 or
    /// 44100) and stereo or mono. Phoenix's own, kept beside the file's.
    std::string soundCompression = "mp3";
    int soundBitRate = 64;
    int soundAdpcmBits = 4;
    int soundRate = 22050;
    bool soundStereo = true;

    /// SWF event sounds' compression, the same way: the soundtrack's when
    /// Phoenix's own settings are there, else the file's EventFormat and
    /// EventCompress (as the soundtrack's come from StreamFormat and
    /// StreamCompress). Library sounds can set their own unless
    /// overrideSounds (OverrideSounds) says these win.
    struct SoundFormat
    {
        std::string compression = "mp3";
        int bitRate = 64;
        int adpcmBits = 4;
        int rate = 22050;
        bool stereo = true;
    };
    SoundFormat event;
    bool overrideSounds = false;
};

/// A sound's compression as the FLA codes it, in a profile's StreamFormat
/// and StreamCompress (EventFormat and EventCompress) or a library sound's
/// exportFormat and exportBits. \a compress picks the codec: 0 raw, 1
/// disabled, 2 to 5 ADPCM's bits a sample, 6 to 17 an MP3 bit rate (8,
/// 16, 20, 24, 32, 48, 56, 64, 80, 112, 128 or 160 kbps), 18 speech.
/// \a format holds the rest: for raw and ADPCM the rate (bits 2 and 3, 5.5
/// to 44 kHz) and stereo (bit 0); for MP3 mono (bit 0) and quality. Speech,
/// which Phoenix cannot write, comes back as mono ADPCM at its rate.
Settings::SoundFormat soundFormat(int format, int compress);

/// The current profile of a PublishSettings.xml; the defaults for a
/// document named \a name when \a xml is empty or not one.
Settings read(const std::string& xml, const std::string& name);

/// \a xml with the current profile's settings set to \a settings and all
/// else kept; a new file of the usual form when \a xml is empty.
std::string write(const std::string& xml, const Settings& settings, const std::string& name);

/// A format's file name: its own, or with default names \a name and the
/// extension.
std::string fileName(const Settings& settings, const std::string& format, const std::string& name);

} // namespace publish

} // namespace fla
