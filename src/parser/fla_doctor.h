#pragma once

#include <string>
#include <vector>

namespace fla {

class Document;

/// The FLA doctor: checks a document file for damage, reports what it
/// finds, repairs what can be repaired, and salvages what can be read from
/// an archive too damaged to open (a truncated save, a RECOVER_ file).
namespace doctor {

struct Issue
{
    enum class Severity
    {
        Info,
        Warning,
        Error
    };
    Severity severity = Severity::Warning;
    /// Where: "Scene 1 > Layer 1", "LIBRARY/Ball.xml", the symbol's name...
    std::string where;
    std::string what;
    /// Whether repair fixes it; after a repair, whether it was fixed.
    bool repairable = false;
    bool repaired = false;
};

struct Report
{
    std::vector<Issue> issues;
    /// The file opened as it is.
    bool readable = false;
    /// It did not, and was read from what could be salvaged.
    bool salvaged = false;

    int count(Issue::Severity severity) const;
    /// One line per issue, for the command line.
    std::string text() const;
    std::string json() const;
};

/// Checks the archive itself: that every part is there and is well-formed
/// XML. Adds what it finds to \a report.
void checkContainer(const std::string& path, Report& report);

/// Checks a document read from a file: frames overlapping or out of
/// order, spans of no length, instances of symbols or bitmaps the library
/// does not have, symbols that hold themselves, broken layer parents,
/// sounds that are not there, names used twice. With \a repair, fixes
/// what it can and marks those issues repaired.
void checkDocument(Document& document, Report& report, bool repair);

/// Reads every part it can from a damaged archive, entry by entry from
/// the local headers rather than the (perhaps missing) directory at its
/// end, into \a folder as an XFL folder. \a recovered lists the parts;
/// \a notes says which symbols were damaged or missing and stood in for by
/// empty ones, so the rest opens.
bool salvage(const std::string& path, const std::string& folder, std::vector<std::string>& recovered,
    std::vector<std::string>& notes, std::string& error);

} // namespace doctor

} // namespace fla
