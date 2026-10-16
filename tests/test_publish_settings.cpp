#include "test_util.h"

#include "../src/edit/publish_settings.h"

namespace {

// The shape of an FLA's publish settings file, cut down: two profiles, the second current.
const char* const kAnimate = R"(<flash_profiles>
<flash_profile version="1.0" name="Old" current="false">
  <PublishFormatProperties enabled="true"><png>0</png></PublishFormatProperties>
</flash_profile>
<flash_profile version="1.0" name="Default" current="true">
  <PublishFormatProperties enabled="true">
    <defaultNames>0</defaultNames>
    <flash>1</flash>
    <html>1</html>
    <gif>1</gif>
    <jpeg>0</jpeg>
    <png>1</png>
    <gifFileName>loop.gif</gifFileName>
    <pngFileName>poster.png</pngFileName>
    <publishFormat id="BBC1F406-65F2-4219-9304-FA48C7549E44">1</publishFormat>
  </PublishFormatProperties>
  <PublishFlashProperties enabled="true"><Version>17</Version></PublishFlashProperties>
  <PublishGifProperties enabled="true">
    <Width>640</Width><Height>360</Height><Animated>1</Animated><MatchMovieDim>0</MatchMovieDim>
    <Loop>0</Loop><LoopCount>3</LoopCount><MaxColors>64</MaxColors>
  </PublishGifProperties>
  <PublishPNGProperties enabled="true">
    <Width>640</Width><Height>360</Height><MatchMovieDim>0</MatchMovieDim><BitDepth>8-bit</BitDepth><MaxColors>128</MaxColors>
  </PublishPNGProperties>
  <PublishProperties id="BBC1F406-65F2-4219-9304-FA48C7549E44" name="SVG Image" otf="true" enabled="true">
    <Property name="default">false</Property>
    <Property name="filename">art.svg</Property>
    <Property name="includeHiddenLayers">false</Property>
  </PublishProperties>
</flash_profile>
</flash_profiles>)";

} // namespace

TEST(publish_settings_are_read_from_the_current_profile)
{
    const fla::publish::Settings settings = fla::publish::read(kAnimate, "shot");
    CHECK(settings.profile == "Default");
    CHECK(settings.png && settings.gif && settings.svg && !settings.jpeg && settings.flash);
    CHECK(!settings.defaultNames && settings.pngFile == "poster.png" && settings.gifFile == "loop.gif");
    CHECK(settings.svgFile == "art.svg" && !settings.svgHiddenLayers);
    CHECK(!settings.matchMovie && settings.width == 640 && settings.height == 360);
    CHECK(settings.pngBitDepth == "8-bit" && settings.pngColors == 128);
    CHECK(settings.gifAnimated && !settings.gifLoop && settings.gifLoopCount == 3 && settings.gifColors == 64);
    CHECK(fla::publish::fileName(settings, "png", "shot") == "poster.png");

    // No file: the format's defaults, named after the document.
    const fla::publish::Settings plain = fla::publish::read("", "shot");
    CHECK(plain.flash && plain.html && !plain.png && plain.defaultNames);
    CHECK(fla::publish::fileName(plain, "jpeg", "shot") == "shot.jpg");
}

TEST(publish_settings_are_written_back_keeping_the_rest)
{
    fla::publish::Settings settings = fla::publish::read(kAnimate, "shot");
    settings.jpeg = true;
    settings.jpegQuality = 55;
    settings.svgEmbed = true;
    const std::string written = fla::publish::write(kAnimate, settings, "shot");
    // What Phoenix does not use is still there, the other profile too.
    CHECK(written.find("<Version>17</Version>") != std::string::npos);
    CHECK(written.find("name=\"Old\"") != std::string::npos);
    const fla::publish::Settings back = fla::publish::read(written, "shot");
    CHECK(back.jpeg && back.jpegQuality == 55 && back.svgEmbed && back.png && back.gifColors == 64);
    CHECK(back.pngFile == "poster.png");

    // A new file has the format's usual form.
    const std::string made = fla::publish::write("", settings, "shot");
    CHECK(made.find("<flash_profiles>") != std::string::npos && made.find("current=\"true\"") != std::string::npos);
    CHECK(fla::publish::read(made, "shot").jpegQuality == 55);
}

TEST(publish_settings_keep_the_movie_sound_beside_animates)
{
    fla::publish::Settings settings = fla::publish::read(kAnimate, "shot");
    CHECK(settings.soundCompression == "mp3" && settings.soundStereo);
    settings.soundCompression = "adpcm";
    settings.soundAdpcmBits = 3;
    settings.soundRate = 11025;
    settings.soundStereo = false;
    const std::string written = fla::publish::write(kAnimate, settings, "shot");
    CHECK(written.find("<Version>17</Version>") != std::string::npos);
    const fla::publish::Settings back = fla::publish::read(written, "shot");
    CHECK(back.soundCompression == "adpcm" && back.soundAdpcmBits == 3);
    CHECK(back.soundRate == 11025 && !back.soundStereo);
    // Phoenix's own setting is for event sounds too.
    CHECK(back.event.compression == "adpcm" && back.event.adpcmBits == 3 && back.event.rate == 11025);
}

TEST(publish_settings_read_animates_sound_codes)
{
    using fla::publish::soundFormat;
    // The usual MP3 at 16 kbps, mono as it must be that low.
    fla::publish::Settings::SoundFormat sound = soundFormat(0, 7);
    CHECK(sound.compression == "mp3" && sound.bitRate == 16 && !sound.stereo && sound.rate == 11025);
    // 160 kbps converted to mono, and 64 kbps in stereo.
    sound = soundFormat(3, 17);
    CHECK(sound.compression == "mp3" && sound.bitRate == 160 && !sound.stereo && sound.rate == 44100);
    sound = soundFormat(4, 13);
    CHECK(sound.compression == "mp3" && sound.bitRate == 64 && sound.stereo);
    // Raw at 11 kHz in stereo; 4-bit ADPCM at 44 kHz in mono.
    sound = soundFormat(7, 0);
    CHECK(sound.compression == "raw" && sound.rate == 11025 && sound.stereo);
    sound = soundFormat(14, 4);
    CHECK(sound.compression == "adpcm" && sound.adpcmBits == 4 && sound.rate == 44100 && !sound.stereo);
    // Disabled; speech, which Phoenix writes as ADPCM.
    CHECK(soundFormat(0, 1).compression == "none");
    sound = soundFormat(10, 18);
    CHECK(sound.compression == "adpcm" && sound.rate == 22050 && !sound.stereo);

    // A profile from Animate: the stream and event sounds each as set.
    const std::string xml = "<flash_profiles><flash_profile name=\"Default\" current=\"true\">"
                            "<PublishFlashProperties enabled=\"true\"><StreamFormat>7</StreamFormat>"
                            "<StreamCompress>0</StreamCompress><EventFormat>0</EventFormat>"
                            "<EventCompress>12</EventCompress><OverrideSounds>1</OverrideSounds>"
                            "</PublishFlashProperties></flash_profile></flash_profiles>";
    const fla::publish::Settings settings = fla::publish::read(xml, "shot");
    CHECK(settings.soundCompression == "raw" && settings.soundRate == 11025 && settings.soundStereo);
    CHECK(settings.event.compression == "mp3" && settings.event.bitRate == 56 && settings.event.stereo);
    CHECK(settings.overrideSounds);
}
