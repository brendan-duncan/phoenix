#pragma once

#include <QString>
#include <QStringList>

#include <memory>

/// The system's spell checker, in the language Phoenix runs in (English
/// where that one has none). On Windows it is the system's own, with its
/// user dictionary; elsewhere it is not available yet.
class SpellChecker
{
public:
    SpellChecker();
    ~SpellChecker();

    SpellChecker(const SpellChecker&) = delete;
    SpellChecker& operator=(const SpellChecker&) = delete;

    bool available() const;
    /// The language checked in, as a tag such as en-US.
    QString language() const { return _language; }

    /// The misspelled words of \a text: where each starts and how long it is.
    struct Misspelling
    {
        int start = 0;
        int length = 0;
    };
    std::vector<Misspelling> check(const QString& text) const;

    QStringList suggestions(const QString& word) const;

    /// Ignore All, for this session; Add to Dictionary, for good.
    void ignore(const QString& word);
    void add(const QString& word);

private:
    struct Native;
    std::unique_ptr<Native> _native;
    QString _language;
};
