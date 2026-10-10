#include "element_commands.h"

#include "../data/document.h"
#include "../data/edge.h"
#include "../data/element.h"
#include "../data/frame.h"
#include "../data/group.h"
#include "../data/layer.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"
#include "element_clone.h"
#include "selection.h"
#include "shape_merge.h"

#include "../data/shape.h"
#include "../data/static_text.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>

namespace fla {

namespace {

/// Shared by every transform command. Whether two of them actually merge is
/// decided by mergeWith, which also checks the element and the gesture.
constexpr int kTransformMergeId = 1;

/// Shared by every edge-geometry command; mergeWith decides the rest.
constexpr int kGeometryMergeId = 2;

} // namespace

SetElementTransformCommand::SetElementTransformCommand(Element* element,
    const Transform& before, const Transform& after, const std::string& name)
    : _element(element)
    , _before(before)
    , _after(after)
    , _name(name)
{}

void SetElementTransformCommand::redo()
{
    if (_element)
        _element->transform = _after;
}

void SetElementTransformCommand::undo()
{
    if (_element)
        _element->transform = _before;
}

int SetElementTransformCommand::mergeId() const
{
    return kTransformMergeId;
}

bool SetElementTransformCommand::mergeWith(const Command* other)
{
    const SetElementTransformCommand* next =
        static_cast<const SetElementTransformCommand*>(other);

    // Only fold together edits to the same object under the same gesture.
    // Merging a move into a scale would leave the Edit menu describing one while
    // undoing both.
    if (next->_element != _element || next->_name != _name)
        return false;

    // Keep this command's starting point so undo still returns to where the
    // gesture began, and take the later end state.
    _after = next->_after;
    return true;
}

SetEdgeGeometryCommand::SetEdgeGeometryCommand(Edge* edge, const EditablePath& before,
    const EditablePath& after, const std::string& name, size_t pathIndex)
    : _edge(edge)
    , _before(before)
    , _after(after)
    , _name(name)
    , _pathIndex(pathIndex)
{}

void SetEdgeGeometryCommand::redo()
{
    if (_edge)
        _after.applyTo(*_edge, _pathIndex);
}

void SetEdgeGeometryCommand::undo()
{
    if (_edge)
        _before.applyTo(*_edge, _pathIndex);
}

int SetEdgeGeometryCommand::mergeId() const
{
    return kGeometryMergeId;
}

bool SetEdgeGeometryCommand::mergeWith(const Command* other)
{
    const SetEdgeGeometryCommand* next =
        static_cast<const SetEdgeGeometryCommand*>(other);

    if (next->_edge != _edge || next->_name != _name || next->_pathIndex != _pathIndex)
        return false;

    // Keep this command's starting geometry so undo still returns to where the
    // gesture began.
    _after = next->_after;
    return true;
}

MergeShapeCommand::MergeShapeCommand(Shape* target, const Shape& addition,
    const std::string& name, const MergeOptions& options)
    : _target(target)
    , _name(name)
{
    if (!_target)
        return;

    _before = cloneShape(*_target, nullptr);

    // The merge runs once, here. redo() and undo() only ever swap a snapshot
    // back in, so repeating either is cheap and cannot drift.
    ShapeMerger::merge(*_target, addition, options);

    _after = cloneShape(*_target, nullptr);
}

MergeShapeCommand::~MergeShapeCommand()
{
    delete _before;
    delete _after;
}

void MergeShapeCommand::redo()
{
    if (_target && _after)
        setShapeContents(*_target, *_after);
}

void MergeShapeCommand::undo()
{
    if (_target && _before)
        setShapeContents(*_target, *_before);
}

SubtractShapeCommand::SubtractShapeCommand(Shape* target, const Shape& cutter,
    const std::string& name, const MergeOptions& options)
    : _target(target)
    , _name(name)
{
    if (!_target)
        return;

    _before = cloneShape(*_target, nullptr);

    // The cut runs once, here, for the same reason a merge does.
    ShapeMerger::subtract(*_target, cutter, options);

    _after = cloneShape(*_target, nullptr);
}

EraseStrokesCommand::EraseStrokesCommand(Shape* target, const Shape& cutter,
    const std::string& name)
    : _target(target)
    , _name(name)
{
    if (!_target)
        return;

    _before = cloneShape(*_target, nullptr);
    ShapeMerger::eraseStrokes(*_target, cutter);
    _after = cloneShape(*_target, nullptr);
}

EraseStrokesCommand::~EraseStrokesCommand()
{
    delete _before;
    delete _after;
}

void EraseStrokesCommand::redo()
{
    if (_target && _after)
        setShapeContents(*_target, *_after);
}

void EraseStrokesCommand::undo()
{
    if (_target && _before)
        setShapeContents(*_target, *_before);
}

SubtractShapeCommand::~SubtractShapeCommand()
{
    delete _before;
    delete _after;
}

void SubtractShapeCommand::redo()
{
    if (_target && _after)
        setShapeContents(*_target, *_after);
}

void SubtractShapeCommand::undo()
{
    if (_target && _before)
        setShapeContents(*_target, *_before);
}

SetShapeContentsCommand::SetShapeContentsCommand(Shape* target, const Shape& after,
    const std::string& name)
    : _target(target)
    , _before(target ? cloneShape(*target, nullptr) : nullptr)
    , _after(cloneShape(after, nullptr))
    , _name(name)
{}

SetShapeContentsCommand::~SetShapeContentsCommand()
{
    delete _before;
    delete _after;
}

void SetShapeContentsCommand::redo()
{
    if (_target && _after)
        setShapeContents(*_target, *_after);
}

void SetShapeContentsCommand::undo()
{
    if (_target && _before)
        setShapeContents(*_target, *_before);
}

SplitRegionCommand::SplitRegionCommand(Shape* source, const Shape& remainder, Frame* frame,
    Shape* piece, const std::string& name, Selection* selection)
    : _source(source)
    , _before(source ? cloneShape(*source, nullptr) : nullptr)
    , _remainder(cloneShape(remainder, nullptr))
    , _frame(frame)
    , _piece(piece)
    , _name(name)
    , _selection(selection)
{}

SplitRegionCommand::~SplitRegionCommand()
{
    delete _before;
    delete _remainder;
    if (_owned)
        delete _piece;
}

void SplitRegionCommand::redo()
{
    if (!_source || !_frame || !_piece || !_owned)
        return;

    setShapeContents(*_source, *_remainder);

    // Just above the source, so the piece draws over the hole it left.
    std::vector<Element*>& elements = _frame->elements;
    auto at = std::find(elements.begin(), elements.end(), static_cast<Element*>(_source));
    if (at == elements.end())
        elements.push_back(_piece);
    else
        elements.insert(at + 1, _piece);
    _piece->parent = _frame;
    _owned = false;
}

void SplitRegionCommand::undo()
{
    if (!_source || !_frame || !_piece || _owned)
        return;

    std::vector<Element*>& elements = _frame->elements;
    const auto it = std::find(elements.begin(), elements.end(), static_cast<Element*>(_piece));
    if (it != elements.end())
        elements.erase(it);
    _piece->parent = nullptr;
    _owned = true;

    if (_selection)
        _selection->remove(_piece);

    setShapeContents(*_source, *_before);
}

AddElementCommand::AddElementCommand(Frame* frame, Element* element,
    const std::string& name, Selection* selection, int index)
    : _frame(frame)
    , _element(element)
    , _name(name)
    , _selection(selection)
    , _index(index)
{}

AddElementCommand::~AddElementCommand()
{
    // Only delete it if it is not in the frame: once it is in, the frame owns it.
    if (_owned)
        delete _element;
}

void AddElementCommand::redo()
{
    if (!_frame || !_element || !_owned)
        return;

    std::vector<Element*>& elements = _frame->elements;

    // Re-inserting at the recorded index is what preserves z-order across an
    // undo and redo, rather than the object jumping to the front.
    if (_index < 0 || _index > static_cast<int>(elements.size()))
        _index = static_cast<int>(elements.size());

    elements.insert(elements.begin() + _index, _element);
    _owned = false;
}

void AddElementCommand::undo()
{
    if (!_frame || !_element || _owned)
        return;

    std::vector<Element*>& elements = _frame->elements;
    const auto it = std::find(elements.begin(), elements.end(), _element);
    if (it == elements.end())
        return;

    _index = static_cast<int>(it - elements.begin());
    elements.erase(it);
    _owned = true;

    // The element is no longer in the document, so nothing may keep pointing at
    // it.
    if (_selection)
        _selection->remove(_element);
}

RemoveElementCommand::RemoveElementCommand(Frame* frame, Element* element,
    const std::string& name, Selection* selection)
    : _frame(frame)
    , _element(element)
    , _name(name)
    , _selection(selection)
{}

GroupElementsCommand::GroupElementsCommand(Frame* frame, const std::vector<Element*>& elements,
    const std::string& name, Selection* selection)
    : _frame(frame)
    , _group(new Group(frame))
    , _name(name)
    , _selection(selection)
{
    // In z-order, so the group's members draw in the order they did before.
    if (!_frame)
        return;

    for (size_t i = 0; i < _frame->elements.size(); ++i)
    {
        Element* candidate = _frame->elements[i];
        if (std::find(elements.begin(), elements.end(), candidate) != elements.end())
        {
            _elements.push_back(candidate);
            _indices.push_back(static_cast<int>(i));
        }
    }
}

GroupElementsCommand::~GroupElementsCommand()
{
    if (!_applied)
    {
        _group->members.clear();
        delete _group;
    }
}

void GroupElementsCommand::redo()
{
    if (!_frame || _applied || _elements.empty())
        return;

    std::vector<Element*>& frameElements = _frame->elements;
    for (Element* element : _elements)
    {
        const auto it = std::find(frameElements.begin(), frameElements.end(), element);
        if (it != frameElements.end())
            frameElements.erase(it);
        element->parent = _group;
        _group->members.push_back(element);
        if (_selection)
            _selection->remove(element);
    }

    // Where the topmost member was, less the members taken out below it.
    int at = _indices.back() - static_cast<int>(_indices.size() - 1);
    at = std::max(0, std::min(at, static_cast<int>(frameElements.size())));
    frameElements.insert(frameElements.begin() + at, _group);
    _applied = true;

    if (_selection)
        _selection->select(_group);
}

void GroupElementsCommand::undo()
{
    if (!_frame || !_applied)
        return;

    std::vector<Element*>& frameElements = _frame->elements;
    const auto it = std::find(frameElements.begin(), frameElements.end(), static_cast<Element*>(_group));
    if (it != frameElements.end())
        frameElements.erase(it);
    if (_selection)
        _selection->remove(_group);

    _group->members.clear();
    for (size_t i = 0; i < _elements.size(); ++i)
    {
        Element* element = _elements[i];
        element->parent = _frame;
        const int at = std::max(0, std::min(_indices[i], static_cast<int>(frameElements.size())));
        frameElements.insert(frameElements.begin() + at, element);
    }
    _applied = false;

    if (_selection)
    {
        std::vector<DOMElement*> picked(_elements.begin(), _elements.end());
        _selection->select(picked);
    }
}

UngroupCommand::UngroupCommand(Frame* frame, Group* group, const std::string& name,
    Selection* selection)
    : _frame(frame)
    , _group(group)
    , _name(name)
    , _selection(selection)
{}

UngroupCommand::~UngroupCommand()
{
    if (_held)
    {
        _group->members.clear();
        delete _group;
    }
}

void UngroupCommand::redo()
{
    if (!_frame || !_group || _held)
        return;

    std::vector<Element*>& frameElements = _frame->elements;
    const auto it = std::find(frameElements.begin(), frameElements.end(), static_cast<Element*>(_group));
    if (it == frameElements.end())
        return;

    _index = static_cast<int>(it - frameElements.begin());
    frameElements.erase(it);
    if (_selection)
        _selection->remove(_group);

    _members = _group->members;
    _group->members.clear();
    _held = true;

    int at = _index;
    for (Element* member : _members)
    {
        member->parent = _frame;
        frameElements.insert(frameElements.begin() + at, member);
        ++at;
    }

    if (_selection)
    {
        std::vector<DOMElement*> picked(_members.begin(), _members.end());
        _selection->select(picked);
    }
}

void UngroupCommand::undo()
{
    if (!_frame || !_group || !_held)
        return;

    std::vector<Element*>& frameElements = _frame->elements;
    for (Element* member : _members)
    {
        const auto it = std::find(frameElements.begin(), frameElements.end(), member);
        if (it != frameElements.end())
            frameElements.erase(it);
        member->parent = _group;
        if (_selection)
            _selection->remove(member);
    }
    _group->members = _members;

    const int at = std::max(0, std::min(_index, static_cast<int>(frameElements.size())));
    frameElements.insert(frameElements.begin() + at, _group);
    _held = false;

    if (_selection)
        _selection->select(_group);
}

ReorderElementCommand::ReorderElementCommand(Frame* frame, Element* element, int newIndex,
    const std::string& name)
    : _frame(frame)
    , _element(element)
    , _after(newIndex)
    , _name(name)
{}

void ReorderElementCommand::moveTo(int index)
{
    std::vector<Element*>& elements = _frame->elements;
    const auto it = std::find(elements.begin(), elements.end(), _element);
    if (it == elements.end())
        return;
    elements.erase(it);

    index = std::max(0, std::min(index, static_cast<int>(elements.size())));
    elements.insert(elements.begin() + index, _element);
}

void ReorderElementCommand::redo()
{
    if (!_frame || !_element)
        return;

    const auto it = std::find(_frame->elements.begin(), _frame->elements.end(), _element);
    if (it == _frame->elements.end())
        return;
    _before = static_cast<int>(it - _frame->elements.begin());
    moveTo(_after);
}

void ReorderElementCommand::undo()
{
    if (!_frame || !_element || _before < 0)
        return;
    moveTo(_before);
}

SetDrawingObjectCommand::SetDrawingObjectCommand(Shape* shape, bool drawingObject,
    const std::string& name)
    : _shape(shape)
    , _before(shape ? shape->isDrawingObject : false)
    , _after(drawingObject)
    , _name(name)
{}

void SetDrawingObjectCommand::redo()
{
    if (_shape)
        _shape->isDrawingObject = _after;
}

void SetDrawingObjectCommand::undo()
{
    if (_shape)
        _shape->isDrawingObject = _before;
}

SetTextContentsCommand::SetTextContentsCommand(StaticText* target, const StaticText& before,
    const StaticText& after, const std::string& name)
    : _target(target)
    , _before(static_cast<StaticText*>(cloneElement(before, nullptr)))
    , _after(static_cast<StaticText*>(cloneElement(after, nullptr)))
    , _name(name)
{
}

SetTextContentsCommand::~SetTextContentsCommand()
{
    delete _before;
    delete _after;
}

void SetTextContentsCommand::redo()
{
    if (_target && _after)
        setTextContents(*_target, *_after);
}

void SetTextContentsCommand::undo()
{
    if (_target && _before)
        setTextContents(*_target, *_before);
}

int BreakApartInstanceCommand::shownFrameIndex(const SymbolInstance& instance)
{
    if (instance.symbolType == SymbolType::Graphic)
        return std::max(0, instance.firstFrame);
    return 0;
}

BreakApartInstanceCommand::BreakApartInstanceCommand(Frame* frame, SymbolInstance* instance,
    const std::string& name, Selection* selection)
    : _frame(frame)
    , _instance(instance)
    , _name(name)
    , _selection(selection)
{
    if (!_frame || !_instance || !_instance->symbol || _instance->symbol->timelines.empty())
        return;

    const Timeline* timeline = _instance->symbol->timelines.front();
    const int shown = shownFrameIndex(*_instance);

    // Layers are listed top first; the stage draws the bottom one first, so
    // the copies are gathered from the last layer back to keep that order.
    for (auto layerIt = timeline->layers.rbegin(); layerIt != timeline->layers.rend(); ++layerIt)
    {
        const Layer* layer = *layerIt;
        if (!layer || layer->layerType == Layer::Type::Folder || layer->layerType == Layer::Type::Guide)
            continue;

        // The keyframe in effect at the shown frame.
        const Frame* source = nullptr;
        for (const Frame* candidate : layer->frames)
        {
            if (candidate && candidate->index <= layer->firstFrame + shown)
                source = candidate;
        }
        if (!source)
            continue;

        for (const Element* element : source->elements)
        {
            if (!element)
                continue;
            Element* piece = cloneElement(*element, _frame);
            if (!piece)
                continue;

            // Where the symbol put it, then where the instance put the symbol.
            Transform placed = _instance->transform;
            placed.concatenate(element->transform);
            piece->transform = placed;
            piece->isSelected = false;
            piece->isFloating = false;

            // The instance's color effect has nowhere to live once the
            // instance is gone, so it is baked into the copies' colors.
            applyColorTransform(*piece, _instance->colorTransform);
            _pieces.push_back(piece);
        }
    }
}

BreakApartInstanceCommand::~BreakApartInstanceCommand()
{
    if (_applied)
    {
        // The frame holds the pieces; the instance is the command's.
        delete _instance;
    }
    else
    {
        for (Element* piece : _pieces)
            delete piece;
    }
}

void BreakApartInstanceCommand::redo()
{
    if (!_frame || !_instance || _applied || _pieces.empty())
        return;

    std::vector<Element*>& frameElements = _frame->elements;
    const auto it = std::find(frameElements.begin(), frameElements.end(),
        static_cast<Element*>(_instance));
    if (it == frameElements.end())
        return;

    _index = static_cast<int>(it - frameElements.begin());
    frameElements.erase(it);
    if (_selection)
        _selection->remove(_instance);
    _instance->parent = nullptr;

    int at = _index;
    for (Element* piece : _pieces)
    {
        piece->parent = _frame;
        frameElements.insert(frameElements.begin() + at, piece);
        ++at;
    }
    _applied = true;

    if (_selection)
    {
        std::vector<DOMElement*> picked(_pieces.begin(), _pieces.end());
        _selection->select(picked);
    }
}

void BreakApartInstanceCommand::undo()
{
    if (!_frame || !_instance || !_applied)
        return;

    std::vector<Element*>& frameElements = _frame->elements;
    for (Element* piece : _pieces)
    {
        const auto it = std::find(frameElements.begin(), frameElements.end(), piece);
        if (it != frameElements.end())
            frameElements.erase(it);
        piece->parent = nullptr;
        if (_selection)
            _selection->remove(piece);
    }

    const int at = std::max(0, std::min(_index, static_cast<int>(frameElements.size())));
    _instance->parent = _frame;
    frameElements.insert(frameElements.begin() + at, _instance);
    _applied = false;

    if (_selection)
        _selection->select(_instance);
}

ConvertToSymbolCommand::ConvertToSymbolCommand(Document* document, Frame* frame,
    const std::vector<Element*>& elements, const std::string& symbolName,
    double registrationX, double registrationY, const std::string& name, Selection* selection,
    SymbolType symbolType)
    : _document(document)
    , _frame(frame)
    , _registrationX(registrationX)
    , _registrationY(registrationY)
    , _symbol(nullptr)
    , _instance(nullptr)
    , _name(name)
    , _selection(selection)
{
    if (!_document || !_frame)
        return;

    // In z-order, so the symbol draws its contents in the order they had.
    for (size_t i = 0; i < _frame->elements.size(); ++i)
    {
        Element* candidate = _frame->elements[i];
        if (std::find(elements.begin(), elements.end(), candidate) != elements.end())
        {
            _elements.push_back(candidate);
            _indices.push_back(static_cast<int>(i));
        }
    }

    // The symbol: one timeline, one layer, one keyframe, as Animate makes it.
    if (!_document->symbolList)
        _document->symbolList = new SymbolList(_document);

    _symbol = new Symbol(_document->symbolList);
    _symbol->name = symbolName;

    // Animate stamps each item with an id and a modification time; the
    // writer carries both through, so a converted symbol gets its own.
    const std::time_t now = std::time(nullptr);
    char id[40];
    std::snprintf(id, sizeof(id), "%08lx-%08lx", static_cast<unsigned long>(now),
        static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(this) & 0xffffffffu));
    _symbol->itemId = id;
    _symbol->lastModified = std::to_string(static_cast<long long>(now));

    Timeline* timeline = new Timeline(_symbol);
    timeline->name = symbolName;
    _symbol->timelines.push_back(timeline);

    Layer* layer = new Layer(timeline);
    layer->name = "Layer_1";
    layer->color[0] = 0;
    layer->color[1] = 255;
    layer->color[2] = 255;
    layer->color[3] = 255;
    layer->current = "true";
    layer->selected = true;
    timeline->layers.push_back(layer);

    _symbolFrame = new Frame(layer);
    _symbolFrame->index = 0;
    _symbolFrame->duration = 1;
    _symbolFrame->keyMode = "9728";
    layer->frames.push_back(_symbolFrame);
    layer->firstFrame = 0;
    layer->lastFrame = 1;

    _instance = new SymbolInstance(_frame);
    _instance->libraryItemName = symbolName;
    _instance->symbol = _symbol;
    _instance->symbolType = symbolType;
    _symbol->symbolType = symbolType;
    _instance->transform.tx = registrationX;
    _instance->transform.ty = registrationY;

    for (Element* element : _elements)
        collectMovable(element, _moved);
    for (Element* element : _moved)
        _movedTransforms.push_back(element->transform);
}

ConvertToSymbolCommand::~ConvertToSymbolCommand()
{
    if (!_applied)
    {
        // Nothing of the document's is inside: the symbol's frame is empty
        // and the instance was never placed.
        delete _instance;
        delete _symbol;
    }
}

void ConvertToSymbolCommand::collectMovable(Element* element, std::vector<Element*>& out) const
{
    if (!element)
        return;

    if (element->elementType() == Element::Type::Group)
    {
        for (Element* member : static_cast<Group*>(element)->members)
            collectMovable(member, out);
        return;
    }

    out.push_back(element);
}

void ConvertToSymbolCommand::redo()
{
    if (!_document || !_frame || !_symbol || !_instance || _applied || _elements.empty())
        return;

    SymbolList* library = _document->symbolList;
    library->symbols.push_back(_symbol);
    library->symbolMap[_symbol->name] = _symbol;

    std::vector<Element*>& frameElements = _frame->elements;
    for (Element* element : _elements)
    {
        const auto it = std::find(frameElements.begin(), frameElements.end(), element);
        if (it != frameElements.end())
            frameElements.erase(it);
        element->parent = _symbolFrame;
        _symbolFrame->elements.push_back(element);
        if (_selection)
            _selection->remove(element);
    }

    // Shifted so the registration point is the symbol's origin; the instance
    // puts them back where they were.
    for (Element* element : _moved)
    {
        element->transform.tx -= _registrationX;
        element->transform.ty -= _registrationY;
    }

    int at = _indices.back() - static_cast<int>(_indices.size() - 1);
    at = std::max(0, std::min(at, static_cast<int>(frameElements.size())));
    frameElements.insert(frameElements.begin() + at, _instance);
    _instance->parent = _frame;
    _applied = true;

    if (_selection)
        _selection->select(_instance);
}

void ConvertToSymbolCommand::undo()
{
    if (!_applied)
        return;

    std::vector<Element*>& frameElements = _frame->elements;
    const auto it = std::find(frameElements.begin(), frameElements.end(), static_cast<Element*>(_instance));
    if (it != frameElements.end())
        frameElements.erase(it);
    if (_selection)
        _selection->remove(_instance);

    for (size_t i = 0; i < _moved.size(); ++i)
        _moved[i]->transform = _movedTransforms[i];

    _symbolFrame->elements.clear();
    for (size_t i = 0; i < _elements.size(); ++i)
    {
        Element* element = _elements[i];
        element->parent = _frame;
        const int at = std::max(0, std::min(_indices[i], static_cast<int>(frameElements.size())));
        frameElements.insert(frameElements.begin() + at, element);
    }

    SymbolList* library = _document->symbolList;
    const auto found = std::find(library->symbols.begin(), library->symbols.end(), _symbol);
    if (found != library->symbols.end())
        library->symbols.erase(found);
    library->symbolMap.erase(_symbol->name);
    _applied = false;

    if (_selection)
    {
        std::vector<DOMElement*> picked(_elements.begin(), _elements.end());
        _selection->select(picked);
    }
}

RemoveElementCommand::~RemoveElementCommand()
{
    if (_owned)
        delete _element;
}

void RemoveElementCommand::redo()
{
    if (!_frame || !_element || _owned)
        return;

    std::vector<Element*>& elements = _frame->elements;
    const auto it = std::find(elements.begin(), elements.end(), _element);
    if (it == elements.end())
        return;

    _index = static_cast<int>(it - elements.begin());
    elements.erase(it);
    _owned = true;

    if (_selection)
        _selection->remove(_element);
}

void RemoveElementCommand::undo()
{
    if (!_frame || !_element || !_owned)
        return;

    std::vector<Element*>& elements = _frame->elements;
    if (_index < 0 || _index > static_cast<int>(elements.size()))
        _index = static_cast<int>(elements.size());

    elements.insert(elements.begin() + _index, _element);
    _owned = false;
}

} // namespace fla
