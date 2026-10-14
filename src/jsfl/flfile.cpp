#include "flfile.h"

#include "extensions.h"
#include "model.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace jsfl::uri {

namespace {

bool startsWithNoCase(const std::string& text, const char* prefix)
{
    for (size_t i = 0; prefix[i]; ++i)
    {
        if (i >= text.size() || std::tolower(static_cast<unsigned char>(text[i])) != prefix[i])
            return false;
    }
    return true;
}

int hexDigit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

std::string percentDecode(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '%' && i + 2 < text.size() && hexDigit(text[i + 1]) >= 0 && hexDigit(text[i + 2]) >= 0)
        {
            out += static_cast<char>(hexDigit(text[i + 1]) * 16 + hexDigit(text[i + 2]));
            i += 2;
        }
        else
            out += text[i];
    }
    return out;
}

} // namespace

bool isUri(const std::string& text)
{
    return startsWithNoCase(text, "file:");
}

std::string toPath(const std::string& text)
{
    if (!isUri(text))
        return text;
    std::string rest = text.substr(5);
    // file:///C|/x, file://server/share/x, file:/x
    if (rest.compare(0, 3, "///") == 0)
        rest = rest.substr(3);
    else if (rest.compare(0, 2, "//") == 0)
    {
#ifdef _WIN32
        // A network share.
        std::string share = "//" + percentDecode(rest.substr(2));
        std::replace(share.begin(), share.end(), '/', '\\');
        return share;
#else
        rest = rest.substr(2);
#endif
    }
    else if (!rest.empty() && rest[0] == '/')
        rest = rest.substr(1);
    std::string path = percentDecode(rest);
#ifdef _WIN32
    // "C|/x" and "C:/x" alike.
    if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && (path[1] == '|' || path[1] == ':'))
        path[1] = ':';
    return path;
#else
    path = "/" + path;
#ifdef __APPLE__
    // Scripts written on a Mac often start with the volume's name:
    // file:///Macintosh HD/Users/... is /Users/... on the startup volume.
    // Only a first name that is no folder at the root is a volume's: a
    // file still to be made (/var/folders/.../new.fla) keeps its path.
    std::error_code error;
    const size_t slash = path.find('/', 1);
    if (slash != std::string::npos && !fs::exists(fs::u8path(path.substr(0, slash)), error))
    {
        const std::string volume = path.substr(1, slash - 1);
        const std::string onStartup = path.substr(slash);
        if (fs::exists(fs::u8path("/Volumes/" + volume), error) && !fs::exists(fs::u8path(onStartup), error))
            return "/Volumes/" + path.substr(1);
        return onStartup;
    }
#endif
    return path;
#endif
}

std::string fromPath(const std::string& path)
{
    std::string clean = path;
    std::replace(clean.begin(), clean.end(), '\\', '/');
    std::string out = "file:///";
    size_t start = 0;
    if (clean.compare(0, 2, "//") == 0)
    {
        out = "file://";
        start = 2;
    }
    else if (!clean.empty() && clean[0] == '/')
        start = 1;
    for (size_t i = start; i < clean.size(); ++i)
    {
        const char c = clean[i];
        if (i == start + 1 && c == ':' && std::isalpha(static_cast<unsigned char>(clean[start])))
            out += '|';
        else if (c == ' ' || c == '%' || c == '#' || c == '?')
        {
            char buffer[4];
            std::snprintf(buffer, sizeof(buffer), "%%%02X", static_cast<unsigned char>(c));
            out += buffer;
        }
        else
            out += c;
    }
    return out;
}

} // namespace jsfl::uri

namespace jsfl {

namespace {

fs::path pathOf(const Value& value)
{
    if (value.isNullish())
        throw Error("A file URI is needed.");
    return fs::u8path(uri::toPath(value.toString()));
}

/// Whether \a name matches \a mask, where * stands for any run of
/// characters and ? for one; case does not matter.
bool matches(const std::string& name, const std::string& mask)
{
    size_t n = 0;
    size_t m = 0;
    size_t star = std::string::npos;
    size_t mark = 0;
    while (n < name.size())
    {
        if (m < mask.size() && (mask[m] == '?' ||
            std::tolower(static_cast<unsigned char>(mask[m])) == std::tolower(static_cast<unsigned char>(name[n]))))
        {
            ++n;
            ++m;
        }
        else if (m < mask.size() && mask[m] == '*')
        {
            star = m++;
            mark = n;
        }
        else if (star != std::string::npos)
        {
            m = star + 1;
            n = ++mark;
        }
        else
            return false;
    }
    while (m < mask.size() && mask[m] == '*')
        ++m;
    return m == mask.size();
}

#ifdef _WIN32
bool fileTimes(const fs::path& path, double& created, double& modified)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.wstring().c_str(), GetFileExInfoStandard, &data))
        return false;
    const auto seconds = [](const FILETIME& time) {
        const unsigned long long ticks = (static_cast<unsigned long long>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
        return (static_cast<double>(ticks) - 116444736000000000.0) / 10000000.0;
    };
    created = seconds(data.ftCreationTime);
    modified = seconds(data.ftLastWriteTime);
    return true;
}
#else
bool fileTimes(const fs::path& path, double& created, double& modified)
{
    std::error_code error;
    const auto time = fs::last_write_time(path, error);
    if (error)
        return false;
    const auto system = std::chrono::time_point_cast<std::chrono::seconds>(
        time - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    modified = static_cast<double>(system.time_since_epoch().count());
    created = modified;
    return true;
}
#endif

Value timeValue(const Value& file, bool creation, bool asDate)
{
    double created = 0.0;
    double modified = 0.0;
    if (!fileTimes(pathOf(file), created, modified))
        return asDate ? Value::null() : Value("00000000");
    const double seconds = creation ? created : modified;
    if (asDate)
        return Value::date(seconds * 1000.0);
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%08llx", static_cast<unsigned long long>(seconds));
    return Value(std::string(buffer));
}

} // namespace

const ClassInfo& FileSystem::classInfo() const
{
    static const ClassInfo info = []() {
        ClassInfo info;
        ClassDef<FileSystem> def(info, "FLfile");
        def.method("exists", 1, [](FileSystem&, const Args& args) {
            std::error_code error;
            return Value(fs::exists(pathOf(arg(args, 0)), error));
        });
        def.method("read", 1, [](FileSystem&, const Args& args) {
            std::ifstream in(pathOf(arg(args, 0)), std::ios::binary);
            if (!in)
                return Value::null();
            std::ostringstream text;
            text << in.rdbuf();
            std::string content = text.str();
            // A UTF-8 byte order mark is not part of the text.
            if (content.compare(0, 3, "\xEF\xBB\xBF") == 0)
                content.erase(0, 3);
            return Value(content);
        });
        def.method("write", 3, [](FileSystem&, const Args& args) {
            const bool append = arg(args, 2).toString() == "append";
            std::ofstream out(pathOf(arg(args, 0)), std::ios::binary | (append ? std::ios::app : std::ios::trunc));
            if (!out)
                return Value(false);
            const std::string text = arg(args, 1).toString();
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            return Value(static_cast<bool>(out));
        });
        def.method("copy", 2, [](FileSystem&, const Args& args) {
            std::error_code error;
            const fs::path to = pathOf(arg(args, 1));
            // As JSFL does: an existing file is not replaced.
            if (fs::exists(to, error))
                return Value(false);
            return Value(fs::copy_file(pathOf(arg(args, 0)), to, error) && !error);
        });
        def.method("remove", 1, [](FileSystem&, const Args& args) {
            std::error_code error;
            const fs::path path = pathOf(arg(args, 0));
            if (!fs::exists(path, error))
                return Value(false);
            fs::remove_all(path, error);
            return Value(!error);
        });
        def.method("createFolder", 1, [](FileSystem&, const Args& args) {
            std::error_code error;
            const fs::path path = pathOf(arg(args, 0));
            if (fs::exists(path, error))
                return Value(false);
            return Value(fs::create_directories(path, error) && !error);
        });
        def.method("listFolder", 2, [](FileSystem&, const Args& args) {
            // The last part may be a mask: file:///C|/work/*.fla.
            fs::path folder = pathOf(arg(args, 0));
            std::string mask;
            const std::string last = folder.filename().u8string();
            if (last.find_first_of("*?") != std::string::npos)
            {
                mask = last;
                folder = folder.parent_path();
            }
            const std::string only = arg(args, 1).isNullish() ? std::string() : arg(args, 1).toString();
            Value names = Value::array();
            std::error_code error;
            if (!fs::is_directory(folder, error))
                return names;
            std::vector<std::string> found;
            for (fs::directory_iterator it(folder, error), end; !error && it != end; it.increment(error))
            {
                const bool directory = it->is_directory(error);
                if ((only == "files" && directory) || (only == "directories" && !directory))
                    continue;
                const std::string name = it->path().filename().u8string();
                if (mask.empty() || matches(name, mask))
                    found.push_back(name);
            }
            std::sort(found.begin(), found.end());
            for (const std::string& name : found)
                names.items.emplace_back(name);
            return names;
        });
        def.method("getAttributes", 1, [](FileSystem&, const Args& args) {
            const fs::path path = pathOf(arg(args, 0));
            std::error_code error;
            if (!fs::exists(path, error))
                return Value("");
            std::string attributes;
#ifdef _WIN32
            const DWORD flags = GetFileAttributesW(path.wstring().c_str());
            if (flags != INVALID_FILE_ATTRIBUTES)
            {
                if (flags & FILE_ATTRIBUTE_READONLY)
                    attributes += 'R';
                if (flags & FILE_ATTRIBUTE_HIDDEN)
                    attributes += 'H';
                if (flags & FILE_ATTRIBUTE_SYSTEM)
                    attributes += 'S';
                if (flags & FILE_ATTRIBUTE_ARCHIVE)
                    attributes += 'A';
                if (flags & FILE_ATTRIBUTE_DIRECTORY)
                    attributes += 'D';
            }
#else
            const fs::perms perms = fs::status(path, error).permissions();
            if ((perms & fs::perms::owner_write) == fs::perms::none)
                attributes += 'R';
            if (path.filename().u8string().compare(0, 1, ".") == 0)
                attributes += 'H';
            if (fs::is_directory(path, error))
                attributes += 'D';
#endif
            return Value(attributes.empty() ? std::string("N") : attributes);
        });
        def.method("setAttributes", 2, [](FileSystem&, const Args& args) {
            const fs::path path = pathOf(arg(args, 0));
            const std::string wanted = arg(args, 1).toString();
            std::error_code error;
            if (!fs::exists(path, error))
                return Value(false);
            const bool readOnly = wanted.find('R') != std::string::npos;
            if (wanted.find('W') != std::string::npos || readOnly || wanted.find('N') != std::string::npos)
            {
                fs::permissions(path, fs::perms::owner_write,
                    readOnly ? fs::perm_options::remove : fs::perm_options::add, error);
            }
            return Value(!error);
        });
        def.method("getSize", 1, [](FileSystem&, const Args& args) {
            std::error_code error;
            const fs::path path = pathOf(arg(args, 0));
            if (!fs::is_regular_file(path, error))
                return Value(0);
            return Value(static_cast<double>(fs::file_size(path, error)));
        });
        def.method("getCreationDate", 1, [](FileSystem&, const Args& args) { return timeValue(arg(args, 0), true, false); });
        def.method("getModificationDate", 1, [](FileSystem&, const Args& args) { return timeValue(arg(args, 0), false, false); });
        def.method("getCreationDateObj", 1, [](FileSystem&, const Args& args) { return timeValue(arg(args, 0), true, true); });
        def.method("getModificationDateObj", 1, [](FileSystem&, const Args& args) { return timeValue(arg(args, 0), false, true); });
        def.method("platformPathToURI", 1, [](FileSystem&, const Args& args) {
            return Value(uri::fromPath(arg(args, 0).toString()));
        });
        def.method("uriToPlatformPath", 1, [](FileSystem&, const Args& args) {
            std::string path = uri::toPath(arg(args, 0).toString());
#ifdef _WIN32
            std::replace(path.begin(), path.end(), '/', '\\');
#endif
            return Value(path);
        });
        describeFileSystemProcesses(def);
        return info;
    }();
    return info;
}

std::shared_ptr<FileSystem> makeFileSystem()
{
    return std::make_shared<FileSystem>();
}

} // namespace jsfl
