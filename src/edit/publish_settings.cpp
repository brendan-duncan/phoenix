#include "publish_settings.h"

#include "tinyxml2.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace fla::publish {

namespace {

using tinyxml2::XMLDocument;
using tinyxml2::XMLElement;

const char* const kSvgId = "BBC1F406-65F2-4219-9304-FA48C7549E44";
/// The HTML5 Canvas format's group, and Phoenix's own HTML5 group for other
/// documents.
const char* const kCanvasId = "JavaScript/HTML";
const char* const kHtml5Id = "Phoenix/HTML5";

/// The profile's PublishProperties group with the id \a id, or null.
template <typename Element>
Element* group(Element* profile, const char* id)
{
    for (Element* element = profile ? profile->FirstChildElement("PublishProperties") : nullptr; element;
         element = element->NextSiblingElement("PublishProperties"))
    {
        const char* value = element->Attribute("id");
        if (value && std::strcmp(value, id) == 0)
            return element;
    }
    return nullptr;
}

/// The profile in use: the one marked current, or the first.
XMLElement* currentProfile(XMLDocument& document)
{
    XMLElement* root = document.RootElement();
    if (!root)
        return nullptr;
    if (std::strcmp(root->Name(), "flash_profile") == 0)
        return root;
    XMLElement* first = root->FirstChildElement("flash_profile");
    for (XMLElement* profile = first; profile; profile = profile->NextSiblingElement("flash_profile"))
    {
        const char* current = profile->Attribute("current");
        if (current && std::strcmp(current, "true") == 0)
            return profile;
    }
    return first;
}

std::string textOf(const XMLElement* parent, const char* name, const std::string& fallback = std::string())
{
    const XMLElement* element = parent ? parent->FirstChildElement(name) : nullptr;
    return element && element->GetText() ? element->GetText() : (element ? std::string() : fallback);
}

int numberOf(const XMLElement* parent, const char* name, int fallback)
{
    const XMLElement* element = parent ? parent->FirstChildElement(name) : nullptr;
    return element && element->GetText() ? std::atoi(element->GetText()) : fallback;
}

bool flagOf(const XMLElement* parent, const char* name, bool fallback)
{
    return numberOf(parent, name, fallback ? 1 : 0) != 0;
}

/// The child \a name of \a parent, made when it is missing.
XMLElement* child(XMLDocument& document, XMLElement* parent, const char* name)
{
    XMLElement* element = parent->FirstChildElement(name);
    if (!element)
        element = parent->InsertEndChild(document.NewElement(name))->ToElement();
    return element;
}

void setText(XMLDocument& document, XMLElement* parent, const char* name, const std::string& value)
{
    child(document, parent, name)->SetText(value.c_str());
}

void setNumber(XMLDocument& document, XMLElement* parent, const char* name, int value)
{
    setText(document, parent, name, std::to_string(value));
}

/// \a value as the child \a name, or no such child when it is empty: for
/// Phoenix's own settings, so a file that never set them is left as it was.
void setOptionalText(XMLDocument& document, XMLElement* parent, const char* name, const std::string& value)
{
    if (!value.empty())
        setText(document, parent, name, value);
    else if (XMLElement* element = parent->FirstChildElement(name))
        parent->DeleteChild(element);
}

/// An "otf" format's settings (SVG Image): its Property elements.
XMLElement* properties(XMLDocument& document, XMLElement* profile, const char* id, const char* name)
{
    for (XMLElement* element = profile->FirstChildElement("PublishProperties"); element;
         element = element->NextSiblingElement("PublishProperties"))
    {
        const char* value = element->Attribute("id");
        if (value && std::strcmp(value, id) == 0)
            return element;
    }
    XMLElement* made = profile->InsertEndChild(document.NewElement("PublishProperties"))->ToElement();
    made->SetAttribute("id", id);
    made->SetAttribute("name", name);
    made->SetAttribute("otf", "true");
    made->SetAttribute("enabled", "true");
    return made;
}

std::string propertyOf(const XMLElement* group, const char* name, const std::string& fallback)
{
    for (const XMLElement* property = group ? group->FirstChildElement("Property") : nullptr; property;
         property = property->NextSiblingElement("Property"))
    {
        const char* key = property->Attribute("name");
        if (key && std::strcmp(key, name) == 0)
            return property->GetText() ? property->GetText() : std::string();
    }
    return fallback;
}

void setProperty(XMLDocument& document, XMLElement* group, const char* name, const std::string& value)
{
    for (XMLElement* property = group->FirstChildElement("Property"); property;
         property = property->NextSiblingElement("Property"))
    {
        const char* key = property->Attribute("name");
        if (key && std::strcmp(key, name) == 0)
        {
            property->SetText(value.c_str());
            return;
        }
    }
    XMLElement* made = group->InsertEndChild(document.NewElement("Property"))->ToElement();
    made->SetAttribute("name", name);
    made->SetText(value.c_str());
}

/// The SVG format's switch among PublishFormatProperties' publishFormat
/// elements.
XMLElement* svgSwitch(XMLDocument& document, XMLElement* formats)
{
    for (XMLElement* element = formats->FirstChildElement("publishFormat"); element;
         element = element->NextSiblingElement("publishFormat"))
    {
        const char* id = element->Attribute("id");
        if (id && std::strcmp(id, kSvgId) == 0)
            return element;
    }
    XMLElement* made = formats->InsertEndChild(document.NewElement("publishFormat"))->ToElement();
    made->SetAttribute("id", kSvgId);
    made->SetText("0");
    return made;
}

} // namespace

Settings read(const std::string& xml, const std::string& name)
{
    Settings settings;
    settings.flashFile = name + ".swf";
    settings.htmlFile = name + ".html";
    settings.pngFile = name + ".png";
    settings.jpegFile = name + ".jpg";
    settings.gifFile = name + ".gif";
    settings.svgFile = name + ".svg";
    settings.html5File = name + ".html";
    XMLDocument document;
    if (xml.empty() || document.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS)
        return settings;
    const XMLElement* profile = currentProfile(document);
    if (!profile)
        return settings;
    if (const char* profileName = profile->Attribute("name"))
        settings.profile = profileName;
    const XMLElement* formats = profile->FirstChildElement("PublishFormatProperties");
    settings.defaultNames = flagOf(formats, "defaultNames", true);
    settings.flash = flagOf(formats, "flash", true);
    settings.html = flagOf(formats, "html", true);
    settings.png = flagOf(formats, "png", false);
    settings.jpeg = flagOf(formats, "jpeg", false);
    settings.gif = flagOf(formats, "gif", false);
    settings.flashFile = textOf(formats, "flashFileName", settings.flashFile);
    settings.htmlFile = textOf(formats, "htmlFileName", settings.htmlFile);
    settings.pngFile = textOf(formats, "pngFileName", settings.pngFile);
    settings.jpegFile = textOf(formats, "jpegFileName", settings.jpegFile);
    settings.gifFile = textOf(formats, "gifFileName", settings.gifFile);
    settings.parentFolder = textOf(formats, "PhoenixParentFolder");
    settings.publishFolder = textOf(formats, "PhoenixPublishFolder");
    for (const XMLElement* element = formats ? formats->FirstChildElement("publishFormat") : nullptr; element;
         element = element->NextSiblingElement("publishFormat"))
    {
        const char* id = element->Attribute("id");
        if (id && std::strcmp(id, kSvgId) == 0)
            settings.svg = element->GetText() && std::atoi(element->GetText()) != 0;
    }
    const XMLElement* png = profile->FirstChildElement("PublishPNGProperties");
    settings.matchMovie = flagOf(png, "MatchMovieDim", true);
    settings.width = numberOf(png, "Width", 0);
    settings.height = numberOf(png, "Height", 0);
    settings.pngBitDepth = textOf(png, "BitDepth", settings.pngBitDepth);
    settings.pngColors = numberOf(png, "MaxColors", 255);
    const XMLElement* jpeg = profile->FirstChildElement("PublishJpegProperties");
    settings.jpegQuality = numberOf(jpeg, "Quality", 80);
    settings.jpegProgressive = flagOf(jpeg, "Progressive", false);
    const XMLElement* gif = profile->FirstChildElement("PublishGifProperties");
    settings.gifAnimated = flagOf(gif, "Animated", false);
    settings.gifLoop = flagOf(gif, "Loop", true);
    settings.gifLoopCount = numberOf(gif, "LoopCount", 0);
    settings.gifColors = numberOf(gif, "MaxColors", 255);
    const XMLElement* flash = profile->FirstChildElement("PublishFlashProperties");
    settings.overrideSounds = flagOf(flash, "OverrideSounds", false);
    // The file's own codes, for a profile Phoenix has not set its own in.
    const bool own = flash && flash->FirstChildElement("PhoenixSoundCompression");
    if (!own && flash && flash->FirstChildElement("StreamCompress"))
    {
        const Settings::SoundFormat stream =
            soundFormat(numberOf(flash, "StreamFormat", 0), numberOf(flash, "StreamCompress", 7));
        settings.soundCompression = stream.compression;
        settings.soundBitRate = stream.bitRate;
        settings.soundAdpcmBits = stream.adpcmBits;
        settings.soundRate = stream.rate;
        settings.soundStereo = stream.stereo;
    }
    settings.soundCompression = textOf(flash, "PhoenixSoundCompression", settings.soundCompression);
    if (settings.soundCompression != "adpcm" && settings.soundCompression != "raw" && settings.soundCompression != "none")
        settings.soundCompression = "mp3";
    settings.soundBitRate = numberOf(flash, "PhoenixSoundBitRate", settings.soundBitRate);
    settings.soundAdpcmBits = std::clamp(numberOf(flash, "PhoenixSoundAdpcmBits", settings.soundAdpcmBits), 2, 5);
    settings.soundRate = numberOf(flash, "PhoenixSoundRate", settings.soundRate);
    settings.soundStereo = flagOf(flash, "PhoenixSoundStereo", settings.soundStereo);
    settings.event = {settings.soundCompression, settings.soundBitRate, settings.soundAdpcmBits, settings.soundRate,
        settings.soundStereo};
    if (!own && flash && flash->FirstChildElement("EventCompress"))
        settings.event = soundFormat(numberOf(flash, "EventFormat", 0), numberOf(flash, "EventCompress", 7));
    for (const XMLElement* group = profile->FirstChildElement("PublishProperties"); group;
         group = group->NextSiblingElement("PublishProperties"))
    {
        const char* id = group->Attribute("id");
        if (!id || std::strcmp(id, kSvgId) != 0)
            continue;
        settings.svgHiddenLayers = propertyOf(group, "includeHiddenLayers", "true") == "true";
        settings.svgEmbed = propertyOf(group, "embed", "false") == "true";
        if (propertyOf(group, "default", "true") != "true")
            settings.svgFile = propertyOf(group, "filename", settings.svgFile);
    }
    // HTML5: the Canvas format's group, else Phoenix's own.
    const XMLElement* canvas = group(profile, kCanvasId);
    const XMLElement* html5 = canvas ? canvas : group(profile, kHtml5Id);
    if (html5)
    {
        const char* enabled = html5->Attribute("enabled");
        settings.html5 = canvas ? !enabled || std::strcmp(enabled, "false") != 0 : enabled && std::strcmp(enabled, "true") == 0;
        if (propertyOf(html5, "default", "true") != "true")
            settings.html5File = propertyOf(html5, "filename", settings.html5File);
        settings.html5Loop = propertyOf(html5, "loop", "true") != "false";
        settings.html5Fit = propertyOf(html5, canvas ? "phoenixScale" : "scale", "fit") != "none";
    }
    return settings;
}

Settings::SoundFormat soundFormat(int format, int compress)
{
    Settings::SoundFormat sound;
    static const int kRates[4] = {5512, 11025, 22050, 44100};
    static const int kBitRates[12] = {8, 16, 20, 24, 32, 48, 56, 64, 80, 112, 128, 160};
    if (compress == 0 || (compress >= 2 && compress <= 5) || compress == 18)
    {
        sound.compression = compress == 0 ? "raw" : "adpcm";
        sound.adpcmBits = compress >= 2 && compress <= 5 ? compress : 4;
        sound.rate = kRates[(format >> 2) & 3];
        sound.stereo = compress != 18 && (format & 1) != 0;
    }
    else if (compress == 1)
        sound.compression = "none";
    else if (compress >= 6 && compress <= 17)
    {
        sound.compression = "mp3";
        sound.bitRate = kBitRates[compress - 6];
        // The rate the bit rate calls for; stereo from 20 kbps.
        sound.rate = sound.bitRate <= 24 ? 11025 : sound.bitRate <= 56 ? 22050 : 44100;
        sound.stereo = (format & 1) == 0 && sound.bitRate >= 20;
    }
    return sound;
}

std::string write(const std::string& xml, const Settings& settings, const std::string& name)
{
    XMLDocument document;
    if (xml.empty() || document.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS || !currentProfile(document))
    {
        document.Clear();
        XMLElement* root = document.NewElement("flash_profiles");
        document.InsertEndChild(root);
        XMLElement* profile = document.NewElement("flash_profile");
        profile->SetAttribute("version", "1.0");
        profile->SetAttribute("name", settings.profile.c_str());
        profile->SetAttribute("current", "true");
        root->InsertEndChild(profile);
    }
    XMLElement* profile = currentProfile(document);
    XMLElement* formats = child(document, profile, "PublishFormatProperties");
    if (!formats->Attribute("enabled"))
        formats->SetAttribute("enabled", "true");
    setNumber(document, formats, "defaultNames", settings.defaultNames ? 1 : 0);
    setNumber(document, formats, "flash", settings.flash ? 1 : 0);
    setNumber(document, formats, "html", settings.html ? 1 : 0);
    setNumber(document, formats, "gif", settings.gif ? 1 : 0);
    setNumber(document, formats, "jpeg", settings.jpeg ? 1 : 0);
    setNumber(document, formats, "png", settings.png ? 1 : 0);
    const struct
    {
        const char* key;
        const std::string* file;
        const char* extension;
    } names[] = {{"flash", &settings.flashFile, ".swf"}, {"html", &settings.htmlFile, ".html"},
        {"gif", &settings.gifFile, ".gif"}, {"jpeg", &settings.jpegFile, ".jpg"}, {"png", &settings.pngFile, ".png"}};
    for (const auto& entry : names)
    {
        const std::string own = std::string(entry.key) + "DefaultName";
        setNumber(document, formats, own.c_str(), settings.defaultNames ? 1 : 0);
        const std::string tag = std::string(entry.key) + "FileName";
        setText(document, formats, tag.c_str(), settings.defaultNames ? name + entry.extension : *entry.file);
    }
    svgSwitch(document, formats)->SetText(settings.svg ? "1" : "0");
    setOptionalText(document, formats, "PhoenixParentFolder", settings.parentFolder);
    setOptionalText(document, formats, "PhoenixPublishFolder", settings.publishFolder);

    const auto sized = [&](const char* tag) {
        XMLElement* group = child(document, profile, tag);
        if (!group->Attribute("enabled"))
            group->SetAttribute("enabled", "true");
        setNumber(document, group, "MatchMovieDim", settings.matchMovie ? 1 : 0);
        if (settings.width > 0)
            setNumber(document, group, "Width", settings.width);
        if (settings.height > 0)
            setNumber(document, group, "Height", settings.height);
        return group;
    };
    XMLElement* png = sized("PublishPNGProperties");
    setText(document, png, "BitDepth", settings.pngBitDepth);
    setNumber(document, png, "MaxColors", settings.pngColors);
    XMLElement* jpeg = sized("PublishJpegProperties");
    setNumber(document, jpeg, "Quality", settings.jpegQuality);
    setNumber(document, jpeg, "Progressive", settings.jpegProgressive ? 1 : 0);
    XMLElement* gif = sized("PublishGifProperties");
    setNumber(document, gif, "Animated", settings.gifAnimated ? 1 : 0);
    setNumber(document, gif, "Loop", settings.gifLoop ? 1 : 0);
    setText(document, gif, "LoopCount", settings.gifLoopCount > 0 ? std::to_string(settings.gifLoopCount) : std::string());
    setNumber(document, gif, "MaxColors", settings.gifColors);
    XMLElement* flash = child(document, profile, "PublishFlashProperties");
    if (!flash->Attribute("enabled"))
        flash->SetAttribute("enabled", "true");
    setText(document, flash, "PhoenixSoundCompression", settings.soundCompression);
    setNumber(document, flash, "PhoenixSoundBitRate", settings.soundBitRate);
    setNumber(document, flash, "PhoenixSoundAdpcmBits", settings.soundAdpcmBits);
    setNumber(document, flash, "PhoenixSoundRate", settings.soundRate);
    setNumber(document, flash, "PhoenixSoundStereo", settings.soundStereo ? 1 : 0);
    XMLElement* svg = properties(document, profile, kSvgId, "SVG Image");
    setProperty(document, svg, "default", settings.defaultNames ? "true" : "false");
    setProperty(document, svg, "filename", settings.defaultNames ? name + ".svg" : settings.svgFile);
    setProperty(document, svg, "embed", settings.svgEmbed ? "true" : "false");
    setProperty(document, svg, "includeHiddenLayers", settings.svgHiddenLayers ? "true" : "false");
    // HTML5 goes in the Canvas format's group when there is one; Phoenix's
    // own is made only once HTML5 is asked for.
    XMLElement* canvas = group(profile, kCanvasId);
    XMLElement* html5 = canvas ? canvas : group(profile, kHtml5Id);
    if (!html5 && settings.html5)
        html5 = properties(document, profile, kHtml5Id, "HTML5");
    if (html5)
    {
        html5->SetAttribute("enabled", settings.html5 ? "true" : "false");
        setProperty(document, html5, "default", settings.defaultNames ? "true" : "false");
        setProperty(document, html5, "filename", settings.defaultNames ? fileName(settings, "html5", name) : settings.html5File);
        setProperty(document, html5, "loop", settings.html5Loop ? "true" : "false");
        setProperty(document, html5, canvas ? "phoenixScale" : "scale", settings.html5Fit ? "fit" : "none");
    }

    tinyxml2::XMLPrinter printer;
    document.Print(&printer);
    return printer.CStr();
}

std::string fileName(const Settings& settings, const std::string& format, const std::string& name)
{
    if (settings.defaultNames)
    {
        if (format == "flash")
            return name + ".swf";
        if (format == "jpeg")
            return name + ".jpg";
        if (format == "html5")
            return name + (settings.html ? "_html5.html" : ".html");
        return name + "." + format;
    }
    if (format == "flash")
        return settings.flashFile;
    if (format == "html")
        return settings.htmlFile;
    if (format == "png")
        return settings.pngFile;
    if (format == "jpeg")
        return settings.jpegFile;
    if (format == "gif")
        return settings.gifFile;
    if (format == "html5")
        return settings.html5File;
    return settings.svgFile;
}

} // namespace fla::publish
