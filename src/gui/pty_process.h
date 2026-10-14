#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

/// A program running in a pseudo-terminal, as a terminal window runs one:
/// what it writes (text and the terminal's escape codes) comes as output,
/// what is typed goes in with write(), and resize() tells it the size of
/// the terminal. ConPTY on Windows (10 1809 and later), forkpty elsewhere.
class PtyProcess : public QObject
{
    Q_OBJECT

public:
    explicit PtyProcess(QObject* parent = nullptr);
    ~PtyProcess() override;

    /// Starts \a program in \a workingDirectory on a terminal of \a columns
    /// by \a rows. False with \a error when it cannot.
    bool start(const QString& program, const QStringList& arguments, const QString& workingDirectory, int columns,
        int rows, QString& error);
    bool isRunning() const;

    void write(const QByteArray& data);
    void resize(int columns, int rows);
    /// Ends the program, and the terminal with it.
    void kill();

signals:
    void output(const QByteArray& data);
    /// The program has ended.
    void finished(int exitCode);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
