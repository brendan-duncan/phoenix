#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>

class QTimer;

namespace fla {
class Document;
}

/// Saving work in the background so a crash loses little of it, and
/// offering it back on the next start. While a document has unsaved
/// changes, a copy is written every few minutes as a Phoenix document in
/// Phoenix's recovery folder, with a note of where the document came from
/// and which Phoenix wrote it. Saving or closing the document removes it; a
/// copy whose Phoenix is no longer running was left by a crash.
class AutoSave : public QObject
{
    Q_OBJECT

public:
    struct Links
    {
        /// The document open now, or null.
        std::function<const fla::Document*()> document;
        /// Whether it has changes not saved.
        std::function<bool()> modified;
        /// Its file and name, for the note.
        std::function<QString()> path;
        std::function<QString()> name;
        /// Whether the person is in the middle of something (a drag), when
        /// a save would stall them; the save waits.
        std::function<bool()> busy;
    };

    AutoSave(Links links, QObject* parent = nullptr);
    ~AutoSave() override;

    /// Something was edited: the next autosave has something to write.
    void noteChanged() { ++_changes; }

    /// The document was saved or closed: its recovery copy goes.
    void discard();

    /// Minutes between copies; 0 turns autosaving off.
    static int interval();
    static void setInterval(int minutes);

    /// A recovery copy left by a Phoenix that is no longer running.
    struct Orphan
    {
        QString file;          // the recovery .phx
        QString note;          // its note
        QString originalPath;  // where the document came from, if anywhere
        QString name;
        QDateTime saved;
    };
    static QList<Orphan> orphans();
    /// Removes an orphan's files.
    static void remove(const Orphan& orphan);

    /// Writes the copy now, when there is something to write.
    void saveNow();

private:
    static QString folder();

    Links _links;
    QTimer* _timer = nullptr;
    QString _id;
    quint64 _changes = 0;
    quint64 _savedChanges = 0;
    QDateTime _lastSave;
};
