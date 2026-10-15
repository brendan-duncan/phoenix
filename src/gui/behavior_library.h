#pragma once

#include "../jsfl/behavior_engine.h"

#include <QObject>
#include <QSet>
#include <QString>

#include <functional>
#include <memory>

class QFileSystemWatcher;

/// The behaviors Phoenix knows: the built-in ones and the files in the
/// Behaviors folder of the script configuration folder, read again when
/// that folder changes. One for the whole app, shared by every window's
/// stage, Test Movie and exports, so a frame comes out the same everywhere.
class BehaviorLibrary : public QObject
{
    Q_OBJECT

public:
    static BehaviorLibrary& instance();

    fla::behaviors::Engine& engine() { return *_engine; }

    /// Configuration/Behaviors, made when missing.
    static QString folder();

    /// Reads the built-in behaviors and the folder's files again.
    void reload();

    /// Where errors go (the Output panel); each is told once, until the
    /// behaviors are read again.
    std::function<void(const QString&)> report;
    void reportOnce(const QString& message);

signals:
    /// The behaviors were read again: what is drawn may change.
    void changed();

private:
    BehaviorLibrary();

    std::unique_ptr<fla::behaviors::Engine> _engine;
    QFileSystemWatcher* _watcher = nullptr;
    QSet<QString> _told;
};
