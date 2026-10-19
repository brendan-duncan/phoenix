#include "reference_board.h"

#include "../data/document.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace fla::references {

const char* const kAttribute = "phoenixReferences";
const char* const kFilePrefix = "bin/phoenix_reference_";

namespace {

std::string number(double value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.10g", value);
    return buffer;
}

double readNumber(const std::string& text, double fallback)
{
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    return end != text.c_str() && std::isfinite(value) ? value : fallback;
}

std::vector<std::string> split(const std::string& text, char separator)
{
    std::vector<std::string> parts;
    std::string part;
    std::istringstream stream(text);
    while (std::getline(stream, part, separator))
        parts.push_back(part);
    return parts;
}

/// Names and paths may hold anything; the separators the text uses are
/// written as %XX.
std::string escape(const std::string& text)
{
    std::string out;
    for (const char c : text)
    {
        if (c == '%' || c == ',' || c == '|' || c == ';' || c == '=' || static_cast<unsigned char>(c) < 32)
        {
            char buffer[4];
            std::snprintf(buffer, sizeof(buffer), "%%%02X", static_cast<unsigned char>(c));
            out += buffer;
        }
        else
        {
            out += c;
        }
    }
    return out;
}

std::string unescape(const std::string& text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '%' && i + 2 < text.size())
        {
            char* end = nullptr;
            const std::string pair = text.substr(i + 1, 2);
            const long value = std::strtol(pair.c_str(), &end, 16);
            if (end && *end == '\0')
            {
                out += static_cast<char>(value);
                i += 2;
                continue;
            }
        }
        out += text[i];
    }
    return out;
}

bool isBoardFile(const std::string& path)
{
    return path.rfind(kFilePrefix, 0) == 0;
}

} // namespace

bool Image::operator==(const Image& other) const
{
    return path == other.path && name == other.name && x == other.x && y == other.y && scale == other.scale &&
           rotation == other.rotation && opacity == other.opacity && locked == other.locked && hidden == other.hidden;
}

bool Board::operator==(const Board& other) const
{
    return images == other.images && hidden == other.hidden;
}

std::string toText(const Board& board)
{
    // "hide=0;images=path,x,y,scale,rotation,opacity,flags,name|..."
    std::string text = "hide=" + std::string(board.hidden ? "1" : "0") + ";images=";
    for (size_t i = 0; i < board.images.size(); ++i)
    {
        const Image& image = board.images[i];
        if (i > 0)
            text += "|";
        std::string flags;
        if (image.locked)
            flags += "l";
        if (image.hidden)
            flags += "h";
        text += escape(image.path) + "," + number(image.x) + "," + number(image.y) + "," + number(image.scale) + "," +
                number(image.rotation) + "," + number(image.opacity) + "," + flags + "," + escape(image.name);
    }
    return text;
}

Board fromText(const std::string& text)
{
    Board board;
    for (const std::string& part : split(text, ';'))
    {
        const size_t equals = part.find('=');
        if (equals == std::string::npos)
            continue;
        const std::string key = part.substr(0, equals);
        const std::string value = part.substr(equals + 1);
        if (key == "hide")
            board.hidden = value == "1";
        else if (key == "images")
        {
            for (const std::string& each : split(value, '|'))
            {
                const std::vector<std::string> fields = split(each, ',');
                if (fields.empty() || fields[0].empty())
                    continue;
                Image image;
                image.path = unescape(fields[0]);
                const auto field = [&](size_t index) { return index < fields.size() ? fields[index] : std::string(); };
                image.x = readNumber(field(1), 0.0);
                image.y = readNumber(field(2), 0.0);
                image.scale = readNumber(field(3), 1.0);
                if (!(image.scale > 0.0))
                    image.scale = 1.0;
                image.rotation = readNumber(field(4), 0.0);
                image.opacity = std::clamp(readNumber(field(5), 1.0), 0.0, 1.0);
                image.locked = field(6).find('l') != std::string::npos;
                image.hidden = field(6).find('h') != std::string::npos;
                image.name = unescape(field(7));
                board.images.push_back(image);
            }
        }
    }
    return board;
}

Board of(const Document& document)
{
    for (const auto& [key, value] : document.extraAttributes)
        if (key == kAttribute)
            return fromText(value);
    return Board();
}

Attributes withBoard(const Document& document, const Board& board)
{
    Attributes attributes;
    bool placed = false;
    const bool empty = board.images.empty();
    for (const auto& attribute : document.extraAttributes)
    {
        if (attribute.first != kAttribute)
        {
            attributes.push_back(attribute);
            continue;
        }
        if (!empty && !placed)
            attributes.emplace_back(kAttribute, toText(board));
        placed = true;
    }
    if (!empty && !placed)
        attributes.emplace_back(kAttribute, toText(board));
    return attributes;
}

Files filesFor(const Document& document, const Board& board)
{
    Files files;
    for (const auto& file : document.extraFiles)
    {
        if (isBoardFile(file.first) &&
            std::none_of(board.images.begin(), board.images.end(),
                [&](const Image& image) { return image.path == file.first; }))
            continue;
        files.push_back(file);
    }
    return files;
}

std::string newPath(const Document& document, const std::string& extension, const Files& taken)
{
    const auto used = [&](const std::string& path) {
        const auto same = [&](const auto& file) { return file.first == path; };
        return std::any_of(document.extraFiles.begin(), document.extraFiles.end(), same) ||
               std::any_of(taken.begin(), taken.end(), same);
    };
    for (int n = 1;; ++n)
    {
        const std::string path = kFilePrefix + std::to_string(n) + "." + (extension.empty() ? "png" : extension);
        if (!used(path))
            return path;
    }
}

const std::vector<uint8_t>& bytesOf(const Document& document, const std::string& path)
{
    static const std::vector<uint8_t> none;
    for (const auto& file : document.extraFiles)
        if (file.first == path)
            return file.second.get();
    return none;
}

SetBoardCommand::SetBoardCommand(Document& document, const Board& board, const Files& added, const std::string& name,
    int mergeId)
    : _document(&document)
    , _beforeAttributes(document.extraAttributes)
    , _beforeFiles(document.extraFiles)
    , _afterAttributes(withBoard(document, board))
    , _name(name)
    , _mergeId(mergeId)
{
    // The files the board will use: those already there and the new ones.
    Files files = document.extraFiles;
    for (const auto& file : added)
        files.push_back(file);
    std::swap(files, document.extraFiles);
    _afterFiles = filesFor(document, board);
    std::swap(files, document.extraFiles);
}

void SetBoardCommand::redo()
{
    _document->extraAttributes = _afterAttributes;
    _document->extraFiles = _afterFiles;
}

void SetBoardCommand::undo()
{
    _document->extraAttributes = _beforeAttributes;
    _document->extraFiles = _beforeFiles;
}

bool SetBoardCommand::mergeWith(const Command* other)
{
    const auto* same = dynamic_cast<const SetBoardCommand*>(other);
    if (!same || same->_document != _document || same->_name != _name)
        return false;
    _afterAttributes = same->_afterAttributes;
    _afterFiles = same->_afterFiles;
    return true;
}

} // namespace fla::references
