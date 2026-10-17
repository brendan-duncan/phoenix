#pragma once

#include "../util/swf_writer.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fla {

class Document;
class RawElement;
class Symbol;

/// An ActionScript 3 movie's scripts compiled to ABC for an SWF, so that it
/// runs in a player as it runs in Test Movie: the classes it reaches (its
/// document class, the classes its symbols are linked to and what they
/// import, Phoenix's fl.* components among them), and each timeline with
/// frame scripts as a MovieClip subclass whose stage instances are its
/// public variables. Free of Qt.
namespace swfmovie {

/// A component placed on a timeline, which the file keeps as its XML
/// (DOMComponentInstance).
struct Component
{
    /// The library's component symbol.
    std::string symbol;
    /// Its instance name, or one made for it when it has none (__id0_, as
    /// published movies name them), so its parameters can be set.
    std::string name;
    swf::Matrix matrix;
    /// Its parametersAsXML: the values set from the inspector.
    std::string parameters;
};

/// Every component instance on the document's timelines, by its element.
std::map<const RawElement*, Component> components(const Document& document);

struct ActionScript3Options
{
    /// A class's source by its qualified name, as Test Movie finds it: the
    /// movie's class path, then Phoenix's own classes.
    std::function<std::optional<std::string>(const std::string& qualifiedName)> findClass;
    /// What include "file.as" reads, or none.
    std::function<std::optional<std::string>(const std::string& path)> readFile;
    /// The document's name (its file's, without the extension), which the
    /// timelines' classes are named by: bunnyPaint_fla.MainTimeline.
    std::string documentName;
};

/// What compiling a document's ActionScript 3 made.
struct ActionScript3
{
    /// The abcFile for DoABC; empty when there is nothing to compile or it
    /// did not compile.
    std::vector<uint8_t> abc;
    /// The main timeline's class, empty for none.
    std::string mainClass;
    /// Each symbol's class, by symbol.
    std::map<const Symbol*, std::string> classes;
    /// Every class compiled, in the order defined.
    std::vector<std::string> defined;
    /// Errors and warnings, a line each, with where they are: a class
    /// file and line, or a timeline's scene or symbol, layer, frame and
    /// line.
    std::vector<std::string> notes;
};

/// Compiles \a document's ActionScript 3.
ActionScript3 compileActionScript3(const Document& document, const ActionScript3Options& options);

/// What translating a document's ActionScript 3 to JavaScript made, for an
/// HTML5 page: the same classes and timelines compiling it reaches.
struct JavaScript3
{
    /// Every class's JavaScript in one script (libas3's bundle), empty when
    /// there is nothing to translate or it could not be translated.
    std::string bundle;
    /// The bundle's source map (v3), back to the .as lines.
    std::string sourceMap;
    /// The main timeline's class, empty for none.
    std::string mainClass;
    /// Each symbol's class, by symbol.
    std::map<const Symbol*, std::string> classes;
    /// Every class translated, in the order defined.
    std::vector<std::string> defined;
    /// Errors and warnings, a line each, with where they are.
    std::vector<std::string> notes;
};

/// Translates \a document's ActionScript 3 to JavaScript.
JavaScript3 translateActionScript3(const Document& document, const ActionScript3Options& options);

/// Compiles \a document's ActionScript 3 and writes it to \a writer as
/// DoABC and SymbolClass, the classes of the characters \a sprites gives
/// (see Options::actionScript3). What did not go in as it is, a line each.
std::vector<std::string> writeActionScript3(const Document& document, swf::Writer& writer,
    const std::map<const Symbol*, int>& sprites, const ActionScript3Options& options);

} // namespace swfmovie

} // namespace fla
