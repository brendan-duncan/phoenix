#include "pty_process.h"

#include <QDir>
#include <QFile>
#include <QPointer>
#include <QRegularExpression>
#include <QSocketNotifier>

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif
#endif

#ifdef _WIN32

namespace {

/// One argument as CreateProcess's command line parsing reads it back.
QString quoted(const QString& argument)
{
    if (!argument.isEmpty() && !argument.contains(QRegularExpression("[\\s\"]")))
        return argument;
    QString out = "\"";
    int slashes = 0;
    for (const QChar c : argument)
    {
        if (c == '\\')
        {
            ++slashes;
            continue;
        }
        if (c == '"')
            out += QString(slashes * 2 + 1, '\\');
        else
            out += QString(slashes, '\\');
        slashes = 0;
        out += c;
    }
    out += QString(slashes * 2, '\\');
    out += "\"";
    return out;
}

} // namespace

struct PtyProcess::Impl
{
    HPCON console = nullptr;
    HANDLE input = nullptr;   // what is typed, to the console
    HANDLE output = nullptr;  // what the console shows, from it
    PROCESS_INFORMATION process{};
    std::thread reader;
    std::thread waiter;
    std::atomic<bool> running{false};

    void close()
    {
        if (console)
        {
            // Closing the console ends what reads its output.
            ClosePseudoConsole(console);
            console = nullptr;
        }
        if (reader.joinable())
            reader.join();
        if (waiter.joinable())
            waiter.join();
        for (HANDLE* handle : {&input, &output, &process.hProcess, &process.hThread})
        {
            if (*handle)
            {
                CloseHandle(*handle);
                *handle = nullptr;
            }
        }
    }
};

PtyProcess::PtyProcess(QObject* parent)
    : QObject(parent)
    , _impl(std::make_unique<Impl>())
{}

PtyProcess::~PtyProcess()
{
    kill();
}

bool PtyProcess::start(const QString& program, const QStringList& arguments, const QString& workingDirectory,
    int columns, int rows, QString& error)
{
    if (_impl->running)
    {
        error = "It is running already.";
        return false;
    }
    _impl->close();
    HANDLE inputRead = nullptr, outputWrite = nullptr;
    if (!CreatePipe(&inputRead, &_impl->input, nullptr, 0) || !CreatePipe(&_impl->output, &outputWrite, nullptr, 0))
    {
        error = "No pipes for the terminal.";
        return false;
    }
    const COORD size{static_cast<SHORT>(std::max(columns, 2)), static_cast<SHORT>(std::max(rows, 2))};
    const HRESULT made = CreatePseudoConsole(size, inputRead, outputWrite, 0, &_impl->console);
    // The console has its own copies now.
    CloseHandle(inputRead);
    CloseHandle(outputWrite);
    if (FAILED(made))
    {
        error = "This version of Windows has no pseudo-console (Windows 10 1809 or later is needed).";
        _impl->close();
        return false;
    }

    SIZE_T listSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &listSize);
    std::vector<char> list(listSize);
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(list.data());
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &listSize) ||
        !UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, _impl->console,
            sizeof(HPCON), nullptr, nullptr))
    {
        error = "The terminal could not be given to the program.";
        _impl->close();
        return false;
    }

    // A batch file (an npm install's claude.cmd) runs through cmd.
    QString commandLine;
    if (program.endsWith(".cmd", Qt::CaseInsensitive) || program.endsWith(".bat", Qt::CaseInsensitive))
        commandLine = "cmd.exe /c " + quoted(QDir::toNativeSeparators(program));
    else
        commandLine = quoted(QDir::toNativeSeparators(program));
    for (const QString& argument : arguments)
        commandLine += " " + quoted(argument);
    std::wstring line = commandLine.toStdWString();
    std::wstring folder = QDir::toNativeSeparators(workingDirectory).toStdWString();
    const BOOL started = CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, EXTENDED_STARTUPINFO_PRESENT,
        nullptr, folder.empty() ? nullptr : folder.c_str(), &startup.StartupInfo, &_impl->process);
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    if (!started)
    {
        error = QString("%1 could not be started.").arg(QDir::toNativeSeparators(program));
        _impl->close();
        return false;
    }
    _impl->running = true;

    QPointer<PtyProcess> self(this);
    HANDLE output = _impl->output;
    _impl->reader = std::thread([self, output]() {
        char buffer[16384];
        DWORD read = 0;
        while (ReadFile(output, buffer, sizeof(buffer), &read, nullptr) && read > 0)
        {
            const QByteArray data(buffer, static_cast<int>(read));
            QMetaObject::invokeMethod(self, [self, data]() {
                if (self)
                    emit self->output(data);
            });
        }
    });
    HANDLE process = _impl->process.hProcess;
    _impl->waiter = std::thread([self, process]() {
        WaitForSingleObject(process, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(process, &code);
        QMetaObject::invokeMethod(self, [self, code]() {
            if (!self || !self->_impl->running)
                return;
            self->_impl->running = false;
            emit self->finished(static_cast<int>(code));
        });
    });
    return true;
}

bool PtyProcess::isRunning() const
{
    return _impl->running;
}

void PtyProcess::write(const QByteArray& data)
{
    if (!_impl->running || !_impl->input)
        return;
    DWORD written = 0;
    WriteFile(_impl->input, data.constData(), static_cast<DWORD>(data.size()), &written, nullptr);
}

void PtyProcess::resize(int columns, int rows)
{
    if (_impl->console)
        ResizePseudoConsole(_impl->console, COORD{static_cast<SHORT>(std::max(columns, 2)), static_cast<SHORT>(std::max(rows, 2))});
}

void PtyProcess::kill()
{
    const bool was = _impl->running;
    _impl->running = false;
    if (was && _impl->process.hProcess)
        TerminateProcess(_impl->process.hProcess, 1);
    _impl->close();
}

#else

struct PtyProcess::Impl
{
    int master = -1;
    pid_t child = -1;
    std::unique_ptr<QSocketNotifier> notifier;
    bool running = false;
};

PtyProcess::PtyProcess(QObject* parent)
    : QObject(parent)
    , _impl(std::make_unique<Impl>())
{}

PtyProcess::~PtyProcess()
{
    kill();
}

bool PtyProcess::start(const QString& program, const QStringList& arguments, const QString& workingDirectory,
    int columns, int rows, QString& error)
{
    if (_impl->running)
    {
        error = "It is running already.";
        return false;
    }
    winsize size{};
    size.ws_col = static_cast<unsigned short>(std::max(columns, 2));
    size.ws_row = static_cast<unsigned short>(std::max(rows, 2));
    // What the child needs, ready before the fork.
    const QByteArray path = QFile::encodeName(program);
    const QByteArray folder = QFile::encodeName(workingDirectory);
    std::vector<QByteArray> words{path};
    for (const QString& argument : arguments)
        words.push_back(argument.toUtf8());
    std::vector<char*> argv;
    for (QByteArray& word : words)
        argv.push_back(word.data());
    argv.push_back(nullptr);

    int master = -1;
    const pid_t child = forkpty(&master, nullptr, nullptr, &size);
    if (child < 0)
    {
        error = "No pseudo-terminal could be opened.";
        return false;
    }
    if (child == 0)
    {
        if (!folder.isEmpty() && chdir(folder.constData()) != 0)
        {
            // The shell starts where Phoenix is instead.
        }
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    _impl->master = master;
    _impl->child = child;
    _impl->running = true;
    fcntl(master, F_SETFL, fcntl(master, F_GETFL) | O_NONBLOCK);
    _impl->notifier = std::make_unique<QSocketNotifier>(master, QSocketNotifier::Read);
    connect(_impl->notifier.get(), &QSocketNotifier::activated, this, [this]() {
        char buffer[16384];
        for (;;)
        {
            const ssize_t n = ::read(_impl->master, buffer, sizeof(buffer));
            if (n > 0)
            {
                emit output(QByteArray(buffer, static_cast<int>(n)));
                continue;
            }
            if (n < 0 && (errno == EAGAIN || errno == EINTR))
                return;
            // The child has gone (EOF, or EIO once the slave side closes).
            _impl->notifier->setEnabled(false);
            int status = 0;
            waitpid(_impl->child, &status, 0);
            _impl->running = false;
            ::close(_impl->master);
            _impl->master = -1;
            emit finished(WIFEXITED(status) ? WEXITSTATUS(status) : 1);
            return;
        }
    });
    return true;
}

bool PtyProcess::isRunning() const
{
    return _impl->running;
}

void PtyProcess::write(const QByteArray& data)
{
    if (_impl->master < 0)
        return;
    const char* at = data.constData();
    qsizetype left = data.size();
    while (left > 0)
    {
        const ssize_t n = ::write(_impl->master, at, static_cast<size_t>(left));
        if (n <= 0)
        {
            if (n < 0 && (errno == EAGAIN || errno == EINTR))
                continue;
            return;
        }
        at += n;
        left -= n;
    }
}

void PtyProcess::resize(int columns, int rows)
{
    if (_impl->master < 0)
        return;
    winsize size{};
    size.ws_col = static_cast<unsigned short>(std::max(columns, 2));
    size.ws_row = static_cast<unsigned short>(std::max(rows, 2));
    ioctl(_impl->master, TIOCSWINSZ, &size);
}

void PtyProcess::kill()
{
    if (_impl->notifier)
        _impl->notifier->setEnabled(false);
    if (_impl->running && _impl->child > 0)
    {
        ::kill(_impl->child, SIGHUP);
        int status = 0;
        waitpid(_impl->child, &status, 0);
    }
    _impl->running = false;
    if (_impl->master >= 0)
    {
        ::close(_impl->master);
        _impl->master = -1;
    }
}

#endif
