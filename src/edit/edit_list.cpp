#include "edit_list.h"

#include "../third_party/tinyxml2/tinyxml2.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace fla {

namespace editlist {

namespace {

std::string trimmed(const std::string& text)
{
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return std::string();
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

/// A file URL or path as a path.
std::string pathOf(std::string url)
{
    if (url.compare(0, 17, "file://localhost/") == 0)
        url = url.substr(17);
    else if (url.compare(0, 8, "file:///") == 0)
        url = url.substr(8);
    else if (url.compare(0, 7, "file://") == 0)
        url = url.substr(7);
    std::string out;
    for (size_t i = 0; i < url.size(); ++i)
    {
        if (url[i] == '%' && i + 2 < url.size())
        {
            out += static_cast<char>(std::strtol(url.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        }
        else
        {
            out += url[i];
        }
    }
    return out;
}

int childInt(const tinyxml2::XMLElement* element, const char* name, int fallback)
{
    const tinyxml2::XMLElement* child = element ? element->FirstChildElement(name) : nullptr;
    return child && child->GetText() ? std::atoi(child->GetText()) : fallback;
}

std::string childText(const tinyxml2::XMLElement* element, const char* name)
{
    const tinyxml2::XMLElement* child = element ? element->FirstChildElement(name) : nullptr;
    return child && child->GetText() ? std::string(child->GetText()) : std::string();
}

} // namespace

int timecodeFrames(const std::string& timecode, double frameRate)
{
    int parts[4] = {0, 0, 0, 0};
    int count = 0;
    std::string field;
    for (size_t i = 0; i <= timecode.size(); ++i)
    {
        const char c = i < timecode.size() ? timecode[i] : ':';
        if (c == ':' || c == ';' || c == '.')
        {
            if (field.empty() || count >= 4)
                return -1;
            parts[count++] = std::atoi(field.c_str());
            field.clear();
        }
        else if (c >= '0' && c <= '9')
        {
            field += c;
        }
        else
        {
            return -1;
        }
    }
    if (count != 4)
        return -1;
    const int fps = std::max(1, static_cast<int>(std::lround(frameRate)));
    return ((parts[0] * 60 + parts[1]) * 60 + parts[2]) * fps + parts[3];
}

std::string shotName(const Event& event)
{
    std::string name = event.name.empty() ? event.file : event.name;
    const size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos)
        name = name.substr(slash + 1);
    const size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0 && name.size() - dot <= 5)
        name = name.substr(0, dot);
    return trimmed(name);
}

EditList parseEdl(const std::string& text, double frameRate)
{
    EditList list;
    list.frameRate = 0.0;
    std::istringstream in(text);
    std::string line;
    Event* current = nullptr;
    std::vector<Event> events;
    int earliest = -1;
    while (std::getline(in, line))
    {
        line = trimmed(line);
        if (line.empty())
            continue;
        if (line.compare(0, 6, "TITLE:") == 0)
        {
            list.title = trimmed(line.substr(6));
            continue;
        }
        if (line[0] == '*')
        {
            // Comments name the clip and its file.
            const std::string comment = trimmed(line.substr(1));
            const std::string clip = "FROM CLIP NAME:";
            const std::string file = "SOURCE FILE:";
            if (current && comment.compare(0, clip.size(), clip) == 0)
                current->name = trimmed(comment.substr(clip.size()));
            else if (current && comment.compare(0, file.size(), file) == 0)
                current->file = trimmed(comment.substr(file.size()));
            continue;
        }
        // 001  REEL  V  C  srcIn srcOut recIn recOut
        std::istringstream fields(line);
        std::vector<std::string> parts;
        for (std::string part; fields >> part;)
            parts.push_back(part);
        if (parts.size() < 8 || !std::all_of(parts[0].begin(), parts[0].end(), ::isdigit))
            continue;
        const size_t n = parts.size();
        const int sourceIn = timecodeFrames(parts[n - 4], frameRate);
        const int recordIn = timecodeFrames(parts[n - 2], frameRate);
        const int recordOut = timecodeFrames(parts[n - 1], frameRate);
        if (sourceIn < 0 || recordIn < 0 || recordOut <= recordIn)
            continue;
        Event event;
        event.name = parts[1];
        const std::string& track = parts[2];
        event.audio = !track.empty() && track[0] == 'A';
        event.track = event.audio && track.size() > 1 && std::isdigit(static_cast<unsigned char>(track[1])) ? std::atoi(track.c_str() + 1) : 1;
        event.start = recordIn;
        event.end = recordOut;
        event.sourceIn = sourceIn;
        events.push_back(event);
        current = &events.back();
        earliest = earliest < 0 ? recordIn : std::min(earliest, recordIn);
    }
    // The cut starts at the first frame, whatever hour the timecode starts
    // at (usually 01:00:00:00).
    for (Event& event : events)
    {
        event.start -= earliest;
        event.end -= earliest;
    }
    list.events = events;
    return list;
}

EditList parseXml(const std::string& text, std::string* error)
{
    EditList list;
    tinyxml2::XMLDocument xml;
    if (xml.Parse(text.data(), text.size()) != tinyxml2::XML_SUCCESS)
    {
        if (error)
            *error = xml.ErrorStr();
        return list;
    }
    const tinyxml2::XMLElement* root = xml.RootElement();
    const tinyxml2::XMLElement* sequence = root ? root->FirstChildElement("sequence") : nullptr;
    if (!sequence && root)
        if (const tinyxml2::XMLElement* project = root->FirstChildElement("project"))
            if (const tinyxml2::XMLElement* children = project->FirstChildElement("children"))
                sequence = children->FirstChildElement("sequence");
    if (!sequence)
    {
        if (error)
            *error = "No sequence in the XML.";
        return list;
    }
    list.title = childText(sequence, "name");
    if (const tinyxml2::XMLElement* rate = sequence->FirstChildElement("rate"))
    {
        list.frameRate = childInt(rate, "timebase", 24);
        if (childText(rate, "ntsc") == "TRUE")
            list.frameRate = list.frameRate * 1000.0 / 1001.0;
    }
    const tinyxml2::XMLElement* media = sequence->FirstChildElement("media");
    for (const char* kind : {"video", "audio"})
    {
        const tinyxml2::XMLElement* section = media ? media->FirstChildElement(kind) : nullptr;
        int trackNumber = 0;
        for (const tinyxml2::XMLElement* track = section ? section->FirstChildElement("track") : nullptr; track;
             track = track->NextSiblingElement("track"))
        {
            ++trackNumber;
            for (const tinyxml2::XMLElement* clip = track->FirstChildElement("clipitem"); clip;
                 clip = clip->NextSiblingElement("clipitem"))
            {
                Event event;
                event.audio = std::strcmp(kind, "audio") == 0;
                event.track = trackNumber;
                event.name = childText(clip, "name");
                event.start = childInt(clip, "start", -1);
                event.end = childInt(clip, "end", -1);
                event.sourceIn = std::max(0, childInt(clip, "in", 0));
                if (const tinyxml2::XMLElement* file = clip->FirstChildElement("file"))
                    event.file = pathOf(childText(file, "pathurl"));
                if (event.start < 0 || event.end <= event.start)
                    continue;
                list.events.push_back(event);
            }
        }
    }
    return list;
}

EditList parse(const std::string& text, double frameRate, std::string* error)
{
    const size_t first = text.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
    if (first != std::string::npos && text[first] == '<')
        return parseXml(text, error);
    EditList list = parseEdl(text, frameRate);
    if (list.events.empty() && error)
        *error = "No events in the edit list.";
    return list;
}

} // namespace editlist

} // namespace fla
