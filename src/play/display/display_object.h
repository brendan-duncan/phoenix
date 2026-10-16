#pragma once

#include "bitmap_filter.h"
#include "blend.h"
#include "events.h"
#include "geom.h"
#include "graphics.h"

#include <climits>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fla {

class Element;
class Layer;
class Timeline;

namespace play {
struct Clip;
}

namespace display {

class BitmapData;
class DisplayObjectContainer;
class Stage;

/// What a display object is, for code that draws or binds it to tell them
/// apart without a cast for each.
enum class ObjectKind
{
    Shape,
    Sprite,
    MovieClip,
    SimpleButton,
    Bitmap,
    TextField,
    Stage,
    /// Art a timeline placed that is not a movie clip, button or text field
    /// (a shape, a group, a bitmap, a graphic symbol): a Shape to scripts.
    TimelineArt
};

/// The errors the display list methods throw in ActionScript, by number.
enum class DisplayError
{
    None = 0,
    /// RangeError #2006: the supplied index is out of bounds.
    IndexOutOfRange = 2006,
    /// TypeError #2007: parameter child must be non-null.
    NullChild = 2007,
    /// ArgumentError #2024: an object cannot be added as a child of itself.
    AddSelf = 2024,
    /// ArgumentError #2025: the supplied DisplayObject must be a child of
    /// the caller.
    NotAChild = 2025,
    /// ArgumentError #2150: an object cannot be added as a child to one of
    /// its children (or children's children, etc.).
    AddAncestor = 2150
};

/// Where a timeline placed an object, for one its parent's timeline put
/// there (not one a script made).
struct TimelinePlacement
{
    /// The element on the parent's frame now, and its layer.
    const Element* element = nullptr;
    const Layer* layer = nullptr;
    /// Its place in the order the timeline draws in: the higher, the
    /// nearer the top. Script children have none.
    int depth = 0;
    /// What tells it apart from the parent's other timeline objects from
    /// frame to frame: its instance name, or its layer and place.
    std::string key;
};

/// flash.display.DisplayObject.
class DisplayObject : public EventDispatcher
{
public:
    ~DisplayObject() override;

    ObjectKind kind() const { return _kind; }
    virtual bool isInteractive() const { return false; }
    virtual bool isContainer() const { return false; }
    /// The shared pointer that owns it (they always are, made with
    /// std::make_shared); null for one that is not.
    std::shared_ptr<DisplayObject> shared();
    std::shared_ptr<const DisplayObject> shared() const;

    DisplayObjectContainer* parent() const { return _parent; }
    /// The stage it is on, or null when it is not on the display list.
    Stage* stage() const;
    /// The main timeline for one on the stage; off it, the topmost of what
    /// holds it (itself when nothing does).
    DisplayObject* root() const;
    EventDispatcher* eventParent() const override;

    const std::string& name() const { return _name; }
    void setName(const std::string& name);

    // Where it is in its parent. A timeline places it (setPlacedMatrix);
    // what a script sets goes over that: x and y, scale, rotation, or the
    // whole matrix.
    double x() const { return matrix().tx; }
    double y() const { return matrix().ty; }
    void setX(double x);
    void setY(double y);
    double scaleX() const;
    double scaleY() const;
    void setScaleX(double scale);
    void setScaleY(double scale);
    /// Degrees, -180 to 180.
    double rotation() const;
    void setRotation(double degrees);
    /// Its box's size in its parent's space; setting one scales it.
    double width() const;
    double height() const;
    /// A text field's set its box's size rather than its scale.
    virtual void setWidth(double width);
    virtual void setHeight(double height);
    /// transform.matrix.
    Matrix matrix() const;
    void setMatrix(const Matrix& matrix);
    /// \a placed (a timeline's matrix, a tween's in-between) with what
    /// scripts set over it.
    Matrix matrixOver(const Matrix& placed) const;
    /// Where its parent's timeline puts it now.
    void setPlacedMatrix(const Matrix& placed);
    const Matrix& placedMatrix() const { return _placed; }
    /// Whether a script has set any of its transform.
    bool transformScripted() const;
    /// From its own space to the stage's (transform.concatenatedMatrix):
    /// through every parent, to the topmost when it is not on the stage.
    Matrix concatenatedMatrix() const;

    /// alpha, 0 to 1: colorTransform's alpha multiplier.
    double alpha() const;
    void setAlpha(double alpha);
    /// Whether a script set alpha (rather than the whole color transform).
    bool alphaScripted() const { return _alphaSet.has_value(); }
    /// transform.colorTransform: what a script set, else what the timeline
    /// placed.
    ColorTransform colorTransform() const;
    void setColorTransform(const ColorTransform& transform);
    void setPlacedColorTransform(const ColorTransform& transform);
    bool colorScripted() const { return _colorSet.has_value(); }
    /// Its own and every parent's, the parent's applying after.
    ColorTransform concatenatedColorTransform() const;
    bool visible() const { return _visible; }
    void setVisible(bool visible);

    /// filters: copies of what a script assigned.
    const std::vector<BitmapFilter>& filters() const { return _filters; }
    void setFilters(std::vector<BitmapFilter> filters);
    bool filtersScripted() const { return _filtersSet; }
    BlendMode blendMode() const { return _blendMode; }
    void setBlendMode(BlendMode mode);
    bool blendModeScripted() const { return _blendModeSet; }
    /// mask: the object whose shape it shows through; null for none. A
    /// mask does not draw itself.
    DisplayObject* mask() const;
    void setMask(const std::shared_ptr<DisplayObject>& mask);
    /// The object this masks, or null.
    DisplayObject* maskedObject() const { return _maskOf; }
    bool cacheAsBitmap() const { return _cacheAsBitmap; }
    void setCacheAsBitmap(bool cache);
    /// opaqueBackground: a color drawn under it, or none.
    std::optional<uint32_t> opaqueBackground() const { return _opaqueBackground; }
    void setOpaqueBackground(std::optional<uint32_t> color);
    /// scrollRect: the part of it that shows, its corner at its origin.
    std::optional<Rect> scrollRect() const { return _scrollRect; }
    void setScrollRect(std::optional<Rect> rect);
    std::optional<Rect> scale9Grid() const { return _scale9Grid; }
    void setScale9Grid(std::optional<Rect> rect);

    /// getBounds and getRect: its box (with strokes, or without) in
    /// \a space's coordinates; its own when \a space is itself or null.
    Rect getBounds(const DisplayObject* space) const;
    Rect getRect(const DisplayObject* space) const;
    Point localToGlobal(const Point& point) const;
    Point globalToLocal(const Point& point) const;
    /// hitTestPoint: whether (\a x, \a y) on the stage is in its box, or
    /// on its art with \a shapeFlag.
    bool hitTestPoint(double x, double y, bool shapeFlag = false) const;
    /// hitTestObject: whether the two boxes on the stage overlap.
    bool hitTestObject(const DisplayObject& other) const;
    /// The pointer in its own space.
    double mouseX() const;
    double mouseY() const;

    /// The box its own art covers (Graphics, bitmap, text, timeline art),
    /// not its children's, in its own space.
    virtual Rect contentBounds(bool strokes) const;
    /// Whether its own art is at (\a x, \a y) in its own space.
    virtual bool contentHits(double x, double y) const;
    /// Its box with its children's, mapped by \a toSpace from its own
    /// space; masks and scroll rects cut it.
    Rect boundsThrough(const Matrix& toSpace, bool strokes) const;
    /// Whether its art or any child's is at (\a x, \a y) in its own space,
    /// as drawn (hidden children and what a mask hides do not count).
    bool hitsLocal(double x, double y) const;
    /// From its own space to its parent's: its matrix, after the shift its
    /// scrollRect makes.
    Matrix localToParent() const;

    /// How it looks changed: whatever draws it draws again.
    void invalidate();
    /// Bumped by every change to it or what is in it.
    uint64_t revision() const { return _revision; }

    /// Set for one its parent's timeline placed.
    std::optional<TimelinePlacement> timeline;
    /// The timeline clip behind a MovieClip or SimpleButton that a
    /// timeline placed or the runtime made from a library symbol; null for
    /// one made in script with no symbol.
    play::Clip* clip = nullptr;

protected:
    explicit DisplayObject(ObjectKind kind);

private:
    friend class DisplayObjectContainer;
    friend class SimpleButton;
    friend class Stage;

    ObjectKind _kind;
    DisplayObjectContainer* _parent = nullptr;
    std::string _name;
    Matrix _placed;
    std::optional<Matrix> _matrixSet;
    std::optional<double> _xSet, _ySet, _scaleXSet, _scaleYSet, _rotationSet;
    ColorTransform _placedColor;
    std::optional<ColorTransform> _colorSet;
    std::optional<double> _alphaSet;
    bool _visible = true;
    std::vector<BitmapFilter> _filters;
    bool _filtersSet = false;
    BlendMode _blendMode = BlendMode::Normal;
    bool _blendModeSet = false;
    std::weak_ptr<DisplayObject> _mask;
    DisplayObject* _maskOf = nullptr;
    bool _cacheAsBitmap = false;
    std::optional<uint32_t> _opaqueBackground;
    std::optional<Rect> _scrollRect;
    std::optional<Rect> _scale9Grid;
    uint64_t _revision = 1;
};

/// flash.display.InteractiveObject.
class InteractiveObject : public DisplayObject
{
public:
    bool isInteractive() const override { return true; }

    bool mouseEnabled = true;
    bool doubleClickEnabled = false;
    bool tabEnabled = false;
    int tabIndex = -1;
    /// focusRect: null (the stage's), or on or off.
    std::optional<bool> focusRect;

protected:
    using DisplayObject::DisplayObject;
};

/// flash.display.DisplayObjectContainer: its children, drawn first to last.
class DisplayObjectContainer : public InteractiveObject
{
public:
    ~DisplayObjectContainer() override;
    bool isContainer() const override { return true; }

    int numChildren() const { return static_cast<int>(_children.size()); }
    const std::vector<std::shared_ptr<DisplayObject>>& children() const { return _children; }

    /// addChild and addChildAt: \a child taken from where it was and put
    /// on top, or at \a index; added and addedToStage dispatched.
    DisplayError addChild(const std::shared_ptr<DisplayObject>& child);
    DisplayError addChildAt(const std::shared_ptr<DisplayObject>& child, int index);
    /// removeChild and the rest: removed and removedFromStage dispatched
    /// before it goes.
    DisplayError removeChild(DisplayObject* child);
    DisplayError removeChildAt(int index, std::shared_ptr<DisplayObject>* removed = nullptr);
    DisplayError removeChildren(int beginIndex = 0, int endIndex = INT_MAX);
    /// Null when out of range.
    std::shared_ptr<DisplayObject> getChildAt(int index) const;
    /// -1 when not a child.
    int getChildIndex(const DisplayObject* child) const;
    DisplayError setChildIndex(DisplayObject* child, int index);
    DisplayError swapChildren(DisplayObject* first, DisplayObject* second);
    DisplayError swapChildrenAt(int first, int second);
    /// Whether \a object is this or anywhere inside it.
    bool contains(const DisplayObject* object) const;
    std::shared_ptr<DisplayObject> getChildByName(const std::string& name) const;
    /// What is under (\a x, \a y) on the stage, bottom first.
    std::vector<std::shared_ptr<DisplayObject>> getObjectsUnderPoint(const Point& point) const;

    bool mouseChildren = true;
    bool tabChildren = true;

    /// A timeline puts \a child in by its depth (child->timeline): over the
    /// timeline children below it, under the first above it, else on top.
    void placeTimelineChild(const std::shared_ptr<DisplayObject>& child);
    /// A timeline takes \a child off (removed events and all).
    void removeTimelineChild(DisplayObject* child);
    /// The timeline child placed for \a element on the frame now, or null.
    DisplayObject* timelineChildFor(const Element* element) const;

protected:
    using InteractiveObject::InteractiveObject;

private:
    /// Puts \a child at \a index, taken off its old parent first.
    void insert(const std::shared_ptr<DisplayObject>& child, int index);
    /// Takes the child at \a index off, after telling it.
    std::shared_ptr<DisplayObject> take(int index);

    std::vector<std::shared_ptr<DisplayObject>> _children;
};

/// flash.display.Shape: its own drawing and nothing else.
class Shape : public DisplayObject
{
public:
    Shape()
        : DisplayObject(ObjectKind::Shape)
        , graphics(this)
    {}
    Rect contentBounds(bool strokes) const override { return graphics.bounds(strokes); }
    bool contentHits(double x, double y) const override { return graphics.hits(x, y); }

    Graphics graphics;
};

/// Art a timeline placed that scripts see as a Shape: an element of a
/// frame (a shape, group, bitmap, static text, graphic symbol), or a whole
/// frame of a timeline (a button's state).
class TimelineArt : public DisplayObject
{
public:
    TimelineArt()
        : DisplayObject(ObjectKind::TimelineArt)
    {}
    Rect contentBounds(bool strokes) const override;
    bool contentHits(double x, double y) const override;

    /// The element it draws, or null for a whole frame...
    const Element* element = nullptr;
    /// ...of this timeline.
    const Timeline* frameOf = nullptr;
    int frame = 0;
};

/// flash.display.Sprite.
class Sprite : public DisplayObjectContainer
{
public:
    Sprite()
        : Sprite(ObjectKind::Sprite)
    {}
    ~Sprite() override;
    Rect contentBounds(bool strokes) const override { return graphics.bounds(strokes); }
    bool contentHits(double x, double y) const override { return graphics.hits(x, y); }

    Graphics graphics;
    bool buttonMode = false;
    bool useHandCursor = true;
    /// hitArea: what the pointer finds it by in place of its own art.
    std::shared_ptr<Sprite> hitArea() const { return _hitArea.lock(); }
    void setHitArea(const std::shared_ptr<Sprite>& area);
    /// The sprite it is the hit area of, if any.
    Sprite* hitAreaOf() const { return _hitAreaOf; }

protected:
    explicit Sprite(ObjectKind kind)
        : DisplayObjectContainer(kind)
        , graphics(this)
    {}

private:
    std::weak_ptr<Sprite> _hitArea;
    Sprite* _hitAreaOf = nullptr;
};

/// flash.display.MovieClip: a Sprite with frames. One a timeline placed, or
/// the runtime made from a library symbol, is played by its play::Clip
/// (clip); one a script made with new MovieClip() has one frame.
class MovieClip : public Sprite
{
public:
    MovieClip()
        : Sprite(ObjectKind::MovieClip)
    {}
    bool enabled = true;
    bool trackAsMenu = false;
};

/// flash.display.SimpleButton: four states, one shown at a time and the
/// hit test state for the pointer.
class SimpleButton : public InteractiveObject
{
public:
    enum class State
    {
        Up,
        Over,
        Down
    };

    SimpleButton()
        : InteractiveObject(ObjectKind::SimpleButton)
    {}

    std::shared_ptr<DisplayObject> upState;
    std::shared_ptr<DisplayObject> overState;
    std::shared_ptr<DisplayObject> downState;
    std::shared_ptr<DisplayObject> hitTestState;
    bool enabled = true;
    bool useHandCursor = true;
    bool trackAsMenu = false;

    /// The state the pointer has it in, and the object shown for it.
    State state() const { return _state; }
    void setState(State state);
    DisplayObject* shownState() const;

    Rect contentBounds(bool strokes) const override;
    bool contentHits(double x, double y) const override;

private:
    State _state = State::Up;
};

/// flash.display.PixelSnapping.
enum class PixelSnapping
{
    Never,
    Auto,
    Always
};

/// flash.display.Bitmap: a BitmapData's pixels, its corner at its origin.
class Bitmap : public DisplayObject
{
public:
    explicit Bitmap(std::shared_ptr<BitmapData> data = nullptr, PixelSnapping snapping = PixelSnapping::Auto,
        bool smoothing = false);

    std::shared_ptr<BitmapData> bitmapData() const { return _data; }
    void setBitmapData(std::shared_ptr<BitmapData> data);
    PixelSnapping pixelSnapping = PixelSnapping::Auto;
    bool smoothing = false;

    Rect contentBounds(bool strokes) const override;
    bool contentHits(double x, double y) const override;

private:
    std::shared_ptr<BitmapData> _data;
};

} // namespace display
} // namespace fla
