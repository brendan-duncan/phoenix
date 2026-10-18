#pragma once

#include "../edit/bone_corrections.h"

#include <QObject>
#include <QString>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>

class PhoenixView;
class QWidget;

namespace fla {
class CommandStack;
class FLADocument;
class Frame;
class Shape;
class SymbolInstance;
class Timeline;
}

/// The Bone Corrections dialog and the correcting of a boned shape in place
/// (see bone_corrections.h). Opened for a bone from the Joint Rotation and
/// Spring dialog or for an armature from the Pose menu, it lists what each
/// bone drives and adds, changes and takes away corrections, each a single
/// step to undo.
///
/// A shape correction is drawn with the bone turned: the boned shape opens
/// in place on the frame shown, as the corrections there make it, and its
/// points are moved with the usual tools. Leaving the symbol keeps what was
/// drawn as the correction in full at that angle, the edits made meanwhile
/// becoming that one step.
class BoneCorrector : public QObject
{
public:
    BoneCorrector(QWidget* window, PhoenixView* view, fla::CommandStack* stack,
        std::function<void(const QString&, int)> status);
    ~BoneCorrector() override;

    /// The dialog for the armature on the pose frame \a pose (its IKTree
    /// being extra child \a child), with the bone turning about \a joint
    /// chosen (-1 for the first).
    void open(fla::Frame* pose, size_t child, int joint);

    /// Whether a shape correction is being drawn.
    bool correcting() const { return static_cast<bool>(_session); }

    /// Called when the view enters or leaves a symbol: leaving the boned
    /// shape keeps the correction drawn.
    void scopeChanged();

private:
    struct Session;

    /// Starts drawing the shape correction of \a joint on the boned shape of
    /// \a pose, on the frame shown.
    void startShape(fla::Frame* pose, size_t child, int joint);
    void finish();
    void changed();

    QWidget* _window;
    PhoenixView* _view;
    fla::CommandStack* _stack;
    std::function<void(const QString&, int)> _status;
    std::unique_ptr<Session> _session;
};
