#pragma once

#include "command.h"

#include <cstdint>
#include <string>
#include <utility>

namespace fla {

/// Sets one field of a document object, undoably.
///
/// The Properties panel edits fields one at a time: a frame's ease, an
/// instance's blend mode, the document's frame rate. One template covers them
/// all rather than a command per field. With a merge id, consecutive sets of
/// the same field under the same name fold into one undo step, which is what
/// a dragged hot text wants.
template <typename T>
class SetPropertyCommand : public Command
{
public:
    SetPropertyCommand(T& target, const T& before, const T& after, const std::string& name,
        int mergeId = -1)
        : _target(&target)
        , _before(before)
        , _after(after)
        , _name(name)
        , _mergeId(mergeId)
    {
    }

    void redo() override { *_target = _after; }

    void undo() override { *_target = _before; }

    std::string name() const override { return _name; }

    int mergeId() const override { return _mergeId; }

    bool mergeWith(const Command* other) override
    {
        const auto* same = dynamic_cast<const SetPropertyCommand<T>*>(other);
        if (!same || same->_target != _target || same->_name != _name)
            return false;
        _after = same->_after;
        return true;
    }

private:
    T* _target;
    T _before;
    T _after;
    std::string _name;
    int _mergeId;
};

/// Puts a new object in a slot that owns its object (a primitive's fill or
/// stroke), keeping the one taken out for undo. Whichever is out of the
/// document belongs to the command and goes with it.
template <typename T>
class ReplaceOwnedCommand : public Command
{
public:
    ReplaceOwnedCommand(T*& slot, T* after, const std::string& name)
        : _slot(&slot)
        , _other(after)
        , _name(name)
    {
    }

    ~ReplaceOwnedCommand() override { delete _other; }

    ReplaceOwnedCommand(const ReplaceOwnedCommand&) = delete;
    ReplaceOwnedCommand& operator=(const ReplaceOwnedCommand&) = delete;

    void redo() override { std::swap(*_slot, _other); }

    void undo() override { std::swap(*_slot, _other); }

    std::string name() const override { return _name; }

private:
    T** _slot;
    T* _other;
    std::string _name;
};

/// Sets an RGBA color stored as four bytes, which the document and the fills
/// keep as plain arrays rather than a copyable value.
class SetColorCommand : public Command
{
public:
    SetColorCommand(uint8_t* target, const uint8_t before[4], const uint8_t after[4],
        const std::string& name)
        : _target(target)
        , _name(name)
    {
        for (int i = 0; i < 4; ++i)
        {
            _before[i] = before[i];
            _after[i] = after[i];
        }
    }

    void redo() override { copy(_after); }

    void undo() override { copy(_before); }

    std::string name() const override { return _name; }

private:
    void copy(const uint8_t from[4])
    {
        for (int i = 0; i < 4; ++i)
            _target[i] = from[i];
    }

    uint8_t* _target;
    uint8_t _before[4];
    uint8_t _after[4];
    std::string _name;
};

/// The merge id the Properties panel gives the sets made during one drag of a
/// hot text, so they land as one undo step. Distinct from the element
/// commands' ids.
constexpr int kPropertyDragMergeId = 100;

} // namespace fla
