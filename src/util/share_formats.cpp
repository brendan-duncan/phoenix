#include "share_formats.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace share {

const std::vector<Format>& formats()
{
    static const std::vector<Format> all = {Format::Gif, Format::WebP, Format::Apng, Format::Mp4, Format::WebM};
    return all;
}

const char* suffix(Format format)
{
    switch (format)
    {
    case Format::Gif: return "gif";
    case Format::WebP: return "webp";
    case Format::Apng: return "png";
    case Format::Mp4: return "mp4";
    case Format::WebM: return "webm";
    }
    return "gif";
}

const char* title(Format format)
{
    switch (format)
    {
    case Format::Gif: return "Animated GIF";
    case Format::WebP: return "Animated WebP";
    case Format::Apng: return "Animated PNG";
    case Format::Mp4: return "MP4 Video";
    case Format::WebM: return "WebM Video";
    }
    return "";
}

const char* blurb(Format format)
{
    switch (format)
    {
    case Format::Gif: return "Plays everywhere: chats, forums, email. Up to 256 colors a frame.";
    case Format::WebP: return "Like a GIF but smaller and in full color. Most browsers and many apps.";
    case Format::Apng: return "Full color and smooth see-through edges, nothing lost. Larger files.";
    case Format::Mp4: return "The video every social site takes, with sound. Smallest for long clips.";
    case Format::WebM: return "Web video that can be see-through, with sound. Browsers play it.";
    }
    return "";
}

bool canBeTransparent(Format format)
{
    return format != Format::Mp4;
}

bool canLoop(Format format)
{
    return format == Format::Gif || format == Format::WebP || format == Format::Apng;
}

std::string encoderFor(Format format)
{
    switch (format)
    {
    case Format::WebP: return "libwebp_anim";
    case Format::Mp4: return "libx264";
    case Format::WebM: return "libvpx-vp9";
    default: break;
    }
    return std::string();
}

int gifColors(int quality)
{
    quality = std::clamp(quality, 1, 100);
    return std::clamp(static_cast<int>(std::lround(32.0 + 224.0 * quality / 100.0)), 2, 256);
}

int videoCrf(Format format, int quality)
{
    quality = std::clamp(quality, 1, 100);
    if (format == Format::WebM)
        return static_cast<int>(std::lround(52.0 - 28.0 * quality / 100.0));
    return static_cast<int>(std::lround(40.0 - 22.0 * quality / 100.0));
}

std::vector<std::string> encoderArguments(Format format, int quality, bool transparent, int plays, bool audio,
    const std::string& output)
{
    quality = std::clamp(quality, 1, 100);
    std::vector<std::string> arguments{"-map", "0:v"};
    switch (format)
    {
    case Format::WebP:
        arguments.insert(arguments.end(), {"-c:v", "libwebp_anim", "-loop", std::to_string(std::max(0, plays))});
        if (quality >= 100)
            arguments.insert(arguments.end(), {"-lossless", "1", "-pix_fmt", "bgra"});
        else
            arguments.insert(arguments.end(), {"-quality", std::to_string(quality), "-pix_fmt",
                transparent ? "yuva420p" : "yuv420p"});
        arguments.insert(arguments.end(), {"-f", "webp"});
        break;
    case Format::WebM:
        if (audio)
            arguments.insert(arguments.end(), {"-map", "1:a", "-c:a", "libopus", "-b:a", "128k", "-shortest"});
        arguments.insert(arguments.end(), {"-c:v", "libvpx-vp9", "-b:v", "0", "-crf",
            std::to_string(videoCrf(format, quality)), "-row-mt", "1", "-pix_fmt", transparent ? "yuva420p" : "yuv420p"});
        // An alpha channel keeps only with the alternate reference frames
        // off.
        if (transparent)
            arguments.insert(arguments.end(), {"-auto-alt-ref", "0"});
        arguments.insert(arguments.end(), {"-f", "webm"});
        break;
    case Format::Mp4:
    default:
        if (audio)
            arguments.insert(arguments.end(), {"-map", "1:a", "-c:a", "aac", "-b:a", "192k", "-shortest"});
        arguments.insert(arguments.end(), {"-c:v", "libx264", "-preset", "medium", "-crf",
            std::to_string(videoCrf(Format::Mp4, quality)), "-pix_fmt", "yuv420p", "-movflags", "+faststart",
            "-f", "mp4"});
        break;
    }
    arguments.push_back(output);
    return arguments;
}

void frameSize(Format format, int& width, int& height)
{
    width = std::max(1, width);
    height = std::max(1, height);
    if (format == Format::Mp4 || format == Format::WebM)
    {
        width = std::max(2, width & ~1);
        height = std::max(2, height & ~1);
    }
}

bool sniff(const std::vector<uint8_t>& head, Format& format)
{
    const auto starts = [&head](size_t at, const char* text) {
        const size_t n = std::strlen(text);
        return head.size() >= at + n && std::memcmp(head.data() + at, text, n) == 0;
    };
    if (starts(0, "GIF89a") || starts(0, "GIF87a"))
        format = Format::Gif;
    else if (starts(0, "RIFF") && starts(8, "WEBP"))
        format = Format::WebP;
    else if (starts(0, "\x89PNG\r\n\x1a\n"))
        format = Format::Apng;
    else if (starts(4, "ftyp"))
        format = Format::Mp4;
    else if (starts(0, "\x1a\x45\xdf\xa3"))
        format = Format::WebM;
    else
        return false;
    return true;
}

} // namespace share
