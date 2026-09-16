#pragma once

#include "command.h"

#include "../data/transform.h"

#include <string>

namespace fla {

class Element;

/// Replaces an element's transform.
///
/// Moving, scaling, rotating and skewing a whole object are all the same edit
/// underneath -- only the matrix and the name differ -- so they share one
/// command rather than four near-identical ones.
///
/// A drag applies its transform live and pushes a single command on release, so
/// the whole gesture is one undo step. Merging exists for the other case:
/// repeated arrow-key nudges, which should also collapse into one step.
class SetElementTransformCommand : public Command
{
public:
    /// \a name is the gesture as the user would describe it ("Move", "Scale"),
    /// and becomes the Edit menu text.
    SetElementTransformCommand(Element* element, const Transform& before,
        const Transform& after, const std::string& name);

    void redo() override;

    void undo() override;

    std::string name() const override { return _name; }

    int mergeId() const override;

    /// Folds a later transform of the same element under the same gesture name
    /// into this one, keeping the original starting point.
    bool mergeWith(const Command* other) override;

    const Element* element() const { return _element; }

private:
    Element* _element;
    Transform _before;
    Transform _after;
    std::string _name;
};

} // namespace fla
