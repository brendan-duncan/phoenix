#include "file_association.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>
#if defined(Q_OS_LINUX)
#include <QProcess>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>

#include <string>

namespace {

const wchar_t* const kClasses = L"Software\\Classes\\";
const wchar_t* const kDocumentProgId = L"Phoenix.Document";
const wchar_t* const kFlaProgId = L"Phoenix.FLA";
const wchar_t* const kViewerProgId = L"Phoenix.Viewer";
/// phoenix:// links.
const wchar_t* const kLinkScheme = L"phoenix";

/// The files the viewer (Phoenix --view) shows: documents, SWF movies and
/// the artwork Phoenix imports.
const wchar_t* const kViewed[] = {L".phx", L".fla", L".xfl", L".swf", L".svg", L".ai", L".eps", L".ps", L".pdf", L".psd", L".psb"};

std::wstring exePath()
{
    wchar_t buffer[MAX_PATH * 4];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    return std::wstring(buffer, length);
}

void setValue(const std::wstring& key, const wchar_t* name, const std::wstring& value)
{
    HKEY handle = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, (kClasses + key).c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &handle,
            nullptr) != ERROR_SUCCESS)
        return;
    RegSetValueExW(handle, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(handle);
}

/// The text of a value; empty when there is none.
std::wstring getValue(const std::wstring& key, const wchar_t* name)
{
    wchar_t buffer[MAX_PATH * 4];
    DWORD size = sizeof(buffer);
    if (RegGetValueW(HKEY_CURRENT_USER, (kClasses + key).c_str(), name, RRF_RT_REG_SZ, nullptr, buffer, &size)
        != ERROR_SUCCESS)
        return {};
    return std::wstring(buffer);
}

/// A program id: its name, its icon and how it opens a file.
void registerProgId(const wchar_t* progId, const wchar_t* description)
{
    const std::wstring exe = exePath();
    setValue(progId, nullptr, description);
    setValue(std::wstring(progId) + L"\\DefaultIcon", nullptr, L"\"" + exe + L"\",0");
    setValue(std::wstring(progId) + L"\\shell\\open\\command", nullptr, L"\"" + exe + L"\" \"%1\"");
}

void deleteValue(const std::wstring& key, const wchar_t* name)
{
    HKEY handle = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, (kClasses + key).c_str(), 0, KEY_SET_VALUE, &handle) == ERROR_SUCCESS)
    {
        RegDeleteValueW(handle, name);
        RegCloseKey(handle);
    }
}

/// phoenix:// links open in this Phoenix.
void registerScheme()
{
    const std::wstring exe = exePath();
    const std::wstring scheme = kLinkScheme;
    setValue(scheme, nullptr, L"URL:Phoenix");
    setValue(scheme, L"URL Protocol", L"");
    setValue(scheme + L"\\DefaultIcon", nullptr, L"\"" + exe + L"\",0");
    setValue(scheme + L"\\shell\\open\\command", nullptr, L"\"" + exe + L"\" \"%1\"");
}

/// The program phoenix:// links open with now; empty when none.
QString registeredProgram()
{
    const std::wstring command = getValue(std::wstring(kLinkScheme) + L"\\shell\\open\\command", nullptr);
    const QString text = QString::fromStdWString(command);
    if (!text.startsWith('"'))
        return text.section(' ', 0, 0);
    return text.mid(1, text.indexOf('"', 1) - 1);
}

} // namespace

namespace FileAssociation {

void registerTypes()
{
    registerProgId(kDocumentProgId, L"Phoenix Document");
    setValue(L".phx", nullptr, kDocumentProgId);
    setValue(L".phx", L"Content Type", L"application/x-phoenix-document");
    setValue(L".phx\\OpenWithProgids", kDocumentProgId, L"");

    // FLA stays with the app that owns it, if any; Phoenix is one of the choices.
    registerProgId(kFlaProgId, L"FLA Document");
    setValue(L".fla\\OpenWithProgids", kFlaProgId, L"");

    // The viewer: under "Open with" for every type it shows, and "View in
    // Phoenix" on their right-click menu, whatever opens them by default.
    const std::wstring exe = exePath();
    const std::wstring view = L"\"" + exe + L"\" --view \"%1\"";
    setValue(kViewerProgId, nullptr, L"Phoenix Viewer");
    setValue(std::wstring(kViewerProgId) + L"\\DefaultIcon", nullptr, L"\"" + exe + L"\",0");
    setValue(std::wstring(kViewerProgId) + L"\\Application", L"ApplicationName", L"Phoenix Viewer");
    setValue(std::wstring(kViewerProgId) + L"\\shell\\open", L"FriendlyAppName", L"Phoenix Viewer");
    setValue(std::wstring(kViewerProgId) + L"\\shell\\open\\command", nullptr, view);
    for (const wchar_t* extension : kViewed)
    {
        setValue(std::wstring(extension) + L"\\OpenWithProgids", kViewerProgId, L"");
        const std::wstring verb = std::wstring(L"SystemFileAssociations\\") + extension + L"\\shell\\PhoenixView";
        setValue(verb, nullptr, L"View in Phoenix");
        setValue(verb, L"Icon", L"\"" + exe + L"\",0");
        setValue(verb + L"\\command", nullptr, view);
    }

    registerScheme();

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

void unregisterTypes()
{
    RegDeleteTreeW(HKEY_CURRENT_USER, (std::wstring(kClasses) + kDocumentProgId).c_str());
    RegDeleteTreeW(HKEY_CURRENT_USER, (std::wstring(kClasses) + kFlaProgId).c_str());
    RegDeleteTreeW(HKEY_CURRENT_USER, (std::wstring(kClasses) + kViewerProgId).c_str());
    for (const wchar_t* extension : kViewed)
    {
        deleteValue(std::wstring(extension) + L"\\OpenWithProgids", kViewerProgId);
        RegDeleteTreeW(HKEY_CURRENT_USER,
            (std::wstring(kClasses) + L"SystemFileAssociations\\" + extension + L"\\shell\\PhoenixView").c_str());
    }
    // .phx was Phoenix's alone; from .fla only Phoenix's entry goes.
    RegDeleteTreeW(HKEY_CURRENT_USER, (std::wstring(kClasses) + L".phx").c_str());
    HKEY handle = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, (std::wstring(kClasses) + L".fla\\OpenWithProgids").c_str(), 0, KEY_SET_VALUE,
            &handle) == ERROR_SUCCESS)
    {
        RegDeleteValueW(handle, kFlaProgId);
        RegCloseKey(handle);
    }
    // Links go only if they were this Phoenix's.
    if (QFileInfo(registeredProgram()) == QFileInfo(QString::fromStdWString(exePath())))
        RegDeleteTreeW(HKEY_CURRENT_USER, (std::wstring(kClasses) + kLinkScheme).c_str());
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

void registerLinks(bool always)
{
    const QString program = registeredProgram();
    if (!always && !program.isEmpty() && QFileInfo::exists(program))
        return;
    registerScheme();
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

} // namespace FileAssociation

#else

namespace FileAssociation {

// macOS declares its types in the bundle's Info.plist; Linux packages
// declare theirs in a .desktop file and a shared-mime-info entry.
void registerTypes() {}
void unregisterTypes() {}

void registerLinks(bool always)
{
#if defined(Q_OS_LINUX)
    // An AppImage has no package to declare its links: a .desktop file in
    // the person's applications does, pointing at the AppImage itself.
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    if (appImage.isEmpty())
        return;
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    const QString file = QDir(folder).filePath("phoenix-links.desktop");
    QString exec;
    {
        QFile existing(file);
        if (existing.open(QIODevice::ReadOnly | QIODevice::Text))
            for (const QString& line : QString::fromUtf8(existing.readAll()).split('\n'))
                if (line.startsWith("Exec="))
                    exec = line.mid(5).section(' ', 0, 0).remove('"');
    }
    if (!always && !exec.isEmpty() && QFileInfo::exists(exec))
        return;
    QDir().mkpath(folder);
    QFile out(file);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return;
    QTextStream(&out) << "[Desktop Entry]\n"
                      << "Type=Application\n"
                      << "Name=Phoenix\n"
                      << "Exec=\"" << appImage << "\" %u\n"
                      << "NoDisplay=true\n"
                      << "MimeType=x-scheme-handler/phoenix;\n";
    out.close();
    QProcess::startDetached("xdg-mime", {"default", "phoenix-links.desktop", "x-scheme-handler/phoenix"});
#else
    Q_UNUSED(always);
#endif
}

} // namespace FileAssociation

#endif

namespace FileAssociation {

bool isLink(const QString& text)
{
    return text.startsWith("phoenix:", Qt::CaseInsensitive);
}

} // namespace FileAssociation
