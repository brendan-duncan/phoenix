// Publishing from scripts: document.exportSWF, document.publish and
// fl.publishDocument go through the host to Phoenix's own SWF export and
// Publish; FLfile.runCommandLine runs a command as a shell would, with no
// window of its own.

#include "extensions.h"
#include "flfile.h"

#include "../data/fla_document.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#endif

namespace jsfl {

namespace {

/// A movie is never written over a document: a .fla, .xfl or .phx name
/// becomes the same name ending in .swf.
std::string swfPath(std::string path)
{
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const char* extension : {".fla", ".xfl", ".phx"})
    {
        if (lower.size() > 4 && lower.compare(lower.size() - 4, 4, extension) == 0)
            return path.substr(0, path.size() - 4) + ".swf";
    }
    return path;
}

#ifdef _WIN32
std::wstring widen(const std::string& text)
{
    if (text.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}
#endif

/// Runs \a command through the system's shell (cmd.exe on Windows) and
/// waits for it, with no console window. Its exit code, or -1 when it could
/// not be started.
int runCommand(const std::string& command)
{
#ifdef _WIN32
    wchar_t shell[MAX_PATH] = L"";
    if (!GetEnvironmentVariableW(L"ComSpec", shell, MAX_PATH))
        wcscpy_s(shell, L"cmd.exe");
    std::wstring line = L"\"" + std::wstring(shell) + L"\" /d /s /c \"" + widen(command) + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(shell, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
        return -1;
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(code);
#else
    const int status = std::system(command.c_str());
    if (status == -1)
        return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

} // namespace

void describeDocumentPublishing(ClassDef<Document>& def)
{
    def.method("exportSWF", 2, [](Document& d, const Args& args) {
        d.data();
        std::string path = given(args, 0) && !arg(args, 0).toString().empty() ? uri::toPath(arg(args, 0).toString()) : std::string();
        if (path.empty())
        {
            path = d.session.host().browseForFile("save", "Export SWF Movie");
            if (path.empty())
                return Value(false);
        }
        if (d.document != d.session.host().activeDocument())
            d.session.beforeSwitch();
        std::string error;
        if (!d.session.host().exportSWF(d.document, swfPath(path), error))
            throw Error(error.empty() ? "The SWF movie could not be written." : error);
        return Value(true);
    });
    def.method("publish", 0, [](Document& d, const Args&) {
        d.data();
        if (d.document != d.session.host().activeDocument())
            d.session.beforeSwitch();
        std::vector<std::string> written;
        std::string error;
        if (!d.session.host().publish(d.document, written, error))
            throw Error(error.empty() ? "The document could not be published." : error);
        return Value::list(written);
    });
}

void describeFlashPublishing(ClassDef<Flash>& def)
{
    def.method("publishDocument", 2, [](Flash& fl, const Args& args) {
        Session& session = fl.session;
        const std::string path = uri::toPath(arg(args, 0).toString());
        std::vector<fla::FLADocument*> before = session.host().documents();
        session.beforeSwitch();
        std::string error;
        fla::FLADocument* document = session.host().open(path, error);
        if (!document)
            throw Error(error.empty() ? "There is no document at " + path + "." : error);
        const bool opened = std::find(before.begin(), before.end(), document) == before.end();
        std::vector<std::string> written;
        const bool published = session.host().publish(document, written, error);
        if (opened)
        {
            session.beforeSwitch();
            if (session.host().close(document, false))
                session.forget(document);
        }
        if (!published)
            throw Error(error.empty() ? "The document could not be published." : error);
        return Value(true);
    });
}

void describeFileSystemProcesses(ClassDef<FileSystem>& def)
{
    def.method("runCommandLine", 1, [](FileSystem&, const Args& args) {
        const std::string command = arg(args, 0).toString();
        if (command.empty())
            return Value(false);
        return Value(runCommand(command));
    });
}

} // namespace jsfl
