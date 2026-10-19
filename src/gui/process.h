#pragma once

// Running another program: ffmpeg for video, Rhubarb for lip sync, Phoenix
// itself for a render, a browser or file manager. On the desktop this is
// QProcess. A browser runs no programs, so the web build has a stand-in
// with QProcess's interface whose programs never start: every wait fails,
// and the features that need one report that they could not run it.

#ifndef PHOENIX_WEB

#include <QProcess>

using Process = QProcess;

#else

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

class Process : public QObject
{
    Q_OBJECT

public:
    enum ProcessState { NotRunning, Starting, Running };
    enum ExitStatus { NormalExit, CrashExit };
    enum ProcessChannelMode { SeparateChannels, MergedChannels, ForwardedChannels };

    explicit Process(QObject* parent = nullptr) : QObject(parent) {}

    void start(const QString&, const QStringList& = {}) {}
    static bool startDetached(const QString&, const QStringList& = {}) { return false; }
    bool waitForStarted(int = 30000) { return false; }
    bool waitForFinished(int = 30000) { return false; }
    bool waitForBytesWritten(int = 30000) { return false; }
    ProcessState state() const { return NotRunning; }
    int exitCode() const { return -1; }
    ExitStatus exitStatus() const { return CrashExit; }
    void kill() {}
    void close() {}
    qint64 write(const QByteArray&) { return -1; }
    qint64 write(const char*, qint64) { return -1; }
    qint64 bytesToWrite() const { return 0; }
    void closeWriteChannel() {}
    QByteArray readAll() { return {}; }
    QByteArray readAllStandardOutput() { return {}; }
    QByteArray readAllStandardError() { return {}; }
    void setProcessChannelMode(ProcessChannelMode) {}
    void setProcessEnvironment(const QProcessEnvironment&) {}
    static QStringList splitCommand(QStringView command) { return QProcess::splitCommand(command); }

signals:
    void finished(int exitCode, Process::ExitStatus exitStatus);
};

#endif
