#include "xsheet_panel.h"

#include "../data/document.h"
#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/shape.h"
#include "../data/edge.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/timeline.h"
#include "../edit/command_stack.h"
#include "../edit/element_clone.h"
#include "../edit/element_commands.h"
#include "../edit/property_command.h"
#include "../edit/timeline_edit.h"

#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kFrameColumn = 44;
constexpr int kColumnWidth = 64;
constexpr int kHeader = 22;
constexpr int kRow = 18;

const fla::SymbolInstance* graphicOf(const fla::Frame& frame)
{
    if (frame.elements.size() != 1)
        return nullptr;
    const auto* instance = dynamic_cast<const fla::SymbolInstance*>(frame.elements.front());
    return instance && instance->symbolType == fla::SymbolType::Graphic ? instance : nullptr;
}

fla::Frame* keyAt(fla::Layer& layer, int index)
{
    for (fla::Frame* frame : layer.frames)
        if (frame && frame->index == index)
            return frame;
    return nullptr;
}

/// What a keyframe's art is, so that keyframes showing the same drawing
/// (one exposed again) are known as one: each element's kind, place and
/// geometry or symbol.
std::string signatureOf(const fla::Frame& frame)
{
    std::string signature;
    for (const fla::Element* element : frame.elements)
    {
        if (!element)
            continue;
        const fla::Transform& t = element->transform;
        signature += std::to_string(static_cast<int>(element->elementType())) + ":" + std::to_string(t.m11) + "," +
                     std::to_string(t.m12) + "," + std::to_string(t.m21) + "," + std::to_string(t.m22) + "," +
                     std::to_string(t.tx) + "," + std::to_string(t.ty) + ";";
        if (const auto* shape = dynamic_cast<const fla::Shape*>(element))
        {
            // The geometry as it is now, not as it was read.
            std::size_t hash = shape->edges.size();
            for (const fla::Edge* edge : shape->edges)
                for (const fla::Path* path : edge ? edge->paths : std::vector<fla::Path*>())
                    for (const fla::PathSegment* segment : path ? path->segments : std::vector<fla::PathSegment*>())
                        for (const fla::Point& point : segment ? segment->points : std::vector<fla::Point>())
                            hash = (hash * 1000003u) ^ static_cast<std::size_t>(std::llround(point.x * 20.0) * 73856093 ^
                                                                                std::llround(point.y * 20.0) * 19349663);
            signature += std::to_string(hash) + ";";
        }
        else if (const auto* instance = dynamic_cast<const fla::SymbolInstance*>(element))
        {
            signature += instance->libraryItemName + "@" + std::to_string(instance->firstFrame) + ";";
        }
    }
    return signature;
}

/// A layer's drawings: the first keyframe showing each different piece of
/// drawn art, in order. Drawing n is the n-th of them.
std::vector<const fla::Frame*> drawingsOf(const fla::Layer& layer)
{
    std::vector<const fla::Frame*> keys;
    for (const fla::Frame* frame : layer.frames)
        if (frame && !frame->elements.empty() && !graphicOf(*frame))
            keys.push_back(frame);
    std::sort(keys.begin(), keys.end(), [](const fla::Frame* a, const fla::Frame* b) { return a->index < b->index; });
    std::vector<const fla::Frame*> drawings;
    std::vector<std::string> seen;
    for (const fla::Frame* key : keys)
    {
        const std::string signature = signatureOf(*key);
        if (std::find(seen.begin(), seen.end(), signature) == seen.end())
        {
            seen.push_back(signature);
            drawings.push_back(key);
        }
    }
    return drawings;
}

} // namespace

XSheetGrid::XSheetGrid(XSheetLinks links, QWidget* parent)
    : QWidget(parent)
    , _links(std::move(links))
{
    setFocusPolicy(Qt::StrongFocus);
    setToolTip("Type a drawing's number and Enter to expose it here; x for none; Delete clears the keyframe");
}

std::vector<fla::Layer*> XSheetGrid::columns() const
{
    std::vector<fla::Layer*> layers;
    fla::Timeline* timeline = _links.timeline ? _links.timeline() : nullptr;
    for (fla::Layer* layer : timeline ? timeline->layers : std::vector<fla::Layer*>())
        if (layer && layer->layerType != fla::Layer::Type::Folder)
            layers.push_back(layer);
    return layers;
}

int XSheetGrid::frameCount() const
{
    int count = 1;
    for (const fla::Layer* layer : columns())
        count = std::max(count, fla::timeline::layerEnd(*layer));
    // Room below to carry on typing.
    return count + 12;
}

QSize XSheetGrid::sizeHint() const
{
    return QSize(kFrameColumn + kColumnWidth * std::max<int>(1, static_cast<int>(columns().size())),
        kHeader + kRow * frameCount());
}

void XSheetGrid::refresh()
{
    resize(sizeHint());
    setMinimumSize(sizeHint());
    update();
}

QString XSheetGrid::drawingOf(const fla::Layer& layer, const fla::Frame& key) const
{
    if (key.elements.empty())
        return "x";
    if (const fla::SymbolInstance* graphic = graphicOf(key))
        return QString::number(graphic->firstFrame + 1);
    if (key.elements.size() == 1)
    {
        if (const auto* instance = dynamic_cast<const fla::SymbolInstance*>(key.elements.front()))
        {
            const std::string& name = instance->libraryItemName;
            return QString::fromStdString(name.substr(name.find_last_of('/') == std::string::npos ? 0
                                                                                                     : name.find_last_of('/') + 1));
        }
    }
    const std::vector<const fla::Frame*> drawings = drawingsOf(layer);
    const std::string signature = signatureOf(key);
    for (size_t i = 0; i < drawings.size(); ++i)
        if (signatureOf(*drawings[i]) == signature)
            return QString::number(i + 1);
    return QString("?");
}

XSheetGrid::Cell XSheetGrid::cellAt(const fla::Layer& layer, int frame) const
{
    Cell cell;
    const fla::Frame* span = fla::timeline::spanAt(const_cast<fla::Layer&>(layer), frame);
    if (!span)
        return cell;
    cell.tweened = span->tweenType != fla::TweenType::None || !span->otherTweenType.empty();
    if (span->index == frame)
    {
        cell.key = true;
        cell.text = drawingOf(layer, *span);
    }
    else
    {
        cell.held = true;
    }
    return cell;
}

void XSheetGrid::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    const QPalette& pal = palette();
    painter.fillRect(rect(), pal.color(QPalette::Base));
    const std::vector<fla::Layer*> layers = columns();
    const int rows = frameCount();
    const int current = _links.currentFrame ? _links.currentFrame() : 0;
    const QRect clip = event->rect();
    const int firstRow = std::max(0, (clip.top() - kHeader) / kRow);
    const int lastRow = std::min(rows - 1, (clip.bottom() - kHeader) / kRow + 1);

    for (int row = firstRow; row <= lastRow; ++row)
    {
        const int y = kHeader + row * kRow;
        // Every eighth frame and seconds marked, as on a paper sheet.
        if (row == current)
            painter.fillRect(QRect(0, y, width(), kRow), QColor(230, 60, 60, 60));
        else if (row % 2 == 1)
            painter.fillRect(QRect(0, y, width(), kRow), pal.color(QPalette::AlternateBase));
        painter.setPen(pal.color(QPalette::PlaceholderText));
        painter.drawText(QRect(0, y, kFrameColumn - 6, kRow), Qt::AlignRight | Qt::AlignVCenter, QString::number(row + 1));
        for (size_t c = 0; c < layers.size(); ++c)
        {
            const QRect box(kFrameColumn + static_cast<int>(c) * kColumnWidth, y, kColumnWidth, kRow);
            const Cell cell = cellAt(*layers[c], row);
            if (cell.key)
            {
                painter.setPen(pal.color(QPalette::Text));
                QFont bold = font();
                bold.setBold(true);
                painter.setFont(bold);
                painter.drawText(box, Qt::AlignCenter, cell.text);
                painter.setFont(font());
            }
            else if (cell.held)
            {
                // The exposure holds: a line down the cell.
                painter.setPen(QPen(cell.tweened ? pal.color(QPalette::Highlight) : pal.color(QPalette::Text), 1.5));
                painter.drawLine(box.center().x(), box.top(), box.center().x(), box.bottom());
            }
            if (static_cast<int>(c) == _column && row == _row)
            {
                painter.setPen(QPen(pal.color(QPalette::Highlight), 2.0));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(box.adjusted(1, 1, -1, -1));
                if (!_typed.isEmpty())
                {
                    painter.fillRect(box.adjusted(2, 2, -2, -2), pal.color(QPalette::Highlight));
                    painter.setPen(pal.color(QPalette::HighlightedText));
                    painter.drawText(box, Qt::AlignCenter, _typed);
                }
            }
        }
        // Seconds and their halves, as lines across.
        painter.setPen(QPen(pal.color(QPalette::Mid), (row + 1) % 24 == 0 ? 1.5 : 0.5));
        painter.drawLine(0, y + kRow - 1, width(), y + kRow - 1);
    }
    for (size_t c = 0; c <= layers.size(); ++c)
    {
        const int x = kFrameColumn + static_cast<int>(c) * kColumnWidth;
        painter.setPen(QPen(pal.color(QPalette::Mid), 1.0));
        painter.drawLine(x, 0, x, height());
    }
    // The column heads: the layers.
    painter.fillRect(QRect(0, 0, width(), kHeader), pal.color(QPalette::Button));
    painter.setPen(pal.color(QPalette::ButtonText));
    painter.drawText(QRect(0, 0, kFrameColumn - 6, kHeader), Qt::AlignRight | Qt::AlignVCenter, "Frame");
    for (size_t c = 0; c < layers.size(); ++c)
    {
        const QRect head(kFrameColumn + static_cast<int>(c) * kColumnWidth + 3, 0, kColumnWidth - 6, kHeader);
        painter.drawText(head, Qt::AlignCenter,
            painter.fontMetrics().elidedText(QString::fromStdString(layers[c]->name), Qt::ElideRight, head.width()));
    }
}

void XSheetGrid::mousePressEvent(QMouseEvent* event)
{
    const QPoint at = event->position().toPoint();
    const std::vector<fla::Layer*> layers = columns();
    if (at.y() < kHeader || layers.empty())
        return;
    _row = std::max(0, (at.y() - kHeader) / kRow);
    _column = std::clamp((at.x() - kFrameColumn) / kColumnWidth, 0, static_cast<int>(layers.size()) - 1);
    _typed.clear();
    setFocus();
    if (_links.setCurrentFrame)
        _links.setCurrentFrame(_row);
    if (_links.setCurrentLayer)
        _links.setCurrentLayer(layers[static_cast<size_t>(_column)]);
    update();
}

void XSheetGrid::keyPressEvent(QKeyEvent* event)
{
    const std::vector<fla::Layer*> layers = columns();
    if (layers.empty())
        return;
    const QString text = event->text();
    const auto move = [this, &layers](int rows, int cols) {
        _row = std::max(0, _row + rows);
        _column = std::clamp(_column + cols, 0, static_cast<int>(layers.size()) - 1);
        if (_links.setCurrentFrame)
            _links.setCurrentFrame(_row);
        update();
    };
    if (!text.isEmpty() && text.at(0).isDigit())
    {
        _typed += text.at(0);
        update();
        return;
    }
    switch (event->key())
    {
    case Qt::Key_Backspace:
        _typed.chop(1);
        update();
        return;
    case Qt::Key_Escape:
        _typed.clear();
        update();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (!_typed.isEmpty())
            expose(_typed.toInt());
        _typed.clear();
        move(1, 0);
        return;
    case Qt::Key_X:
        _typed.clear();
        expose(0);
        move(1, 0);
        return;
    case Qt::Key_Delete:
        _typed.clear();
        clearCell();
        return;
    case Qt::Key_Up:
        move(-1, 0);
        return;
    case Qt::Key_Down:
        move(1, 0);
        return;
    case Qt::Key_Left:
        move(0, -1);
        return;
    case Qt::Key_Right:
        move(0, 1);
        return;
    default:
        QWidget::keyPressEvent(event);
    }
}

void XSheetGrid::expose(int number)
{
    const std::vector<fla::Layer*> layers = columns();
    fla::CommandStack* stack = _links.commands ? _links.commands() : nullptr;
    if (!stack || _column >= static_cast<int>(layers.size()))
        return;
    fla::Layer* layer = layers[static_cast<size_t>(_column)];
    if (layer->isLocked())
        return;
    const int at = _row;
    const fla::Document* document = layer->findAncestorOfType<fla::Document>();
    const fla::SymbolList* library = document ? document->symbolList : nullptr;

    // What the layer exposes: a graphic symbol's frames, or its drawings.
    // The nearest keyframe at or before the cell with a graphic symbol.
    const fla::SymbolInstance* graphic = nullptr;
    int graphicAt = -1;
    for (const fla::Frame* frame : layer->frames)
    {
        if (frame && frame->index <= at && frame->index > graphicAt && graphicOf(*frame))
        {
            graphic = graphicOf(*frame);
            graphicAt = frame->index;
        }
    }
    const std::vector<const fla::Frame*> drawings = drawingsOf(*layer);
    const fla::Frame* drawing = number >= 1 && number <= static_cast<int>(drawings.size()) && !graphic
        ? drawings[static_cast<size_t>(number - 1)] : nullptr;
    if (number > 0 && !graphic && !drawing)
        return;

    stack->beginMacro(number == 0 ? "Expose Blank" : "Expose Drawing");
    // A keyframe of its own here: blank for no drawing or another drawing,
    // a copy of what shows for a graphic.
    const bool keepContent = graphic && number > 0;
    stack->push(fla::CommandPtr(new fla::EditFramesCommand({layer}, "Expose",
        [layer, at, keepContent](std::vector<fla::Frame*>&) {
            fla::Frame* existing = keyAt(*layer, at);
            if (!existing)
                fla::timeline::insertKeyframe(*layer, at, !keepContent);
        })));
    fla::Frame* key = keyAt(*layer, at);
    if (key && number > 0)
    {
        if (graphic)
        {
            if (key->elements.empty())
            {
                // A blank keyframe takes the symbol again.
                if (fla::Element* copy = fla::cloneElement(*graphic, key))
                {
                    fla::relinkSymbols(*copy, library);
                    stack->push(fla::CommandPtr(new fla::AddElementCommand(key, copy, "Expose Drawing")));
                }
            }
            if (auto* instance = key->elements.size() == 1 ? dynamic_cast<fla::SymbolInstance*>(key->elements.front()) : nullptr)
            {
                stack->push(fla::CommandPtr(new fla::SetPropertyCommand<int>(
                    instance->firstFrame, instance->firstFrame, number - 1, "Expose Drawing")));
                stack->push(fla::CommandPtr(new fla::SetPropertyCommand<fla::LoopType>(
                    instance->loopType, instance->loopType, fla::LoopType::SingleFrame, "Expose Drawing")));
            }
        }
        else if (drawing && drawing != key)
        {
            // The drawing's art, copied in place of what was here.
            const std::vector<fla::Element*> old = key->elements;
            for (fla::Element* element : old)
                stack->push(fla::CommandPtr(new fla::RemoveElementCommand(key, element, "Expose Drawing")));
            for (const fla::Element* element : drawing->elements)
            {
                if (fla::Element* copy = element ? fla::cloneElement(*element, key) : nullptr)
                {
                    fla::relinkSymbols(*copy, library);
                    stack->push(fla::CommandPtr(new fla::AddElementCommand(key, copy, "Expose Drawing")));
                }
            }
        }
    }
    else if (key && number == 0 && !key->elements.empty())
    {
        const std::vector<fla::Element*> old = key->elements;
        for (fla::Element* element : old)
            stack->push(fla::CommandPtr(new fla::RemoveElementCommand(key, element, "Expose Blank")));
    }
    stack->endMacro();
    stack->breakMergeChain();
    if (_links.changed)
        _links.changed();
    refresh();
}

void XSheetGrid::clearCell()
{
    const std::vector<fla::Layer*> layers = columns();
    fla::CommandStack* stack = _links.commands ? _links.commands() : nullptr;
    if (!stack || _column >= static_cast<int>(layers.size()))
        return;
    fla::Layer* layer = layers[static_cast<size_t>(_column)];
    const int at = _row;
    if (layer->isLocked() || !keyAt(*layer, at) || at == 0)
        return;
    stack->push(fla::CommandPtr(new fla::EditFramesCommand({layer}, "Clear Keyframe",
        [layer, at](std::vector<fla::Frame*>& removed) { fla::timeline::clearKeyframe(*layer, at, removed); })));
    stack->breakMergeChain();
    if (_links.changed)
        _links.changed();
    refresh();
}

// ---------------------------------------------------------------------------

XSheetPanel::XSheetPanel(XSheetLinks links, QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    _grid = new XSheetGrid(std::move(links), this);
    _scroll = new QScrollArea(this);
    _scroll->setWidget(_grid);
    _scroll->setWidgetResizable(false);
    layout->addWidget(_scroll);
    refresh();
}

void XSheetPanel::refresh()
{
    _grid->refresh();
}
