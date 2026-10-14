#include "test_util.h"

#include "../src/edit/publish_settings.h"

namespace {

// The shape of Animate's file, cut down: two profiles, the second current.
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

    // No file: Animate's defaults, named after the document.
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

    // A new file has Animate's form.
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
}
