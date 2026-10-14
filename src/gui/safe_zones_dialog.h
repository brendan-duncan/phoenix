#pragma once

#include "../edit/safe_zones.h"

#include <QDialog>

#include <functional>

class HotDoubleSpinBox;
class QCheckBox;
class QComboBox;
class QTreeWidget;

/// View > Safe Zone Settings: title and action safe, the center cross and
/// thirds, the aspect-ratio frames (each shown or not, in its color, placed
/// by its anchor, shading the stage outside it or not), and presets, built
/// in and the user's own, with the default new documents start with. Every
/// change is shown on the stage as it is made; Cancel puts it back.
class SafeZonesDialog : public QDialog
{
    Q_OBJECT

public:
    SafeZonesDialog(const fla::safezones::Settings& settings,
        std::function<void(const fla::safezones::Settings&)> preview, QWidget* parent = nullptr);

    const fla::safezones::Settings& settings() const { return _settings; }

    /// The settings new documents start with: the user's default, or the
    /// built-in defaults.
    static fla::safezones::Settings newDocumentDefault();

private:
    void load();
    void fillFrames();
    void fillFollow();
    void fillPresets();
    void changed();

    fla::safezones::Settings _settings;
    std::function<void(const fla::safezones::Settings&)> _preview;
    bool _loading = false;

    QComboBox* _presets = nullptr;
    QCheckBox* _title = nullptr;
    HotDoubleSpinBox* _titlePercent = nullptr;
    QCheckBox* _action = nullptr;
    HotDoubleSpinBox* _actionPercent = nullptr;
    QCheckBox* _cross = nullptr;
    QCheckBox* _thirds = nullptr;
    QComboBox* _follow = nullptr;
    QTreeWidget* _frames = nullptr;
};
