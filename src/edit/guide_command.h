#pragma once

#include "command.h"

#include "../data/timeline.h"

#include <string>
#include <utility>
#include <vector>

namespace fla {

/// Sets a timeline's ruler guides: adding, moving, removing or clearing
/// them is one undoable step, and leaves the document with changes to save,
/// as the guides are saved with it.
class SetGuidesCommand : public Command
{
public:
    SetGuidesCommand(Timeline* timeline, std::vector<double> guidesX, std::vector<double> guidesY,
        std::string name)
        : _timeline(timeline)
        , _beforeX(timeline->guidesX)
        , _beforeY(timeline->guidesY)
        , _afterX(std::move(guidesX))
        , _afterY(std::move(guidesY))
        , _name(std::move(name))
    {}

    void redo() override
    {
        _timeline->guidesX = _afterX;
        _timeline->guidesY = _afterY;
    }

    void undo() override
    {
        _timeline->guidesX = _beforeX;
        _timeline->guidesY = _beforeY;
    }

    std::string name() const override { return _name; }

private:
    Timeline* _timeline;
    std::vector<double> _beforeX;
    std::vector<double> _beforeY;
    std::vector<double> _afterX;
    std::vector<double> _afterY;
    std::string _name;
};

} // namespace fla
