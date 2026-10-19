#pragma once

#include <QDialog>
#include <QStringList>

class QCheckBox;
class QLabel;
class QListWidget;
class QPushButton;
class QVBoxLayout;

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
        /// A copy of the starter template starterTemplate().
        StarterTemplate,
    };

    StartDialog(const QStringList& recentFiles, QWidget* parent = nullptr);

    Choice choice() const { return _choice; }
    /// Which of NewDocumentDialog::presets() was picked.
    int preset() const { return _preset; }
    QString path() const { return _path; }

    /// Whether to show it the next time Phoenix starts.
    bool showAtStartup() const;

    /// The starter template picked, by id.
    QString starterTemplate() const { return _template; }
    /// The Simple and Full layout buttons: \a current marked, with a word
    /// for someone new when \a firstTime.
    void setWorkspace(const QString& current, bool firstTime);

signals:
    /// Simple or Full was clicked: the window takes that workspace.
    void workspaceChosen(const QString& name);

private:
    void choose(Choice choice);
    void addStarterTemplates(QVBoxLayout* layout);
    void addWorkspaceChoice(QVBoxLayout* layout);

    Choice _choice = Choice::None;
    int _preset = -1;
    QString _path;
    QListWidget* _presets = nullptr;
    QListWidget* _recent = nullptr;
    QCheckBox* _showAtStartup = nullptr;
    QString _template;
    QListWidget* _templates = nullptr;
    QPushButton* _simple = nullptr;
    QPushButton* _full = nullptr;
    QLabel* _workspaceNote = nullptr;
};
