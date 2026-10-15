#pragma once

#include <QDialog>
#include <QStringList>

class QCheckBox;
class QListWidget;

/// The window Phoenix opens on when started without a file, its Home
/// screen: new documents from a few presets or the full New Document
/// dialog, Open, and the files worked on lately.
class StartDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Choice
    {
        None,
        /// A new document of preset preset().
        NewPreset,
        /// The New Document dialog, for any size.
        NewCustom,
        /// The Open dialog.
        Browse,
        /// The recent file path().
        OpenRecent,
        /// Help > Tutorials, the guided lessons.
        Tutorials,
    };

    StartDialog(const QStringList& recentFiles, QWidget* parent = nullptr);

    Choice choice() const { return _choice; }
    /// Which of NewDocumentDialog::presets() was picked.
    int preset() const { return _preset; }
    QString path() const { return _path; }

    /// Whether to show it the next time Phoenix starts.
    bool showAtStartup() const;

private:
    void choose(Choice choice);

    Choice _choice = Choice::None;
    int _preset = -1;
    QString _path;
    QListWidget* _presets = nullptr;
    QListWidget* _recent = nullptr;
    QCheckBox* _showAtStartup = nullptr;
};
