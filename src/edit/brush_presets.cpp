#include "brush_presets.h"

#include "drawing_style.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace fla::brushpresets {

namespace {

void put(std::string& out, const char* key, double value)
{
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "style.%s=%.6g\n", key, value);
    out += buffer;
}

void putText(std::string& out, const char* key, const std::string& value)
{
    out += std::string("style.") + key + "=" + value + "\n";
}

} // namespace

std::string styleText(const DrawingStyle& style)
{
    std::string out;
    putText(out, "texture", style.brushTexture);
    put(out, "textureScale", style.brushTextureScale);
    put(out, "textureStrength", style.brushTextureStrength);
    put(out, "textureOffsetX", style.brushTextureOffsetX);
    put(out, "textureOffsetY", style.brushTextureOffsetY);
    put(out, "textureRotation", style.brushTextureRotation);
    put(out, "texturePressure", style.brushTexturePressure);
    put(out, "textureFixed", style.brushTextureFixed);
    put(out, "autoFill", style.brushAutoFill);
    put(out, "snapGuides", style.brushSnapGuides);
    put(out, "bitmapSoftness", style.bitmapSoftness);
    put(out, "bitmapOpacity", style.bitmapOpacity);
    put(out, "bitmapFlow", style.bitmapFlow);
    put(out, "bitmapSpacing", style.bitmapSpacing);
    put(out, "bitmapHardness", style.bitmapHardness);
    put(out, "bitmapOpacityPressure", style.bitmapOpacityPressure);
    put(out, "bitmapFlowPressure", style.bitmapFlowPressure);
    put(out, "bitmapBlend", static_cast<int>(style.bitmapBlend));
    putText(out, "bitmapTip", style.bitmapTip);
    putText(out, "bitmapTipTexture", style.bitmapTipTexture);
    return out;
}

std::string presetText(const BrushSettings& brush, const DrawingStyle& style)
{
    return toText(brush) + styleText(style);
}

void apply(const std::string& text, BrushSettings& brush, DrawingStyle& style)
{
    std::string own;
    size_t start = 0;
    while (start < text.size())
    {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.compare(0, 6, "style.") != 0)
        {
            own += line + "\n";
            continue;
        }
        const size_t equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        const std::string key = line.substr(6, equals - 6);
        const std::string value = line.substr(equals + 1);
        const double number = std::atof(value.c_str());
        const bool on = number != 0.0;
        if (key == "texture") style.brushTexture = value;
        else if (key == "textureScale") style.brushTextureScale = std::clamp(number, 10.0, 1000.0);
        else if (key == "textureStrength") style.brushTextureStrength = std::clamp(number, 0.0, 100.0);
        else if (key == "textureOffsetX") style.brushTextureOffsetX = number;
        else if (key == "textureOffsetY") style.brushTextureOffsetY = number;
        else if (key == "textureRotation") style.brushTextureRotation = number;
        else if (key == "texturePressure") style.brushTexturePressure = on;
        else if (key == "textureFixed") style.brushTextureFixed = on;
        else if (key == "autoFill") style.brushAutoFill = on;
        else if (key == "snapGuides") style.brushSnapGuides = on;
        else if (key == "bitmapSoftness") style.bitmapSoftness = std::clamp(number, 0.0, 100.0);
        else if (key == "bitmapOpacity") style.bitmapOpacity = std::clamp(number, 1.0, 100.0);
        else if (key == "bitmapFlow") style.bitmapFlow = std::clamp(number, 1.0, 100.0);
        else if (key == "bitmapSpacing") style.bitmapSpacing = std::clamp(number, 1.0, 500.0);
        else if (key == "bitmapHardness") style.bitmapHardness = std::clamp(number, 0.0, 100.0);
        else if (key == "bitmapOpacityPressure") style.bitmapOpacityPressure = on;
        else if (key == "bitmapFlowPressure") style.bitmapFlowPressure = on;
        else if (key == "bitmapBlend")
            style.bitmapBlend = static_cast<DrawingStyle::BitmapBlend>(std::clamp(static_cast<int>(number), 0, 6));
        else if (key == "bitmapTip") style.bitmapTip = value;
        else if (key == "bitmapTipTexture") style.bitmapTipTexture = value;
    }
    fromText(own, brush);
}

std::string toSetText(const std::vector<Preset>& presets)
{
    std::string out;
    for (const Preset& preset : presets)
    {
        out += "[" + preset.name + "]\n";
        out += preset.text;
        if (!preset.text.empty() && preset.text.back() != '\n')
            out += "\n";
    }
    return out;
}

std::vector<Preset> fromSetText(const std::string& text)
{
    std::vector<Preset> presets;
    size_t start = 0;
    while (start < text.size())
    {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.size() >= 2 && line.front() == '[' && line.back() == ']')
        {
            presets.push_back({line.substr(1, line.size() - 2), std::string()});
            continue;
        }
        if (!presets.empty() && line.find('=') != std::string::npos)
            presets.back().text += line + "\n";
    }
    return presets;
}

std::vector<Preset> builtIns(const std::string& list)
{
    // Each starts from the brush's defaults with the style's plain options,
    // then changes what makes it what it is.
    DrawingStyle plainStyle;
    const auto make = [&](const char* name, BrushSettings brush, const std::string& extra) {
        return Preset{name, presetText(brush, plainStyle) + extra};
    };
    std::vector<Preset> presets;
    if (list == kFlow)
    {
        BrushSettings brush = BrushSettings::fluidDefaults();
        presets.push_back(make("Default", brush, ""));
        BrushSettings tapered = brush;
        tapered.taperStart = 20.0;
        tapered.taperEnd = 30.0;
        tapered.minSize = 10.0;
        presets.push_back(make("Tapered Ink", tapered, ""));
        BrushSettings calligraphy = brush;
        calligraphy.size = 18.0;
        calligraphy.roundness = 25.0;
        calligraphy.angle = 45.0;
        calligraphy.angleFunction = BrushSettings::AngleFunction::Heading;
        presets.push_back(make("Calligraphy", calligraphy, ""));
        BrushSettings marker = brush;
        marker.size = 24.0;
        marker.usePressure = false;
        marker.tip = BrushSettings::Tip::Rectangle;
        marker.roundness = 60.0;
        presets.push_back(make("Marker", marker, ""));
    }
    else if (list == kNib)
    {
        BrushSettings brush = BrushSettings::classicDefaults();
        presets.push_back(make("Round", brush, ""));
        BrushSettings chisel = brush;
        chisel.tip = BrushSettings::Tip::Rectangle;
        chisel.roundness = 20.0;
        chisel.angle = 45.0;
        presets.push_back(make("Chisel", chisel, ""));
        BrushSettings star = brush;
        star.tip = BrushSettings::Tip::Star;
        star.size = 20.0;
        presets.push_back(make("Star Stamp", star, ""));
    }
    else if (list == kInk)
    {
        BrushSettings brush = BrushSettings::inkDefaults();
        presets.push_back(make("Default", brush, ""));
        BrushSettings fine = brush;
        fine.size = 3.0;
        fine.minSize = 30.0;
        fine.stabilizer = 20.0;
        presets.push_back(make("Fine Liner", fine, ""));
        BrushSettings bold = brush;
        bold.size = 14.0;
        bold.taperStart = 15.0;
        bold.taperEnd = 25.0;
        presets.push_back(make("Bold Outline", bold, ""));
    }
    else if (list == kBitmap)
    {
        BrushSettings brush = BrushSettings::fluidDefaults();
        brush.size = 30.0;
        presets.push_back(make("Soft Round", brush, ""));
        BrushSettings air = brush;
        air.size = 60.0;
        presets.push_back(make("Airbrush", air,
            "style.bitmapFlow=12\nstyle.bitmapSoftness=100\nstyle.bitmapHardness=0\nstyle.bitmapFlowPressure=1\n"));
        BrushSettings chalk = brush;
        chalk.size = 20.0;
        presets.push_back(make("Chalk", chalk,
            "style.bitmapSoftness=10\nstyle.bitmapHardness=90\nstyle.texture=Chalk\n"
            "style.bitmapTipTexture=Rough Paper\nstyle.bitmapOpacityPressure=1\n"));
        BrushSettings hard = brush;
        hard.size = 12.0;
        presets.push_back(make("Hard Round", hard, "style.bitmapSoftness=5\nstyle.bitmapHardness=100\n"));
    }
    return presets;
}

} // namespace fla::brushpresets
