#pragma once

#include <string>
#include <vector>

namespace fla {

/// The FLA's publish settings (PublishSettings.xml in the package): which
/// formats File > Publish writes, their file names and their options.
/// The file keeps them as profiles (publish configurations), one of them
/// current. Phoenix reads a profile, publishes what it can of it, and edits
/// it in place, so every setting it does not use goes back into the file
/// unchanged. Free of Qt.
namespace publish {

struct Settings
{
    /// The profile's name, as the file records it.
    std::string profile = "Default";

    /// The formats to publish. A document with no publish settings
    /// publishes an HTML5 page; a file's profile says for itself.
    bool flash = false;
    bool html = false;
    bool png = false;
    bool jpeg = false;
    bool gif = false;
    bool svg = false;

    /// Where the files go: publishFolder, a folder made in parentFolder to
    /// hold them, or parentFolder itself when publishFolder is empty. An
    /// empty parentFolder is the document's own folder; a relative one is
    /// from it. Phoenix's own, kept beside the file's.
    std::string parentFolder;
    std::string publishFolder;

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

    /// HTML5: a page that plays the movie with Phoenix's JavaScript runtime
    /// (resources/html5), its file name, whether the page fits the stage to
    /// the window (or shows it at its own size), and whether the main
    /// timeline loops. An HTML5 Canvas document keeps these in its Canvas
    /// format's group (JavaScript/HTML), any other in Phoenix's own.
    bool html5 = true;
    std::string html5File;
    bool html5Fit = true;
    bool html5Loop = true;

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

/// The profile \a profile of a PublishSettings.xml, or with none named
/// (or none of that name) the current one; the defaults for a document
/// named \a name when \a xml is empty or not one.
Settings read(const std::string& xml, const std::string& name, const std::string& profile = std::string());

/// \a xml with the settings of the profile \a settings names (or else the
/// current one) set to \a settings and all else kept; a new file of the
/// usual form when \a xml is empty.
std::string write(const std::string& xml, const Settings& settings, const std::string& name);

/// The profiles' names in the file's order, and the current one's: one
/// profile, Default, when \a xml has none.
std::vector<std::string> profiles(const std::string& xml);
std::string currentProfile(const std::string& xml);
/// \a xml with a new profile \a profile, a copy of \a from (every setting
/// of it, Phoenix's and the file's), made current; \a xml unchanged when
/// the name is empty or taken. \a name is the document's, for a file made
/// from nothing.
std::string addProfile(const std::string& xml, const std::string& profile, const std::string& from,
    const std::string& name);
/// \a xml with \a profile current, renamed, or removed (never the last;
/// another becomes current when the current one goes); unchanged when
/// there is no such profile or the new name is taken.
std::string setCurrentProfile(const std::string& xml, const std::string& profile);
std::string renameProfile(const std::string& xml, const std::string& profile, const std::string& to);
std::string removeProfile(const std::string& xml, const std::string& profile);

/// A format's file name: its own, or with default names \a name and the
/// extension. HTML5's page is \a name.html, or \a name_html5.html while the
/// HTML format writes \a name.html.
std::string fileName(const Settings& settings, const std::string& format, const std::string& name);

} // namespace publish

} // namespace fla
