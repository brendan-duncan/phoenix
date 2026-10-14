#include "file_association.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>

#include <string>

namespace {

const wchar_t* const kClasses = L"Software\\Classes\\";
const wchar_t* const kDocumentProgId = L"Phoenix.Document";
const wchar_t* const kFlaProgId = L"Phoenix.FLA";
const wchar_t* const kViewerProgId = L"Phoenix.Viewer";

/// The files the viewer (Phoenix --view) shows: documents, SWF movies and
/// the artwork Phoenix imports.
const wchar_t* const kViewed[] = {L".phx", L".fla", L".xfl", L".swf", L".svg", L".ai", L".pdf", L".psd", L".psb"};

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

} // namespace

namespace FileAssociation {

void registerTypes()
{
    registerProgId(kDocumentProgId, L"Phoenix Document");
    setValue(L".phx", nullptr, kDocumentProgId);
    setValue(L".phx", L"Content Type", L"application/x-phoenix-document");
    setValue(L".phx\\OpenWithProgids", kDocumentProgId, L"");

    // FLA stays Animate's when it is there; Phoenix is one of the choices.
    registerProgId(kFlaProgId, L"Animate Document");
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
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

} // namespace FileAssociation

#else

namespace FileAssociation {

// macOS declares its types in the bundle's Info.plist; Linux packages
// declare theirs in a .desktop file and a shared-mime-info entry.
void registerTypes() {}
void unregisterTypes() {}

} // namespace FileAssociation

#endif
