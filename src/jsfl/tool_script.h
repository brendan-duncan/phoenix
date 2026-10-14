#pragma once

#include "host.h"

#include <memory>
#include <string>
#include <vector>

namespace jsfl {

/// A JSFL tool: a script in the Tools folder that a toolbox button runs.
/// Unlike a command it stays loaded, in a script context of its own, from
/// when Phoenix starts until it quits, and its functions are called as the
/// tool is used: configureTool() once, activate() and deactivate() as it is
/// picked and put down, mouseDown(), mouseMove(), mouseUp(),
/// mouseDoubleClick(), keyDown() and keyUp() as it works, setCursor() to
/// choose its cursor, and notifySettingsChanged() when its options change.
///
/// The script sees fl.tools (the pen's location, the keys held,
/// activeTool, its ToolObj) and fl.drawingLayer, which draws a preview over
/// the stage and, through a path's makeShape(), adds shapes to the
/// document. Each call that changes a document is one undo step, named
/// after the tool. Free of Qt.
class ToolScript
{
public:
    /// What a call is told about the pen and the keys, in stage
    /// coordinates.
    struct State
    {
        double penX = 0, penY = 0;
        double downX = 0, downY = 0;
        bool mouseDown = false;
        bool alt = false, ctrl = false, shift = false;
        int key = 0;
    };

    /// A setting from the tool's options file, as its Options dialog shows
    /// it: a number, a choice from a list, or a checkbox.
    struct Option
    {
        std::string name;
        std::string variable;
        std::string type; // "Number", "Strings", "Boolean", "Color"
        std::vector<std::string> list;
        double min = -1e9, max = 1e9;
        std::string value;
    };

    /// A preview path: its color (0xRRGGBB) and its pieces, each a move,
    /// line, quadratic or cubic ('M', 'L', 'Q', 'C') and its points.
    struct Preview
    {
        uint32_t color = 0;
        struct Piece
        {
            char kind = 'L';
            std::vector<double> points;
        };
        std::vector<Piece> pieces;
    };

    ToolScript(Host& host, std::string path);
    ~ToolScript();
    ToolScript(const ToolScript&) = delete;
    ToolScript& operator=(const ToolScript&) = delete;

    /// Runs the file and its configureTool(). False with \a error set when
    /// it cannot be loaded.
    bool load(std::string& error);

    /// What configureTool() set.
    const std::string& toolName() const;
    const std::string& menuString() const;
    const std::string& toolTip() const;
    /// The icon file, beside the script; empty when none.
    const std::string& iconPath() const;
    const std::string& path() const;

    bool has(const std::string& function) const;
    /// Calls \a function, when the script has it, with the pen and keys as
    /// \a state says; the error it threw ("file:line: message"), or empty.
    std::string call(const std::string& function, const State& state);

    /// The preview the drawing layer holds now; empty while nothing is
    /// drawn.
    std::vector<Preview> preview() const;
    /// The cursor setCursor() chose (Animate's numbers: 0 a plus, 1 a black
    /// arrow, 2 a white arrow, 3 a four-way arrow, 4 two-way horizontal, 5
    /// two-way vertical, 6 an X, 7 a hand).
    int cursor() const;

    /// The options and their values now; setting one tells the tool
    /// (notifySettingsChanged).
    std::vector<Option> options() const;
    std::string setOptions(const std::vector<std::pair<std::string, std::string>>& values);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace jsfl
