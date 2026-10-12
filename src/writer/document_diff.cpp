#include "document_diff.h"

#include "xfl_content.h"

#include "../data/document.h"

#include <algorithm>
#include <map>
#include <sstream>

namespace fla {

namespace {

/// What each part is called in a report.
std::string itemName(const std::string& path)
{
    if (path == "DOMDocument.xml")
        return "Scenes";
    const std::string library = "LIBRARY/";
    if (path.compare(0, library.size(), library) == 0)
    {
        std::string name = path.substr(library.size());
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".xml") == 0)
            name.resize(name.size() - 4);
        return "Symbol: " + name;
    }
    if (path.compare(0, 4, "bin/") == 0)
        return "Media: " + path;
    return "File: " + path;
}

std::vector<std::string> lines(const std::string& text)
{
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);)
        out.push_back(line);
    return out;
}

/// How many lines one text has that the other does not, as a multiset.
int changedLines(const std::string& a, const std::string& b)
{
    std::map<std::string, int> counts;
    for (const std::string& line : lines(a))
        ++counts[line];
    for (const std::string& line : lines(b))
        --counts[line];
    int changed = 0;
    for (const auto& [line, count] : counts)
        changed += std::abs(count);
    return changed;
}

bool contents(const Document& document, std::map<std::string, const XFLEntry*>& byPath,
    std::vector<XFLEntry>& entries, std::string* error)
{
    std::vector<std::string> unsupported;
    std::string problem;
    if (!XFLContent::build(document, std::string(), entries, unsupported, problem))
    {
        if (error)
            *error = problem;
        return false;
    }
    // The package's metadata (XMP, publish history) changes with every
    // save in Animate and says nothing about the art.
    for (const XFLEntry& entry : entries)
        if (entry.path.compare(0, 9, "META-INF/") != 0)
            byPath[entry.path] = &entry;
    return true;
}

} // namespace

const char* changeName(DocumentChange::Kind kind)
{
    switch (kind)
    {
    case DocumentChange::Kind::Added: return "added";
    case DocumentChange::Kind::Removed: return "removed";
    case DocumentChange::Kind::Changed: return "changed";
    }
    return "changed";
}

std::vector<DocumentChange> compareDocuments(const Document& before, const Document& after, std::string* error)
{
    std::vector<DocumentChange> changes;
    std::vector<XFLEntry> beforeEntries, afterEntries;
    std::map<std::string, const XFLEntry*> a, b;
    if (!contents(before, a, beforeEntries, error) || !contents(after, b, afterEntries, error))
        return changes;
    for (const auto& [path, entry] : a)
    {
        const auto other = b.find(path);
        if (other == b.end())
        {
            changes.push_back({DocumentChange::Kind::Removed, itemName(path), 0});
            continue;
        }
        const XFLEntry* next = other->second;
        if (entry->isBinary() || next->isBinary())
        {
            const bool same = entry->isBinary() && next->isBinary() && *entry->binary == *next->binary;
            if (!same)
                changes.push_back({DocumentChange::Kind::Changed, itemName(path), 0});
        }
        else if (entry->text != next->text)
        {
            changes.push_back({DocumentChange::Kind::Changed, itemName(path), changedLines(entry->text, next->text)});
        }
    }
    for (const auto& [path, entry] : b)
        if (!a.count(path))
            changes.push_back({DocumentChange::Kind::Added, itemName(path), 0});
    std::stable_sort(changes.begin(), changes.end(),
        [](const DocumentChange& x, const DocumentChange& y) { return x.item < y.item; });
    return changes;
}

} // namespace fla
