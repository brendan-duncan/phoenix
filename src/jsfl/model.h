#pragma once

#include "object.h"
#include "session.h"

#include "../data/transform.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace fla {
class Bitmap;
class DOMElement;
class Element;
class FLADocument;
class Folder;
class Frame;
class Layer;
class Symbol;
class Timeline;
} // namespace fla

/// The JSFL classes. Each holds plain pointers into a document and checks
/// them before use (Session::alive), so an object whose layer, frame or
/// element is gone, or whose document was closed, fails with a clear error.
/// Their properties and methods are declared in their classInfo() tables
/// (model_*.cpp), which is all a script engine binds.
namespace jsfl {

/// `fl`: the open documents, output, dialogs and running other scripts.
class Flash final : public Object
{
public:
    explicit Flash(Session& session) : session(session) {}
    const ClassInfo& classInfo() const override;
    const void* identity() const override { return this; }
    Session& session;
    /// Runs another script file in the same script context; set by the
    /// engine, which owns that context.
    std::function<Value(const std::string& path, const std::string& function, const Args& args)> runScript;
};

/// `fl.outputPanel`.
class OutputPanel final : public Object
{
public:
    explicit OutputPanel(Session& session) : session(session) {}
    const ClassInfo& classInfo() const override;
    const void* identity() const override { return &session; }
    Session& session;
};

/// `FLfile`: files and folders by file:/// URI.
class FileSystem final : public Object
{
public:
    const ClassInfo& classInfo() const override;
};

/// What every object reached from a document holds.
class DocumentPart : public Object
{
public:
    DocumentPart(Session& session, fla::FLADocument* document) : session(session), document(document) {}
    const void* scope() const override { return document; }
    Session& session;
    fla::FLADocument* document;
    fla::Document& data() const { return session.data(document); }
    /// An error unless \a object is still in the document.
    void require(const fla::DOMElement* object, const char* what) const;
    void push(fla::CommandPtr command, bool structural = true) const { session.push(document, std::move(command), structural); }
};

class Document final : public DocumentPart
{
public:
    using DocumentPart::DocumentPart;
    const ClassInfo& classInfo() const override;
    const void* identity() const override { return document; }
};

class Library final : public DocumentPart
{
public:
    using DocumentPart::DocumentPart;
    const ClassInfo& classInfo() const override;
    const void* identity() const override { return reinterpret_cast<const char*>(document) + 1; }
};

/// A library item: a symbol, bitmap, sound, video or folder. Sounds and
/// videos are kept by name, the others by pointer.
class Item final : public DocumentPart
{
public:
    enum class Kind
    {
        Symbol,
        Bitmap,
        Sound,
        Video,
        Folder
    };
    Item(Session& session, fla::FLADocument* document, Kind kind, void* pointer, std::string name)
        : DocumentPart(session, document), kind(kind), pointer(pointer), itemName(std::move(name))
    {}
    const ClassInfo& classInfo() const override;
    const void* identity() const override { return pointer; }
    Kind kind;
    void* pointer;
    /// The name it was reached by (sounds and videos are found by it).
    std::string itemName;

    fla::Symbol* symbol() const;
    fla::Bitmap* bitmap() const;
    fla::Folder* folder() const;
    /// Its name now, with its folder path; an error when it has gone.
    std::string name() const;
};

class Timeline final : public DocumentPart
{
public:
    Timeline(Session& session, fla::FLADocument* document, fla::Timeline* timeline)
        : DocumentPart(session, document), timeline(timeline)
    {}
    const ClassInfo& classInfo() const override;
    const void* identity() const override { return timeline; }
    fla::Timeline* timeline;
    fla::Timeline& get() const;
};

class Layer final : public DocumentPart
{
public:
    Layer(Session& session, fla::FLADocument* document, fla::Layer* layer)
        : DocumentPart(session, document), layer(layer)
    {}
    const ClassInfo& classInfo() const override;
    const void* identity() const override { return layer; }
    fla::Layer* layer;
    fla::Layer& get() const;
};

/// A frame of a layer, by its number: it shows whichever keyframe's span
/// covers that frame now, so it follows the layer through frame edits (a
/// keyframe cleared under it, frames inserted before it).
class Frame final : public DocumentPart
{
public:
    Frame(Session& session, fla::FLADocument* document, fla::Layer* layer, int index)
        : DocumentPart(session, document), layer(layer), index(index)
    {}
    const ClassInfo& classInfo() const override;
    fla::Layer* layer;
    int index;
    /// The keyframe covering the frame; an error past the layer's end.
    fla::Frame& get() const;
};

/// An element on a frame: a shape, a group, a symbol or bitmap instance, a
/// text field. Its class (SymbolInstance, Text, Shape...) follows its kind.
class Element final : public DocumentPart
{
public:
    ///  element must be alive: its kind is taken now, so the object
    /// still knows its class once the element is gone.
    Element(Session& session, fla::FLADocument* document, fla::Element* element);
    Element(Session& session, fla::FLADocument* document, fla::Element* element, const ClassInfo& info)
        : DocumentPart(session, document), element(element), _info(&info)
    {}
    const ClassInfo& classInfo() const override { return *_info; }
    const void* identity() const override { return element; }
    fla::Element* element;
    fla::Element& get() const;

private:
    const ClassInfo* _info;
};

/// The objects scripts start from.
std::shared_ptr<Flash> makeFlash(Session& session);
std::shared_ptr<FileSystem> makeFileSystem();

// Shared between the model's files.
std::shared_ptr<Document> wrapDocument(Session& session, fla::FLADocument* document);
std::shared_ptr<Timeline> wrapTimeline(Session& session, fla::FLADocument* document, fla::Timeline* timeline);
std::shared_ptr<Layer> wrapLayer(Session& session, fla::FLADocument* document, fla::Layer* layer);
std::shared_ptr<Element> wrapElement(Session& session, fla::FLADocument* document, fla::Element* element);
/// The library item called \a name (with its folder path), or null.
std::shared_ptr<Item> findItem(Session& session, fla::FLADocument* document, const std::string& name);
std::shared_ptr<Item> wrapSymbol(Session& session, fla::FLADocument* document, fla::Symbol* symbol);

/// "#RRGGBB" (with AA when not opaque), and back from that, "#RGB" or a
/// number (0xRRGGBB).
std::string colorText(const uint8_t rgba[4]);
bool parseColor(const Value& value, uint8_t rgba[4]);

/// The keyframe at the playhead on the current layer of the timeline being
/// edited; an error when there is none.
fla::Frame* currentKeyframe(Session& session, fla::FLADocument* document);
/// The selected elements still in the document, in selection order.
std::vector<fla::Element*> selectedElements(Session& session, fla::FLADocument* document);
/// Places an instance of \a item (a symbol or bitmap) with its registration
/// point at \a x, \a y on the current keyframe, and selects it.
void placeItem(Session& session, fla::FLADocument* document, const Item& item, double x, double y);
/// A new transform for \a element, as one step of the script's undo.
void setTransform(Session& session, fla::FLADocument* document, fla::Element* element, const fla::Transform& after,
    const char* name);
/// \a a after \a b: what a point goes through when \a b moves it, then \a a.
fla::Transform compose(const fla::Transform& a, const fla::Transform& b);

} // namespace jsfl
