#pragma once

#include <QDialog>

#include <functional>
#include <string>

class QListWidget;
class QPushButton;

/// Animate's Motion Presets: motions saved by name to use again, kept in the
/// settings. A preset is a motion tween's AnimationCore; Animate's own
/// preset files (.xml) import as they are, one at a time or a folder of them.
class MotionPresetsDialog : public QDialog
{
    Q_OBJECT

public:
    /// \a current gives the motion of the tween in hand (empty when there is
    /// none); \a apply pastes a motion onto the selection, false when it could
    /// not.
    MotionPresetsDialog(std::function<std::string()> current, std::function<bool(const std::string&)> apply,
        QWidget* parent = nullptr);

private:
    void reload();
    void saveCurrent();
    void applySelected();
    void removeSelected();
    void importFiles();
    void importFolder();
    void exportSelected();
    /// Adds a preset from a file's text; false when it holds no motion.
    bool addPreset(const QString& name, const std::string& text);

    std::function<std::string()> _current;
    std::function<bool(const std::string&)> _apply;
    QListWidget* _list = nullptr;
    QPushButton* _applyButton = nullptr;
    QPushButton* _deleteButton = nullptr;
    QPushButton* _exportButton = nullptr;
};
