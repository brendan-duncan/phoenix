#include "asset_warp_tool.h"

#include "phoenix_view.h"

#include "../data/symbol_instance.h"
#include "../edit/command_stack.h"
#include "../edit/property_command.h"

#include <QKeyEvent>
#include <QMouseEvent>

#include <algorithm>
#include <cmath>

AssetWarpTool::AssetWarpTool(fla::CommandStack& commands, std::function<void(const QString&)> status,
    std::function<fla::SymbolInstance*(fla::Element*, fla::Frame*)> makePuppet)
    : _commands(commands)
    , _status(std::move(status))
    , _makePuppet(std::move(makePuppet))
{
}

std::vector<fla::WarpPin> AssetWarpTool::pinsOf() const
{
    if (!_puppet)
        return {};
    for (const auto& attribute : _puppet->extraAttributes)
    {
        if (attribute.first == "puppetHandles")
            return fla::AssetWarp::pins(attribute.second);
    }
    return {};
}

int AssetWarpTool::pinAt(PhoenixView& view, const QPointF& documentPos) const
{
    const std::vector<fla::WarpPin> pins = pinsOf();
    int best = -1;
    double reach = 8.0 * view.pickTolerance();
    for (size_t i = 0; i < pins.size(); ++i)
    {
        const QPointF at = _toDocument.map(QPointF(pins[i].to.x, pins[i].to.y));
        const double distance = std::hypot(at.x() - documentPos.x(), at.y() - documentPos.y());
        if (distance <= reach)
        {
            reach = distance;
            best = static_cast<int>(i);
        }
    }
    return best;
}

void AssetWarpTool::apply(PhoenixView& view, const std::vector<fla::WarpPin>& pins)
{
    if (!_puppet)
        return;
    std::string current;
    for (const auto& attribute : _puppet->extraAttributes)
    {
        if (attribute.first == "puppetHandles")
            current = attribute.second;
    }
    const std::string written = fla::AssetWarp::withPins(current, pins);
    bool found = false;
    for (auto& attribute : _puppet->extraAttributes)
    {
        if (attribute.first == "puppetHandles")
        {
            attribute.second = written;
            found = true;
        }
    }
    if (!found)
        _puppet->extraAttributes.emplace_back("puppetHandles", written);
    view.clearCaches();
    view.update();
}

void AssetWarpTool::commit(PhoenixView& view, const Attributes& before, const char* name)
{
    if (!_puppet || before == _puppet->extraAttributes)
        return;
    const Attributes after = _puppet->extraAttributes;
    _commands.push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(_puppet->extraAttributes, before, after, name)));
    _commands.breakMergeChain();
    view.clearCaches();
    view.update();
}

bool AssetWarpTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;
    // Another puppet under the cursor becomes the one warped; the one
    // before may be on a frame no longer shown.
    const HitResult hit = view.hitTest(documentPos, view.pickTolerance());
    fla::SymbolInstance* instance = hit && hit.element->elementType() == fla::Element::Type::SymbolInstance
        ? static_cast<fla::SymbolInstance*>(hit.element) : nullptr;
    if (instance && instance->otherSymbolType == "puppet" && instance != _puppet)
    {
        _puppet = instance;
        _toDocument = hit.elementToDocument;
        _selected = -1;
    }

    // A pin of the puppet being warped.
    const int pin = _puppet ? pinAt(view, documentPos) : -1;
    if (pin >= 0)
    {
        _selected = pin;
        _before = _puppet->extraAttributes;
        if (event->modifiers() & Qt::AltModifier)
        {
            std::vector<fla::WarpPin> pins = pinsOf();
            pins.erase(pins.begin() + pin);
            apply(view, pins);
            commit(view, _before, "Remove Pin");
            _selected = -1;
            return true;
        }
        _dragging = true;
        view.update();
        return true;
    }

    // Art that is not a puppet yet becomes one, and the pin goes on it.
    if (hit && (!instance || instance->otherSymbolType != "puppet"))
    {
        const bool puppetable = !instance || instance->symbolType == fla::SymbolType::Graphic;
        fla::SymbolInstance* made = puppetable && _makePuppet ? _makePuppet(hit.element, hit.frame) : nullptr;
        if (!made)
        {
            if (_status)
                _status(instance ? "A movie clip or button cannot be warped: pins go on shapes, bitmaps, groups "
                                   "and graphics."
                                 : "This art cannot be made into a puppet.");
            _puppet = nullptr;
            _selected = -1;
            view.update();
            return true;
        }
        const HitResult again = view.hitTest(documentPos, view.pickTolerance());
        instance = again && again.element == made ? made : nullptr;
        if (!instance)
        {
            // Under the cursor no longer (a stroke picked at its edge, say):
            // the puppet is made, pins wait for the next click.
            _puppet = made;
            _selected = -1;
            view.update();
            return true;
        }
        _puppet = nullptr;
        if (_status)
            _status("Made a puppet. Click to add pins, drag a pin to bend the art around the others.");
        return mousePress(view, event, documentPos);
    }
    if (!instance)
    {
        _puppet = nullptr;
        _selected = -1;
        view.update();
        return false;
    }
    if (instance != _puppet)
    {
        _puppet = instance;
        _selected = -1;
    }
    _toDocument = hit.elementToDocument;
    bool invertible = false;
    const QPointF local = _toDocument.inverted(&invertible).map(documentPos);
    if (!invertible)
        return true;

    // Where the art is shown there is warped; the pin goes on the art as it
    // was, at the point the warp brings under the cursor.
    std::vector<fla::WarpPin> pins = pinsOf();
    fla::Point from(local.x(), local.y());
    for (int step = 0; step < 20 && !pins.empty(); ++step)
    {
        const fla::Point shown = fla::AssetWarp::warp(pins, from);
        from = fla::Point(from.x + (local.x() - shown.x) * 0.8, from.y + (local.y() - shown.y) * 0.8);
    }
    fla::WarpPin added;
    for (const fla::WarpPin& existing : pins)
        added.id = std::max(added.id, existing.id + 1);
    added.from = from;
    added.to = fla::Point(local.x(), local.y());
    _before = _puppet->extraAttributes;
    pins.push_back(added);
    apply(view, pins);
    commit(view, _before, "Add Pin");
    _selected = static_cast<int>(pins.size()) - 1;
    return true;
}

bool AssetWarpTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)event;
    if (!_dragging || !_puppet || _selected < 0)
        return false;
    std::vector<fla::WarpPin> pins = pinsOf();
    if (_selected >= static_cast<int>(pins.size()))
        return true;
    bool invertible = false;
    const QPointF local = _toDocument.inverted(&invertible).map(documentPos);
    if (!invertible)
        return true;
    pins[static_cast<size_t>(_selected)].to = fla::Point(local.x(), local.y());
    apply(view, pins);
    return true;
}

bool AssetWarpTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)event;
    (void)documentPos;
    if (!_dragging)
        return false;
    _dragging = false;
    commit(view, _before, "Move Pin");
    return true;
}

bool AssetWarpTool::keyPress(PhoenixView& view, QKeyEvent* event)
{
    if (!_puppet || _selected < 0 || (event->key() != Qt::Key_Delete && event->key() != Qt::Key_Backspace))
        return false;
    std::vector<fla::WarpPin> pins = pinsOf();
    if (_selected >= static_cast<int>(pins.size()))
        return false;
    _before = _puppet->extraAttributes;
    pins.erase(pins.begin() + _selected);
    apply(view, pins);
    commit(view, _before, "Remove Pin");
    _selected = -1;
    return true;
}

void AssetWarpTool::paintOverlay(PhoenixView& view, QPainter& painter, double scale)
{
    (void)view;
    if (!_puppet)
        return;
    const std::vector<fla::WarpPin> pins = pinsOf();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (size_t i = 0; i < pins.size(); ++i)
    {
        const QPointF at = _toDocument.map(QPointF(pins[i].to.x, pins[i].to.y));
        const bool selected = static_cast<int>(i) == _selected;
        painter.setPen(QPen(QColor(20, 20, 20), 1.5 * scale));
        painter.setBrush(selected ? QColor(255, 200, 0) : QColor(255, 255, 255));
        painter.drawEllipse(at, 5.0 * scale, 5.0 * scale);
        painter.setBrush(QColor(20, 20, 20));
        painter.drawEllipse(at, 1.5 * scale, 1.5 * scale);
    }
    painter.restore();
}

void AssetWarpTool::deactivate(PhoenixView& view)
{
    if (_dragging)
        commit(view, _before, "Move Pin");
    _dragging = false;
    _puppet = nullptr;
    _selected = -1;
    view.update();
}
