#pragma once

#include "../edit/paper_cleanup.h"

#include <QColor>
#include <QDialog>
#include <QImage>
#include <QStringList>

#include <functional>

class HotSpinBox;
class PhoenixView;
class Player;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class TimelineView;

namespace fla {
class CommandStack;
class FLADocument;
class Selection;
}

/// File > Import > Import Paper Drawings: scanned or photographed pencil
/// drawings made into vector line art, one drawing to a keyframe. One
/// window, in three steps from top to bottom: pick the drawings (one
/// picture or a numbered sequence), clean them up while watching a
/// preview (the paper made white, how dark a mark must be to count as a
/// line, specks and gaps), then say where they go.
class PaperImportDialog : public QDialog
{
    Q_OBJECT

public:
    struct Choices
    {
        QStringList files;
        fla::paper::Settings cleanup;
        /// How closely the traced outlines follow the pixels.
        double curveFit = 1.0;
        /// Onto a new layer, or the current one.
        bool newLayer = true;
        /// The frame the first drawing goes on, from 0.
        int startFrame = 0;
        /// How many frames each drawing is held: 2 for drawings on twos.
        int hold = 1;
        /// The page fitted to the stage, or a pixel to a stage pixel.
        bool fitStage = true;
        /// Each drawing's lines centered on the stage, rather than each
        /// page placed the same (which keeps drawings registered on pegs
        /// lined up).
        bool centerLines = false;
    };

    PaperImportDialog(int currentFrame, QWidget* parent = nullptr);

    Choices choices() const;

    /// A picture shrunk to the size drawings are cleaned up and traced
    /// at, so a large scan does not make thousands of tiny points.
    static QImage workImage(const QImage& image);

private:
    void filesChanged();
    void browse();
    void loadPreview();
    void updatePreview();
    void findThreshold();
    fla::paper::Settings cleanupSettings() const;

    QLineEdit* _path = nullptr;
    QCheckBox* _sequence = nullptr;
    QLabel* _found = nullptr;
    QCheckBox* _even = nullptr;
    QCheckBox* _levels = nullptr;
    HotSpinBox* _threshold = nullptr;
    HotSpinBox* _specks = nullptr;
    HotSpinBox* _gaps = nullptr;
    HotSpinBox* _smooth = nullptr;
    QPushButton* _color = nullptr;
    QColor _lineColor = Qt::black;
    QComboBox* _fit = nullptr;
    QComboBox* _layer = nullptr;
    HotSpinBox* _start = nullptr;
    HotSpinBox* _hold = nullptr;
    QComboBox* _size = nullptr;
    QComboBox* _lineUp = nullptr;
    HotSpinBox* _which = nullptr;
    QCheckBox* _showScan = nullptr;
    QLabel* _preview = nullptr;
    QLabel* _measure = nullptr;
    QPushButton* _import = nullptr;
    QStringList _files;
    QImage _work;
};

namespace PaperImport {

/// Imports the drawings as \a choices say: each cleaned up, traced and put
/// on its keyframe of the layer, as one step to undo. Returns what the
/// status bar should say; files that could not be read are added to
/// \a problems.
QString run(const PaperImportDialog::Choices& choices, fla::FLADocument& document, TimelineView& timelineView,
    fla::CommandStack& stack, fla::Selection& selection, QStringList& problems);

/// The whole of File > Import > Import Paper Drawings: the window, then the
/// import, then the stage and the timeline brought up to date. \a status
/// shows a message in the status bar.
void open(QWidget* parent, fla::FLADocument* document, PhoenixView* view, TimelineView* timelineView,
    Player* player, fla::CommandStack& stack, fla::Selection& selection, const std::function<void(const QString&)>& status);

} // namespace PaperImport
