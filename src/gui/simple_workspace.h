#pragma once

#include <string>
#include <vector>

/// The Simple workspace: the stage, the timeline, the tools most drawings
/// and animations need, and the few panels that go with them. Every other
/// panel is closed and every other tool hidden from the Tools panel (each
/// still works by its key, and Window > Workspaces > Essentials brings
/// everything back). Free of Qt, for the tests.
namespace simpleworkspace {

/// The workspace's name in Window > Workspaces.
inline const char* name()
{
    return "Simple";
}

/// The full workspace one click away from it.
inline const char* fullName()
{
    return "Essentials";
}

/// The panels it keeps open, by object name; the first of the right-hand
/// ones (Properties) is in front.
inline const std::vector<std::string>& panels()
{
    static const std::vector<std::string> kept = {
        "toolsDock", "timelineDock", "propertiesDock", "colorDock", "libraryDock"};
    return kept;
}

/// The tools it shows, by name, a slot each; "|" is a line between
/// sections.
inline const std::vector<std::vector<std::string>>& toolSlots()
{
    static const std::vector<std::vector<std::string>> kSlots = {
        {"Selection"},
        {"Free Transform"},
        {"|"},
        {"Brush"},
        {"Pencil"},
        {"Line"},
        {"Rectangle", "Oval"},
        {"Text"},
        {"|"},
        {"Paint Bucket"},
        {"Eyedropper"},
        {"Eraser"},
        {"|"},
        {"Hand"},
        {"Zoom"},
    };
    return kSlots;
}

} // namespace simpleworkspace
