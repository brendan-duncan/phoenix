#include "display_object.h"

#include "backend.h"
#include "bitmap_data.h"
#include "stage.h"

#include <algorithm>
#include <cmath>

namespace fla {
namespace display {

namespace {

Backend* theBackend = nullptr;
uint64_t generation = 1;

/// Scale and the two angles (of the x axis and of the y axis) a matrix is
/// made of, as Flash Player takes them apart: a mirrored one has a
/// negative y scale.
void decompose(const Matrix& m, double& sx, double& sy, double& rx, double& ry)
{
    sx = std::hypot(m.a, m.b);
    sy = std::hypot(m.c, m.d);
    rx = std::atan2(m.b, m.a);
    if (m.determinant() < 0.0)
    {
        sy = -sy;
        ry = std::atan2(m.c, -m.d);
    }
    else
        ry = std::atan2(-m.c, m.d);
}

/// Degrees to the range ActionScript gives rotation in, -180 to 180.
double normalizedDegrees(double degrees)
{
    if (!std::isfinite(degrees))
        return 0.0;
    degrees = std::fmod(degrees, 360.0);
    if (degrees > 180.0)
        degrees -= 360.0;
    else if (degrees < -180.0)
        degrees += 360.0;
    return degrees;
}

/// A box with any extent: a lone point (or nothing) has none, a line has.
bool hasExtent(const Rect& rect)
{
    return rect.width > 0.0 || rect.height > 0.0;
}

/// Grows (l, t, r, b) by \a rect; \a any says whether it holds anything yet.
void include(bool& any, double& l, double& t, double& r, double& b, const Rect& rect)
{
    if (!any)
    {
        l = rect.left();
        t = rect.top();
        r = rect.right();
        b = rect.bottom();
        any = true;
        return;
    }
    l = std::min(l, rect.left());
    t = std::min(t, rect.top());
    r = std::max(r, rect.right());
    b = std::max(b, rect.bottom());
}

/// Sends \a type to \a object and everything in it, parents first.
void tellAll(DisplayObject& object, const char* type)
{
    std::shared_ptr<DisplayObject> keep = object.shared();
    Event event(type);
    object.dispatchEvent(event);
    if (object.isContainer())
    {
        const std::vector<std::shared_ptr<DisplayObject>> children =
            static_cast<DisplayObjectContainer&>(object).children();
        for (const std::shared_ptr<DisplayObject>& child : children)
            if (child)
                tellAll(*child, type);
    }
}

} // namespace

Backend* backend()
{
    return theBackend;
}

void setBackend(Backend* backend)
{
    theBackend = backend;
}

uint64_t displayGeneration()
{
    return generation;
}

void markDisplayChanged()
{
    ++generation;
}

DisplayObject::DisplayObject(ObjectKind kind)
    : _kind(kind)
{}

DisplayObject::~DisplayObject()
{
    if (_maskOf)
        _maskOf->_mask.reset();
    if (std::shared_ptr<DisplayObject> mask = _mask.lock())
        mask->_maskOf = nullptr;
}

std::shared_ptr<DisplayObject> DisplayObject::shared()
{
    return std::static_pointer_cast<DisplayObject>(weak_from_this().lock());
}

std::shared_ptr<const DisplayObject> DisplayObject::shared() const
{
    return std::static_pointer_cast<const DisplayObject>(weak_from_this().lock());
}

Stage* DisplayObject::stage() const
{
    for (const DisplayObject* at = this; at; at = at->_parent)
        if (at->_kind == ObjectKind::Stage)
            return const_cast<Stage*>(static_cast<const Stage*>(at));
    return nullptr;
}

DisplayObject* DisplayObject::root() const
{
    if (Stage* on = stage())
        return on->rootObject() ? on->rootObject() : on;
    const DisplayObject* top = this;
    while (top->_parent)
        top = top->_parent;
    return const_cast<DisplayObject*>(top);
}

EventDispatcher* DisplayObject::eventParent() const
{
    return _parent;
}

void DisplayObject::setName(const std::string& name)
{
    _name = name;
}

Matrix DisplayObject::matrixOver(const Matrix& placed) const
{
    Matrix base = _matrixSet ? *_matrixSet : placed;
    if (_scaleXSet || _scaleYSet || _rotationSet)
    {
        double sx, sy, rx, ry;
        decompose(base, sx, sy, rx, ry);
        if (_scaleXSet)
            sx = *_scaleXSet;
        if (_scaleYSet)
            sy = *_scaleYSet;
        if (_rotationSet)
        {
            // Turned as a whole: any skew between the axes is kept.
            const double turn = *_rotationSet * kPi / 180.0;
            ry += turn - rx;
            rx = turn;
        }
        base.a = sx * std::cos(rx);
        base.b = sx * std::sin(rx);
        base.c = -sy * std::sin(ry);
        base.d = sy * std::cos(ry);
    }
    if (_xSet)
        base.tx = *_xSet;
    if (_ySet)
        base.ty = *_ySet;
    return base;
}

Matrix DisplayObject::matrix() const
{
    return matrixOver(_placed);
}

Matrix DisplayObject::localToParent() const
{
    const Matrix m = matrix();
    if (!_scrollRect)
        return m;
    return Matrix(1, 0, 0, 1, -_scrollRect->x, -_scrollRect->y).then(m);
}

void DisplayObject::setMatrix(const Matrix& m)
{
    _matrixSet = m;
    _xSet.reset();
    _ySet.reset();
    _scaleXSet.reset();
    _scaleYSet.reset();
    _rotationSet.reset();
    invalidate();
}

void DisplayObject::setPlacedMatrix(const Matrix& placed)
{
    if (placed == _placed)
        return;
    _placed = placed;
    invalidate();
}

bool DisplayObject::transformScripted() const
{
    return _matrixSet || _xSet || _ySet || _scaleXSet || _scaleYSet || _rotationSet;
}

void DisplayObject::setX(double x)
{
    _xSet = std::isfinite(x) ? x : 0.0;
    invalidate();
}

void DisplayObject::setY(double y)
{
    _ySet = std::isfinite(y) ? y : 0.0;
    invalidate();
}

double DisplayObject::scaleX() const
{
    if (_scaleXSet)
        return *_scaleXSet;
    double sx, sy, rx, ry;
    decompose(matrix(), sx, sy, rx, ry);
    return sx;
}

double DisplayObject::scaleY() const
{
    if (_scaleYSet)
        return *_scaleYSet;
    double sx, sy, rx, ry;
    decompose(matrix(), sx, sy, rx, ry);
    return sy;
}

void DisplayObject::setScaleX(double scale)
{
    _scaleXSet = std::isfinite(scale) ? scale : 0.0;
    invalidate();
}

void DisplayObject::setScaleY(double scale)
{
    _scaleYSet = std::isfinite(scale) ? scale : 0.0;
    invalidate();
}

double DisplayObject::rotation() const
{
    if (_rotationSet)
        return *_rotationSet;
    const Matrix m = matrix();
    return normalizedDegrees(std::atan2(m.b, m.a) * 180.0 / kPi);
}

void DisplayObject::setRotation(double degrees)
{
    _rotationSet = normalizedDegrees(degrees);
    invalidate();
}

double DisplayObject::width() const
{
    return boundsThrough(matrix(), true).width;
}

double DisplayObject::height() const
{
    return boundsThrough(matrix(), true).height;
}

void DisplayObject::setWidth(double width)
{
    if (!std::isfinite(width))
        return;
    const Matrix m = matrix();
    const Rect local = boundsThrough(Matrix(), true);
    if (!(local.width > 0.0))
        return;
    if (std::fabs(m.b) < 1e-12 && std::fabs(m.c) < 1e-12)
    {
        setScaleX(width / local.width * (scaleX() < 0.0 ? -1.0 : 1.0));
        return;
    }
    const double now = this->width();
    if (now > 0.0)
        setScaleX(scaleX() * width / now);
}

void DisplayObject::setHeight(double height)
{
    if (!std::isfinite(height))
        return;
    const Matrix m = matrix();
    const Rect local = boundsThrough(Matrix(), true);
    if (!(local.height > 0.0))
        return;
    if (std::fabs(m.b) < 1e-12 && std::fabs(m.c) < 1e-12)
    {
        setScaleY(height / local.height * (scaleY() < 0.0 ? -1.0 : 1.0));
        return;
    }
    const double now = this->height();
    if (now > 0.0)
        setScaleY(scaleY() * height / now);
}

Matrix DisplayObject::concatenatedMatrix() const
{
    Matrix m = localToParent();
    for (const DisplayObject* at = _parent; at && at->_kind != ObjectKind::Stage; at = at->_parent)
        m = m.then(at->localToParent());
    return m;
}

double DisplayObject::alpha() const
{
    return _alphaSet ? *_alphaSet : colorTransform().alphaMultiplier;
}

void DisplayObject::setAlpha(double alpha)
{
    _alphaSet = std::isnan(alpha) ? 0.0 : alpha;
    invalidate();
}

ColorTransform DisplayObject::colorTransform() const
{
    ColorTransform shown = _colorSet ? *_colorSet : _placedColor;
    if (_alphaSet)
        shown.alphaMultiplier = *_alphaSet;
    return shown;
}

void DisplayObject::setColorTransform(const ColorTransform& transform)
{
    _colorSet = transform;
    _alphaSet.reset();
    invalidate();
}

void DisplayObject::setPlacedColorTransform(const ColorTransform& transform)
{
    _placedColor = transform;
    invalidate();
}

ColorTransform DisplayObject::concatenatedColorTransform() const
{
    ColorTransform shown = colorTransform();
    for (const DisplayObject* at = _parent; at; at = at->_parent)
    {
        ColorTransform outer = at->colorTransform();
        outer.concat(shown);
        shown = outer;
    }
    return shown;
}

void DisplayObject::setVisible(bool visible)
{
    if (_visible == visible)
        return;
    _visible = visible;
    invalidate();
}

void DisplayObject::setFilters(std::vector<BitmapFilter> filters)
{
    _filters = std::move(filters);
    _filtersSet = true;
    invalidate();
}

void DisplayObject::setPlacedFilters(std::vector<BitmapFilter> filters)
{
    if (_filtersSet)
        return;
    _filters = std::move(filters);
    invalidate();
}

void DisplayObject::setPlacedBlendMode(BlendMode mode)
{
    if (_blendModeSet || _blendMode == mode)
        return;
    _blendMode = mode;
    invalidate();
}

bool DisplayObject::effectsScripted() const
{
    return _filtersSet || _blendModeSet || (_colorSet && !_colorSet->alphaOnly()) || mask() || _scrollRect ||
        _opaqueBackground;
}

void DisplayObject::clearScriptedX()
{
    _xSet.reset();
    invalidate();
}

void DisplayObject::clearScriptedY()
{
    _ySet.reset();
    invalidate();
}

void DisplayObject::setBlendMode(BlendMode mode)
{
    _blendMode = mode;
    _blendModeSet = true;
    invalidate();
}

DisplayObject* DisplayObject::mask() const
{
    return _mask.lock().get();
}

void DisplayObject::setMask(const std::shared_ptr<DisplayObject>& mask)
{
    if (std::shared_ptr<DisplayObject> old = _mask.lock())
        old->_maskOf = nullptr;
    _mask.reset();
    if (mask && mask.get() != this)
    {
        // A mask masks one object at a time.
        if (mask->_maskOf)
            mask->_maskOf->_mask.reset();
        mask->_maskOf = this;
        _mask = mask;
        mask->invalidate();
    }
    invalidate();
}

void DisplayObject::setCacheAsBitmap(bool cache)
{
    _cacheAsBitmap = cache;
    invalidate();
}

void DisplayObject::setOpaqueBackground(std::optional<uint32_t> color)
{
    _opaqueBackground = color;
    invalidate();
}

void DisplayObject::setScrollRect(std::optional<Rect> rect)
{
    _scrollRect = rect;
    invalidate();
}

void DisplayObject::setScale9Grid(std::optional<Rect> rect)
{
    _scale9Grid = rect;
    invalidate();
}

Rect DisplayObject::contentBounds(bool) const
{
    return Rect();
}

bool DisplayObject::contentHits(double, double) const
{
    return false;
}

Rect DisplayObject::boundsThrough(const Matrix& toSpace, bool strokes) const
{
    bool any = false;
    double l = 0, t = 0, r = 0, b = 0;
    const Rect own = contentBounds(strokes);
    if (hasExtent(own))
        include(any, l, t, r, b, toSpace.mapRect(own));
    if (isContainer())
        for (const std::shared_ptr<DisplayObject>& child : static_cast<const DisplayObjectContainer*>(this)->children())
        {
            if (!child)
                continue;
            const Rect inner = child->boundsThrough(child->localToParent().then(toSpace), strokes);
            if (hasExtent(inner))
                include(any, l, t, r, b, inner);
        }
    if (!any)
        return Rect();
    Rect box(l, t, r - l, b - t);
    if (_scrollRect)
        box = box.intersected(toSpace.mapRect(*_scrollRect));
    return box;
}

bool DisplayObject::hitsLocal(double x, double y) const
{
    if (_scrollRect && !_scrollRect->contains(x, y))
        return false;
    // Only where its mask shows it.
    if (const DisplayObject* mask = this->mask())
    {
        const Point onStage = concatenatedMatrix().map(x, y);
        const Point inMask = mask->concatenatedMatrix().inverted().map(onStage);
        if (!mask->hitsLocal(inMask.x, inMask.y))
            return false;
    }
    if (contentHits(x, y))
        return true;
    if (!isContainer())
        return false;
    for (const std::shared_ptr<DisplayObject>& child : static_cast<const DisplayObjectContainer*>(this)->children())
    {
        if (!child || !child->_visible || child->_maskOf)
            continue;
        bool invertible = false;
        const Point inChild = child->localToParent().inverted(&invertible).map(x, y);
        if (invertible && child->hitsLocal(inChild.x, inChild.y))
            return true;
    }
    return false;
}

Rect DisplayObject::getBounds(const DisplayObject* space) const
{
    Matrix toSpace;
    if (space && space != this)
        toSpace = concatenatedMatrix().then(space->concatenatedMatrix().inverted());
    return boundsThrough(toSpace, true);
}

Rect DisplayObject::getRect(const DisplayObject* space) const
{
    Matrix toSpace;
    if (space && space != this)
        toSpace = concatenatedMatrix().then(space->concatenatedMatrix().inverted());
    return boundsThrough(toSpace, false);
}

Point DisplayObject::localToGlobal(const Point& point) const
{
    return concatenatedMatrix().map(point);
}

Point DisplayObject::globalToLocal(const Point& point) const
{
    return concatenatedMatrix().inverted().map(point);
}

bool DisplayObject::hitTestPoint(double x, double y, bool shapeFlag) const
{
    if (!shapeFlag)
        return boundsThrough(concatenatedMatrix(), true).contains(x, y);
    bool invertible = false;
    const Point local = concatenatedMatrix().inverted(&invertible).map(x, y);
    return invertible && hitsLocal(local.x, local.y);
}

bool DisplayObject::hitTestObject(const DisplayObject& other) const
{
    const Rect a = boundsThrough(concatenatedMatrix(), true);
    const Rect b = other.boundsThrough(other.concatenatedMatrix(), true);
    return a.right() >= b.left() && b.right() >= a.left() && a.bottom() >= b.top() && b.bottom() >= a.top() &&
        hasExtent(a) && hasExtent(b);
}

double DisplayObject::mouseX() const
{
    const Stage* on = stage();
    const Point local = globalToLocal(on ? Point{on->pointerX(), on->pointerY()} : Point());
    return local.x;
}

double DisplayObject::mouseY() const
{
    const Stage* on = stage();
    const Point local = globalToLocal(on ? Point{on->pointerX(), on->pointerY()} : Point());
    return local.y;
}

void DisplayObject::invalidate()
{
    for (DisplayObject* at = this; at; at = at->_parent)
        ++at->_revision;
    // A mask shows through what it masks.
    if (_maskOf)
        for (DisplayObject* at = _maskOf; at; at = at->_parent)
            ++at->_revision;
    markDisplayChanged();
    if (Stage* on = stage())
        if (on->changed)
            on->changed();
}

// DisplayObjectContainer

DisplayObjectContainer::~DisplayObjectContainer()
{
    for (const std::shared_ptr<DisplayObject>& child : _children)
        if (child && child->_parent == this)
            child->_parent = nullptr;
}

std::shared_ptr<DisplayObject> DisplayObjectContainer::getChildAt(int index) const
{
    if (index < 0 || index >= numChildren())
        return nullptr;
    return _children[static_cast<size_t>(index)];
}

int DisplayObjectContainer::getChildIndex(const DisplayObject* child) const
{
    for (size_t i = 0; i < _children.size(); ++i)
        if (_children[i].get() == child)
            return static_cast<int>(i);
    return -1;
}

bool DisplayObjectContainer::contains(const DisplayObject* object) const
{
    for (const DisplayObject* at = object; at; at = at->parent())
        if (at == this)
            return true;
    return false;
}

std::shared_ptr<DisplayObject> DisplayObjectContainer::getChildByName(const std::string& name) const
{
    for (const std::shared_ptr<DisplayObject>& child : _children)
        if (child && child->name() == name)
            return child;
    return nullptr;
}

DisplayError DisplayObjectContainer::addChild(const std::shared_ptr<DisplayObject>& child)
{
    if (child && child->_parent == this)
        return addChildAt(child, numChildren() - 1);
    return addChildAt(child, numChildren());
}

DisplayError DisplayObjectContainer::addChildAt(const std::shared_ptr<DisplayObject>& child, int index)
{
    if (!child)
        return DisplayError::NullChild;
    if (child.get() == this)
        return DisplayError::AddSelf;
    if (child->isContainer() && static_cast<DisplayObjectContainer*>(child.get())->contains(this))
        return DisplayError::AddAncestor;
    if (child->_kind == ObjectKind::Stage)
        return DisplayError::AddAncestor;
    if (child->_parent == this)
    {
        // Already here: moved to the place asked.
        if (index < 0 || index >= numChildren())
            return DisplayError::IndexOutOfRange;
        return setChildIndex(child.get(), index);
    }
    if (index < 0 || index > numChildren())
        return DisplayError::IndexOutOfRange;
    insert(child, index);
    return DisplayError::None;
}

void DisplayObjectContainer::insert(const std::shared_ptr<DisplayObject>& child, int index)
{
    std::shared_ptr<DisplayObject> keep = child;
    if (DisplayObjectContainer* old = child->_parent)
    {
        old->removeChild(child.get());
        // The old parent's listeners may have changed this one.
        index = std::min(index, numChildren());
    }
    if (child->_parent)
        return;
    _children.insert(_children.begin() + index, child);
    child->_parent = this;
    child->invalidate();
    Event added(events::kAdded, true);
    child->dispatchEvent(added);
    if (stage() && child->_parent == this)
        tellAll(*child, events::kAddedToStage);
}

std::shared_ptr<DisplayObject> DisplayObjectContainer::take(int index)
{
    std::shared_ptr<DisplayObject> child = _children[static_cast<size_t>(index)];
    Event removed(events::kRemoved, true);
    child->dispatchEvent(removed);
    if (Stage* on = stage())
    {
        tellAll(*child, events::kRemovedFromStage);
        // What has the focus goes with it.
        const InteractiveObject* focused = on->focus();
        if (focused && (focused == child.get() ||
                           (child->isContainer() && static_cast<DisplayObjectContainer*>(child.get())->contains(focused))))
            on->setFocus(nullptr);
    }
    // Listeners may have moved it already.
    const int now = getChildIndex(child.get());
    if (now >= 0)
    {
        invalidate();
        _children.erase(_children.begin() + now);
        child->_parent = nullptr;
    }
    return child;
}

DisplayError DisplayObjectContainer::removeChild(DisplayObject* child)
{
    if (!child)
        return DisplayError::NullChild;
    const int index = getChildIndex(child);
    if (index < 0)
        return DisplayError::NotAChild;
    take(index);
    return DisplayError::None;
}

DisplayError DisplayObjectContainer::removeChildAt(int index, std::shared_ptr<DisplayObject>* removed)
{
    if (index < 0 || index >= numChildren())
        return DisplayError::IndexOutOfRange;
    std::shared_ptr<DisplayObject> child = take(index);
    if (removed)
        *removed = std::move(child);
    return DisplayError::None;
}

DisplayError DisplayObjectContainer::removeChildren(int beginIndex, int endIndex)
{
    if (numChildren() == 0 && beginIndex == 0)
        return DisplayError::None;
    if (endIndex == INT_MAX)
        endIndex = numChildren() - 1;
    if (beginIndex < 0 || endIndex < beginIndex || endIndex >= numChildren())
        return DisplayError::IndexOutOfRange;
    std::vector<DisplayObject*> going;
    for (int i = beginIndex; i <= endIndex; ++i)
        going.push_back(_children[static_cast<size_t>(i)].get());
    for (DisplayObject* child : going)
        removeChild(child);
    return DisplayError::None;
}

DisplayError DisplayObjectContainer::setChildIndex(DisplayObject* child, int index)
{
    if (!child)
        return DisplayError::NullChild;
    const int from = getChildIndex(child);
    if (from < 0)
        return DisplayError::NotAChild;
    if (index < 0 || index >= numChildren())
        return DisplayError::IndexOutOfRange;
    if (from == index)
        return DisplayError::None;
    std::shared_ptr<DisplayObject> held = _children[static_cast<size_t>(from)];
    _children.erase(_children.begin() + from);
    _children.insert(_children.begin() + index, held);
    invalidate();
    return DisplayError::None;
}

DisplayError DisplayObjectContainer::swapChildren(DisplayObject* first, DisplayObject* second)
{
    if (!first || !second)
        return DisplayError::NullChild;
    const int a = getChildIndex(first);
    const int b = getChildIndex(second);
    if (a < 0 || b < 0)
        return DisplayError::NotAChild;
    return swapChildrenAt(a, b);
}

DisplayError DisplayObjectContainer::swapChildrenAt(int first, int second)
{
    if (first < 0 || second < 0 || first >= numChildren() || second >= numChildren())
        return DisplayError::IndexOutOfRange;
    std::swap(_children[static_cast<size_t>(first)], _children[static_cast<size_t>(second)]);
    invalidate();
    return DisplayError::None;
}

std::vector<std::shared_ptr<DisplayObject>> DisplayObjectContainer::getObjectsUnderPoint(const Point& point) const
{
    std::vector<std::shared_ptr<DisplayObject>> under;
    for (const std::shared_ptr<DisplayObject>& child : _children)
    {
        if (!child)
            continue;
        if (child->isContainer())
        {
            const auto* container = static_cast<const DisplayObjectContainer*>(child.get());
            if (child->contentHits(child->globalToLocal(point).x, child->globalToLocal(point).y))
                under.push_back(child);
            const auto inner = container->getObjectsUnderPoint(point);
            under.insert(under.end(), inner.begin(), inner.end());
        }
        else if (child->hitTestPoint(point.x, point.y, true))
            under.push_back(child);
    }
    return under;
}

void DisplayObjectContainer::placeTimelineChild(const std::shared_ptr<DisplayObject>& child)
{
    if (!child || !child->timeline)
        return;
    const int depth = child->timeline->depth;
    int at = numChildren();
    for (size_t i = 0; i < _children.size(); ++i)
    {
        const DisplayObject* other = _children[i].get();
        if (other && other != child.get() && other->timeline && other->timeline->depth > depth)
        {
            at = static_cast<int>(i);
            break;
        }
    }
    if (child->_parent == this)
    {
        const int from = getChildIndex(child.get());
        setChildIndex(child.get(), at > from ? at - 1 : at);
        return;
    }
    insert(child, at);
}

void DisplayObjectContainer::removeTimelineChild(DisplayObject* child)
{
    removeChild(child);
}

DisplayObject* DisplayObjectContainer::timelineChildFor(const Element* element) const
{
    for (const std::shared_ptr<DisplayObject>& child : _children)
        if (child && child->timeline && child->timeline->element == element)
            return child.get();
    return nullptr;
}

// Leaves

Rect TimelineArt::contentBounds(bool strokes) const
{
    Backend* renderer = backend();
    return renderer ? renderer->artBounds(*this, strokes) : Rect();
}

bool TimelineArt::contentHits(double x, double y) const
{
    Backend* renderer = backend();
    return renderer && renderer->artHits(*this, x, y);
}

Sprite::~Sprite()
{
    if (std::shared_ptr<Sprite> area = _hitArea.lock())
        area->_hitAreaOf = nullptr;
}

void Sprite::setHitArea(const std::shared_ptr<Sprite>& area)
{
    if (std::shared_ptr<Sprite> old = _hitArea.lock())
        old->_hitAreaOf = nullptr;
    _hitArea = area;
    if (area)
    {
        if (area->_hitAreaOf)
            area->_hitAreaOf->_hitArea.reset();
        area->_hitAreaOf = this;
    }
}

void SimpleButton::setState(State state)
{
    if (_state == state)
        return;
    _state = state;
    invalidate();
}

DisplayObject* SimpleButton::shownState() const
{
    switch (_state)
    {
    case State::Over: return overState.get();
    case State::Down: return downState.get();
    default: return upState.get();
    }
}

Rect SimpleButton::contentBounds(bool strokes) const
{
    const DisplayObject* shown = shownState();
    return shown ? shown->boundsThrough(shown->localToParent(), strokes) : Rect();
}

bool SimpleButton::contentHits(double x, double y) const
{
    const DisplayObject* hit = hitTestState.get();
    if (!hit)
        return false;
    bool invertible = false;
    const Point inState = hit->localToParent().inverted(&invertible).map(x, y);
    return invertible && hit->hitsLocal(inState.x, inState.y);
}

Bitmap::Bitmap(std::shared_ptr<BitmapData> data, PixelSnapping snapping, bool smoothing)
    : DisplayObject(ObjectKind::Bitmap)
    , pixelSnapping(snapping)
    , smoothing(smoothing)
    , _data(std::move(data))
{}

void Bitmap::setBitmapData(std::shared_ptr<BitmapData> data)
{
    _data = std::move(data);
    invalidate();
}

Rect Bitmap::contentBounds(bool) const
{
    if (!_data || _data->disposed())
        return Rect();
    return _data->rect();
}

bool Bitmap::contentHits(double x, double y) const
{
    return _data && !_data->disposed() && _data->rect().contains(x, y);
}

} // namespace display
} // namespace fla
