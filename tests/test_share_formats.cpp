#include "test_util.h"

#include "../src/util/apng_writer.h"
#include "../src/util/gif_writer.h"
#include "../src/util/share_formats.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

const int kWidth = 64;
const int kHeight = 48;
const int kFrames = 5;

/// Frame \a i: a square moving across a half see-through background, as
/// straight RGBA.
std::vector<uint8_t> frameRgba(int i)
{
    std::vector<uint8_t> rgba(static_cast<size_t>(kWidth) * kHeight * 4);
    for (int y = 0; y < kHeight; ++y)
    {
        for (int x = 0; x < kWidth; ++x)
        {
            uint8_t* p = rgba.data() + (static_cast<size_t>(y) * kWidth + x) * 4;
            const bool inside = x >= i * 8 && x < i * 8 + 16 && y >= 16 && y < 32;
            p[0] = inside ? 255 : 20;
            p[1] = inside ? 128 : 40;
            p[2] = inside ? 0 : 200;
            p[3] = inside ? 255 : 128;
        }
    }
    return rgba;
}

std::vector<uint8_t> readAll(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string ffmpegProgram()
{
    if (const char* given = std::getenv("PHOENIX_TEST_FFMPEG"))
        return given;
#ifdef PHOENIX_TEST_FFMPEG
    if (std::filesystem::exists(PHOENIX_TEST_FFMPEG))
        return PHOENIX_TEST_FFMPEG;
#endif
    return std::string();
}

std::string quoted(const std::string& text)
{
    return "\"" + text + "\"";
}

int runCommand(std::string command)
{
#ifdef _WIN32
    // cmd takes off one pair of quotes round the whole line.
    command = "\"" + command + "\"";
#endif
    return std::system(command.c_str());
}

/// How many video frames ffmpeg decodes from \a file; -1 when it cannot.
int decodedFrames(const std::string& ffmpeg, const std::filesystem::path& file)
{
    const std::filesystem::path list = file.string() + ".crc";
    if (runCommand(quoted(ffmpeg) + " -y -hide_banner -loglevel error -i " + quoted(file.string()) +
                   " -map 0:v -f framecrc " + quoted(list.string())) != 0)
        return -1;
    std::ifstream in(list);
    std::string line;
    int frames = 0;
    while (std::getline(in, line))
        if (!line.empty() && line[0] != '#')
            ++frames;
    return frames;
}

/// An animated WebP's frames (ANMF chunks) and loop count (ANIM).
int webpFrames(const std::vector<uint8_t>& bytes, int& loops)
{
    loops = -1;
    int frames = 0;
    size_t at = 12;
    while (at + 8 <= bytes.size())
    {
        const uint32_t size = bytes[at + 4] | (bytes[at + 5] << 8) | (bytes[at + 6] << 16) |
                              (static_cast<uint32_t>(bytes[at + 7]) << 24);
        if (std::memcmp(bytes.data() + at, "ANMF", 4) == 0)
            ++frames;
        if (std::memcmp(bytes.data() + at, "ANIM", 4) == 0 && size >= 6)
            loops = bytes[at + 12] | (bytes[at + 13] << 8);
        at += 8 + size + (size & 1);
    }
    return frames;
}

std::filesystem::path scratch(const char* name)
{
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "phoenix_share_test";
    std::filesystem::create_directories(folder);
    return folder / name;
}

/// Encodes the test frames to \a format through ffmpeg as the Share
/// export does: raw BGRA in, the format's own arguments after.
bool encodeWithFfmpeg(const std::string& ffmpeg, share::Format format, bool transparent, int plays,
    const std::filesystem::path& out)
{
    const std::filesystem::path raw = scratch("frames.bgra");
    {
        std::ofstream file(raw, std::ios::binary);
        for (int i = 0; i < kFrames; ++i)
        {
            std::vector<uint8_t> rgba = frameRgba(i);
            for (size_t p = 0; p < rgba.size(); p += 4)
                std::swap(rgba[p], rgba[p + 2]);
            file.write(reinterpret_cast<const char*>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
        }
    }
    std::string command = quoted(ffmpeg) + " -y -hide_banner -loglevel error -f rawvideo -pix_fmt bgra -s " +
                          std::to_string(kWidth) + "x" + std::to_string(kHeight) + " -framerate 12 -i " +
                          quoted(raw.string());
    for (const std::string& argument : share::encoderArguments(format, 80, transparent, plays, false, out.string()))
        command += " " + quoted(argument);
    return runCommand(command) == 0;
}

} // namespace

TEST(apng_writer_writes_each_frame_and_its_loop_count)
{
    apng::AnimationWriter writer(kWidth, kHeight, 12.0, 3);
    for (int i = 0; i < kFrames; ++i)
        CHECK(writer.addFrame(frameRgba(i)));
    // The same again: the last frame lasts longer instead.
    CHECK(writer.addFrame(frameRgba(kFrames - 1)));
    CHECK(!writer.addFrame(std::vector<uint8_t>(4)));
    const std::vector<uint8_t> bytes = writer.finish();
    apng::Info info;
    CHECK(apng::inspect(bytes, info));
    CHECK(info.width == kWidth && info.height == kHeight);
    CHECK(info.frames == kFrames && info.frameControls == kFrames);
    CHECK(info.plays == 3);
    share::Format format = share::Format::Gif;
    CHECK(share::sniff(bytes, format) && format == share::Format::Apng);

    // A corrupted byte fails its chunk's checksum.
    std::vector<uint8_t> broken = bytes;
    broken[40] ^= 0xFF;
    CHECK(!apng::inspect(broken, info));

    // And ffmpeg, a reader of its own, plays every frame.
    const std::string ffmpeg = ffmpegProgram();
    if (ffmpeg.empty())
        return;
    const std::filesystem::path file = scratch("share.png");
    {
        std::ofstream out(file, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    CHECK(decodedFrames(ffmpeg, file) == kFrames);
}

TEST(share_quality_and_formats)
{
    CHECK(share::formats().size() == 5);
    CHECK(share::gifColors(100) == 256 && share::gifColors(1) < 64);
    CHECK(share::videoCrf(share::Format::Mp4, 100) == 18);
    CHECK(share::videoCrf(share::Format::Mp4, 50) > 18);
    CHECK(share::canBeTransparent(share::Format::WebM) && !share::canBeTransparent(share::Format::Mp4));
    CHECK(share::canLoop(share::Format::WebP) && !share::canLoop(share::Format::Mp4));
    CHECK(std::string(share::suffix(share::Format::Apng)) == "png");
    int width = 101;
    int height = 57;
    share::frameSize(share::Format::Mp4, width, height);
    CHECK(width == 100 && height == 56);
    width = 101;
    share::frameSize(share::Format::WebP, width, height);
    CHECK(width == 101);
}

TEST(share_gif_writes_every_frame)
{
    gif::Options options;
    options.colors = share::gifColors(80);
    options.repeat = 2;
    gif::AnimationWriter writer(kWidth, kHeight, true, options);
    for (int i = 0; i < kFrames; ++i)
        CHECK(writer.addFrame(frameRgba(i), gif::AnimationWriter::delayOf(i, 12.0), true));
    const std::vector<uint8_t> bytes = writer.finish();
    share::Format format = share::Format::Mp4;
    CHECK(share::sniff(bytes, format) && format == share::Format::Gif);
    const std::string ffmpeg = ffmpegProgram();
    if (ffmpeg.empty())
        return;
    const std::filesystem::path file = scratch("share.gif");
    {
        std::ofstream out(file, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    CHECK(decodedFrames(ffmpeg, file) == kFrames);
}

TEST(share_ffmpeg_formats_write_valid_files)
{
    const std::string ffmpeg = ffmpegProgram();
    if (ffmpeg.empty())
    {
        std::printf("    (no ffmpeg: skipped)\n");
        return;
    }
    // Animated WebP, see-through, playing twice.
    const std::filesystem::path webp = scratch("share.webp");
    CHECK(encodeWithFfmpeg(ffmpeg, share::Format::WebP, true, 2, webp));
    std::vector<uint8_t> bytes = readAll(webp);
    share::Format format = share::Format::Gif;
    CHECK(share::sniff(bytes, format) && format == share::Format::WebP);
    int loops = -1;
    CHECK(webpFrames(bytes, loops) == kFrames);
    CHECK(loops == 2);

    // MP4 (H.264) and see-through WebM (VP9).
    const std::filesystem::path mp4 = scratch("share.mp4");
    CHECK(encodeWithFfmpeg(ffmpeg, share::Format::Mp4, false, 0, mp4));
    bytes = readAll(mp4);
    CHECK(share::sniff(bytes, format) && format == share::Format::Mp4);
    CHECK(decodedFrames(ffmpeg, mp4) == kFrames);

    const std::filesystem::path webm = scratch("share.webm");
    CHECK(encodeWithFfmpeg(ffmpeg, share::Format::WebM, true, 0, webm));
    bytes = readAll(webm);
    CHECK(share::sniff(bytes, format) && format == share::Format::WebM);
    CHECK(decodedFrames(ffmpeg, webm) == kFrames);
    // The alpha channel is kept: VP9 stores it as a side stream ffmpeg
    // marks in the file.
    const std::string text(bytes.begin(), bytes.end());
    CHECK(text.find("ALPHA_MODE") != std::string::npos);
}
