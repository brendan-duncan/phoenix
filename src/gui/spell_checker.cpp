#include "spell_checker.h"

#include <QLocale>

#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <spellcheck.h>
#endif

struct SpellChecker::Native
{
#ifdef _WIN32
    ISpellChecker* checker = nullptr;
    bool initialized = false;
#endif
};

SpellChecker::SpellChecker()
    : _native(new Native)
{
#ifdef _WIN32
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    _native->initialized = SUCCEEDED(init);
    ISpellCheckerFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(SpellCheckerFactory), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) || !factory)
        return;
    // The language Phoenix runs in, else English.
    for (const QString& tag : {QLocale().bcp47Name(), QString("en-US")})
    {
        BOOL supported = FALSE;
        const std::wstring wide = tag.toStdWString();
        if (SUCCEEDED(factory->IsSupported(wide.c_str(), &supported)) && supported &&
            SUCCEEDED(factory->CreateSpellChecker(wide.c_str(), &_native->checker)) && _native->checker)
        {
            _language = tag;
            break;
        }
    }
    factory->Release();
#endif
}

SpellChecker::~SpellChecker()
{
#ifdef _WIN32
    if (_native->checker)
        _native->checker->Release();
    if (_native->initialized)
        CoUninitialize();
#endif
}

bool SpellChecker::available() const
{
#ifdef _WIN32
    return _native->checker != nullptr;
#else
    return false;
#endif
}

std::vector<SpellChecker::Misspelling> SpellChecker::check(const QString& text) const
{
    std::vector<Misspelling> found;
#ifdef _WIN32
    if (!_native->checker || text.isEmpty())
        return found;
    const std::wstring wide = text.toStdWString();
    IEnumSpellingError* errors = nullptr;
    if (FAILED(_native->checker->Check(wide.c_str(), &errors)) || !errors)
        return found;
    ISpellingError* error = nullptr;
    while (errors->Next(&error) == S_OK && error)
    {
        ULONG start = 0;
        ULONG length = 0;
        error->get_StartIndex(&start);
        error->get_Length(&length);
        found.push_back({static_cast<int>(start), static_cast<int>(length)});
        error->Release();
        error = nullptr;
    }
    errors->Release();
#else
    (void)text;
#endif
    return found;
}

QStringList SpellChecker::suggestions(const QString& word) const
{
    QStringList list;
#ifdef _WIN32
    if (!_native->checker)
        return list;
    const std::wstring wide = word.toStdWString();
    IEnumString* suggested = nullptr;
    if (FAILED(_native->checker->Suggest(wide.c_str(), &suggested)) || !suggested)
        return list;
    LPOLESTR one = nullptr;
    while (suggested->Next(1, &one, nullptr) == S_OK && one)
    {
        list << QString::fromWCharArray(one);
        CoTaskMemFree(one);
        one = nullptr;
    }
    suggested->Release();
#else
    (void)word;
#endif
    return list;
}

void SpellChecker::ignore(const QString& word)
{
#ifdef _WIN32
    if (_native->checker)
        _native->checker->Ignore(word.toStdWString().c_str());
#else
    (void)word;
#endif
}

void SpellChecker::add(const QString& word)
{
#ifdef _WIN32
    if (_native->checker)
        _native->checker->Add(word.toStdWString().c_str());
#else
    (void)word;
#endif
}
