#pragma once

#include <QColor>
#include <QDialog>

class QCheckBox;
class QComboBox;
class HotSpinBox;
class QToolButton;

/// Edit > Preferences: what Phoenix keeps between runs that is not set
/// where it is used. Read through the functions here, which keep the
/// values to hand so a hot path such as picking reads no settings.
namespace Preferences {

/// Steps of undo history kept; 0 for no limit.
int undoLevels();
/// How near, in screen pixels, a click must be to pick a line or a point.
double clickPixels();
/// How near, in screen pixels, a pencil line's ends must be to be joined.
double connectPixels();
/// The pen draws the next segment to the cursor before the click.
bool penPreview();
/// The pen's anchors drawn filled rather than hollow.
bool solidPoints();
/// A selection is highlighted in its layer's color, else highlightColor().
bool layerHighlight();
QColor highlightColor();

/// Reads the settings again, after the dialog has written them.
void reload();

} // namespace Preferences

class PreferencesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PreferencesDialog(QWidget* parent = nullptr);

    /// Writes the choices to the settings and reloads them.
    void save();

private:
    HotSpinBox* _undoLevels;
    QComboBox* _autoSave;
    QCheckBox* _startScreen;
    QCheckBox* _reopenDocuments;
    QComboBox* _highlight;
    QToolButton* _highlightColor;
    QColor _color;
    QComboBox* _clickAccuracy;
    QComboBox* _connectLines;
    QCheckBox* _penPreview;
    QCheckBox* _solidPoints;
    QCheckBox* _mcp;
    HotSpinBox* _mcpPort;
};
