// The Artist layout (View > Mode > Artist): the stage with a strip of the
// drawing tools down its left, a button at its foot that opens the timeline
// (under a row of frame buttons) as a drawer over the foot of the stage,
// and a round button floating at the stage's right edge that opens
// Properties as a drawer there, each closed by a press anywhere else; every
// other panel and bar is put away, and comes back as it was. While the
// timeline is closed, a time strip floats over the foot of the stage: the
// current layer's frames alone, to scrub, step, play and add a frame
// without opening the timeline (a button on the frame row turns it off).
// It is the web editor's layout for a phone too: there the menu bar is put
// away into a menu button at the foot of the strip, with the Color,
// Swatches and Library panels as sheets, a press held on the stage opens
// the pop-up palette, and a turned stage has a button to set it straight.
// Animation is three ideas here: New
// Frame draws the next drawing
// (Duplicate keeps this one to work from), Longer and Shorter hold it, and
// moving something at a later frame keys that frame by itself, tweening the
// span before it, so a drawing dragged at frame 12 slides there from frame
// 1. A drawing that is not a symbol yet becomes one, named after its
// layer, so the tween can carry it. Motion names the tween's ease, or Jump
// for none. The edits themselves are in src/edit/artist_edit.h.

#include "main_window.h"
#include "canvas_only.h"
#include "color_well.h"
#include "phoenix_view.h"
#include "player.h"
#include "simple_workspace.h"
#include "stage_drawers.h"
#include "theme.h"
#include "timeline_view.h"
#include "timelines_panel.h"
#ifdef PHOENIX_WEB
#include "web_page.h"
#endif

#include "../data/document.h"
#include "../data/symbol_list.h"
#include "../edit/artist_edit.h"
#include "../edit/element_commands.h"
#include "../edit/frame_commands.h"
#include "../edit/timeline_edit.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QSettings>
#include <QSlider>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr int kButtonSize = 44;
/// The round button floating over the stage.
constexpr int kFloatSize = 48;

enum class ArtistIcon { Undo, Redo, Back, Play, Stop, Forward, Loop, Onion, NewFrame, Duplicate, Longer, Shorter, NewLayer,
    Timeline, Properties, Strip, Menu, Straighten };

/// The layout's own pictures, in the theme's text color.
QIcon artistIcon(ArtistIcon kind)
{
    const int size = 64;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor ink = theme::isDark() ? QColor(225, 225, 225) : QColor(40, 40, 40);
    painter.setPen(QPen(ink, 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    const auto triangle = [&](bool right, double x) {
        QPainterPath path;
        if (right)
        {
            path.moveTo(x, 16);
            path.lineTo(x + 24, 32);
            path.lineTo(x, 48);
        }
        else
        {
            path.moveTo(x + 24, 16);
            path.lineTo(x, 32);
            path.lineTo(x + 24, 48);
        }
        path.closeSubpath();
        painter.fillPath(path, ink);
    };
    const auto frame = [&](const QRectF& rect, bool filled) {
        if (filled)
            painter.fillRect(rect, ink);
        else
            painter.drawRect(rect);
    };
    switch (kind)
    {
    case ArtistIcon::Undo:
    case ArtistIcon::Redo:
    {
        const bool redo = kind == ArtistIcon::Redo;
        painter.save();
        if (redo)
        {
            painter.translate(size, 0);
            painter.scale(-1, 1);
        }
        QPainterPath arc;
        arc.moveTo(20, 24);
        arc.cubicTo(30, 14, 50, 18, 50, 34);
        arc.cubicTo(50, 46, 38, 50, 26, 48);
        painter.drawPath(arc);
        QPainterPath head;
        head.moveTo(10, 22);
        head.lineTo(26, 12);
        head.lineTo(26, 32);
        head.closeSubpath();
        painter.fillPath(head, ink);
        painter.restore();
        break;
    }
    case ArtistIcon::Back:
        painter.drawLine(QPointF(16, 16), QPointF(16, 48));
        triangle(false, 22);
        break;
    case ArtistIcon::Play:
        triangle(true, 22);
        break;
    case ArtistIcon::Stop:
        painter.fillRect(QRectF(18, 18, 28, 28), ink);
        break;
    case ArtistIcon::Forward:
        triangle(true, 18);
        painter.drawLine(QPointF(48, 16), QPointF(48, 48));
        break;
    case ArtistIcon::Loop:
    {
        QPainterPath loop;
        loop.moveTo(44, 22);
        loop.arcTo(QRectF(14, 14, 36, 36), 60, 300);
        painter.drawPath(loop);
        QPainterPath head;
        head.moveTo(46, 10);
        head.lineTo(54, 26);
        head.lineTo(36, 26);
        head.closeSubpath();
        painter.fillPath(head, ink);
        break;
    }
    case ArtistIcon::Onion:
        painter.setPen(QPen(theme::withAlpha(ink, 90), 5));
        painter.drawEllipse(QRectF(8, 18, 28, 28));
        painter.setPen(QPen(theme::withAlpha(ink, 160), 5));
        painter.drawEllipse(QRectF(18, 18, 28, 28));
        painter.setPen(QPen(ink, 5));
        painter.drawEllipse(QRectF(28, 18, 28, 28));
        break;
    case ArtistIcon::NewFrame:
        frame(QRectF(10, 16, 22, 32), false);
        painter.drawLine(QPointF(46, 22), QPointF(46, 42));
        painter.drawLine(QPointF(36, 32), QPointF(56, 32));
        break;
    case ArtistIcon::Duplicate:
        frame(QRectF(8, 16, 22, 32), true);
        frame(QRectF(34, 16, 22, 32), false);
        break;
    case ArtistIcon::Longer:
        frame(QRectF(8, 20, 34, 24), true);
        painter.drawLine(QPointF(48, 32), QPointF(58, 32));
        painter.drawLine(QPointF(53, 24), QPointF(58, 32));
        painter.drawLine(QPointF(53, 40), QPointF(58, 32));
        break;
    case ArtistIcon::Shorter:
        frame(QRectF(8, 20, 24, 24), true);
        painter.drawLine(QPointF(58, 32), QPointF(42, 32));
        painter.drawLine(QPointF(48, 24), QPointF(42, 32));
        painter.drawLine(QPointF(48, 40), QPointF(42, 32));
        break;
    case ArtistIcon::Timeline:
        // A layer's frames, a keyframe dot in the first and a span after it.
        painter.drawRect(QRectF(8, 20, 48, 24));
        for (double x : {20.0, 32.0, 44.0})
            painter.drawLine(QPointF(x, 20), QPointF(x, 44));
        painter.setBrush(ink);
        painter.drawEllipse(QPointF(14, 32), 3.5, 3.5);
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(QPointF(24, 54), QPointF(56, 54));
        break;
    case ArtistIcon::Properties:
        // Sliders.
        for (int row = 0; row < 3; ++row)
        {
            const double y = 16 + row * 16;
            painter.drawLine(QPointF(12, y), QPointF(52, y));
            painter.setBrush(ink);
            painter.drawEllipse(QPointF(row == 1 ? 40 : 24, y), 5.5, 5.5);
            painter.setBrush(Qt::NoBrush);
        }
        break;
    case ArtistIcon::Strip:
        // One layer's frames with the playhead over them.
        painter.drawRect(QRectF(8, 26, 48, 16));
        painter.setBrush(ink);
        painter.drawEllipse(QPointF(15, 34), 3.5, 3.5);
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(QPointF(36, 14), QPointF(36, 50));
        break;
    case ArtistIcon::Menu:
        for (double y : {20.0, 32.0, 44.0})
            painter.drawLine(QPointF(14, y), QPointF(50, y));
        break;
    case ArtistIcon::Straighten:
        // A turned frame and the upright one it goes back to.
        painter.save();
        painter.setPen(QPen(theme::withAlpha(ink, 110), 4));
        painter.translate(32, 32);
        painter.rotate(-18);
        painter.drawRect(QRectF(-16, -12, 32, 24));
        painter.restore();
        painter.drawRect(QRectF(16, 20, 32, 24));
        break;
    case ArtistIcon::NewLayer:
        painter.drawRect(QRectF(10, 30, 32, 22));
        painter.drawLine(QPointF(18, 30), QPointF(18, 22));
        painter.drawLine(QPointF(18, 22), QPointF(50, 22));
        painter.drawLine(QPointF(50, 22), QPointF(50, 44));
        painter.drawLine(QPointF(50, 44), QPointF(42, 44));
        break;
    }
    painter.end();
    return QIcon(pixmap);
}

/// The named eases as the Motion box offers them.
const std::vector<std::pair<const char*, const char*>>& eases()
{
    static const std::vector<std::pair<const char*, const char*>> list = {
        {"jump", "Jump"}, {"even", "Even"}, {"in", "Slow start"}, {"out", "Slow stop"}, {"both", "Slow both"}};
    return list;
}

/// Runs \a edit on \a layer as one undo step, as the timeline's frame
/// commands do; false, and no step, when it changed nothing.
bool editLayer(fla::CommandStack& stack, fla::Layer* layer, const char* name,
    const std::function<void(fla::Layer&, std::vector<fla::Frame*>& removed)>& edit)
{
    auto* command = new fla::EditFramesCommand({layer}, name,
        [layer, edit](std::vector<fla::Frame*>& removed) { edit(*layer, removed); });
    command->redo();
    if (!command->changedAnything())
    {
        command->undo();
        delete command;
        return false;
    }
    command->undo();
    stack.push(fla::CommandPtr(command));
    stack.breakMergeChain();
    return true;
}

/// The frame an element sits in, up through any groups; null off a frame.
fla::Frame* frameOf(fla::DOMElement* element)
{
    for (fla::DOMElement* parent = element ? element->parent : nullptr; parent; parent = parent->parent)
        if (parent->domType() == fla::DOMElement::DOMType::Frame)
            return static_cast<fla::Frame*>(parent);
    return nullptr;
}

fla::Layer* layerOf(fla::Frame* frame)
{
    if (frame && frame->parent && frame->parent->domType() == fla::DOMElement::DOMType::Layer)
        return static_cast<fla::Layer*>(frame->parent);
    return nullptr;
}

bool sameTransform(const fla::Transform& a, const fla::Transform& b)
{
    return a.m11 == b.m11 && a.m12 == b.m12 && a.m21 == b.m21 && a.m22 == b.m22 && a.tx == b.tx && a.ty == b.ty;
}

/// Makes  frame's drawing one symbol, named after its layer and turning
/// about its middle, so a tween can carry it; nothing when it already is
/// symbols, or holds nothing. The selection, when given, follows it.
bool symbolize(fla::Document* document, PhoenixView* view, fla::Selection* selection, fla::CommandStack& stack,
    const fla::Layer* layer, fla::Frame* frame)
{
    if (!document || !frame || frame->elements.empty() || fla::artist::tweenable(*frame))
        return false;
    QRectF bounds;
    for (fla::Element* element : frame->elements)
    {
        const QRectF each = view->elementBounds(element);
        if (each.isValid())
            bounds = bounds.isValid() ? bounds.united(each) : each;
    }
    const QPointF center = bounds.isValid() ? bounds.center() : QPointF();
    const std::string base = layer->name.empty() ? std::string("Drawing") : layer->name;
    std::string name = base;
    for (int n = 2; document->symbolList && document->symbolList->symbolMap.count(name) != 0; ++n)
        name = base + " " + std::to_string(n);
    stack.push(fla::CommandPtr(new fla::ConvertToSymbolCommand(document, frame, frame->elements, name, center.x(),
        center.y(), "Convert to Symbol", selection, fla::SymbolType::Graphic)));
    return true;
}

} // namespace

/// Watches the stage for the press that may begin a move and the release
/// that ends it, so a move at a frame inside a span can key the frame
/// first (and leave nothing behind when the press moved nothing).
class ArtistStage : public QObject
{
public:
    ArtistStage(QWidget* stage, std::function<void(const QPoint&)> pressed, std::function<void()> released)
        : QObject(stage)
        , _stage(stage)
        , _pressed(std::move(pressed))
        , _released(std::move(released))
    {
        stage->installEventFilter(this);
    }

    void setEnabled(bool on) { _on = on; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (_on && watched == _stage)
        {
            if (event->type() == QEvent::MouseButtonPress &&
                static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton)
            {
                _pressed(static_cast<QMouseEvent*>(event)->position().toPoint());
            }
            else if (event->type() == QEvent::MouseButtonRelease &&
                static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton)
            {
                // Once the stage, and its tool, have had the release.
                QTimer::singleShot(0, this, [this]() { _released(); });
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QWidget* _stage;
    std::function<void(const QPoint&)> _pressed;
    std::function<void()> _released;
    bool _on = false;
};

namespace {

/// One layer's frames in a row: spans, keyframe dots, tween arrows and the
/// playhead; a press or drag scrubs. When the layer is longer than the row,
/// it scrolls to keep the playhead in view.
class TimeCells : public QWidget
{
public:
    static constexpr int kCellWidth = 14;
    static constexpr int kCellRow = 22;
    static constexpr int kNumberRow = 12;

    TimeCells(Player* player, std::function<const fla::Layer*()> layer, QWidget* parent)
        : QWidget(parent)
        , _player(player)
        , _layer(std::move(layer))
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(kCellRow + kNumberRow);
        setCursor(Qt::PointingHandCursor);
    }

    /// The frames worth showing: the layer's, with room after its end.
    int framesWanted() const
    {
        const fla::Layer* layer = _layer();
        const int end = layer ? fla::timeline::layerEnd(*layer) : 0;
        return std::max({24, end + 4, (_player ? _player->currentFrame() : 0) + 2});
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        const QPalette pal = palette();
        const int shown = std::max(1, width() / kCellWidth);
        const int current = _player ? _player->currentFrame() : 0;
        const int total = framesWanted();
        _first = total > shown ? std::clamp(current - shown / 2, 0, total - shown) : 0;
        const int top = kNumberRow;
        painter.fillRect(QRect(0, top, width(), kCellRow), pal.color(QPalette::Base));
        QFont small = font();
        small.setPointSizeF(std::max(6.0, font().pointSizeF() - 2.0));
        painter.setFont(small);
        // A tick and a number every five frames.
        for (int frame = _first; frame < _first + shown; ++frame)
        {
            if ((frame + 1) % 5 != 0 && frame != 0)
                continue;
            const int x = (frame - _first) * kCellWidth;
            painter.setPen(pal.color(QPalette::Mid));
            painter.drawLine(x, top, x, top + kCellRow);
            painter.setPen(pal.color(QPalette::Text));
            painter.drawText(QRect(x + 2, 0, kCellWidth * 4, kNumberRow), Qt::AlignLeft | Qt::AlignVCenter,
                QString::number(frame + 1));
        }
        // The layer's spans: a dot at each keyframe (hollow when it holds
        // nothing), an arrow along a tween.
        const QColor ink = pal.color(QPalette::Text);
        if (const fla::Layer* layer = _layer())
        {
            for (const fla::Frame* frame : layer->frames)
            {
                if (!frame)
                    continue;
                const int last = frame->index + std::max(frame->duration, 1) - 1;
                if (last < _first || frame->index >= _first + shown)
                    continue;
                const int x0 = (std::max(frame->index, _first) - _first) * kCellWidth;
                const int x1 = (std::min(last, _first + shown - 1) - _first + 1) * kCellWidth;
                const bool tween = frame->tweenType != fla::TweenType::None;
                // The fills the timeline's own grid uses: plain, tween, blank.
                QColor fill = tween ? QColor(150, 130, 200) : (theme::isDark() ? QColor(120, 120, 120) : QColor(200, 200, 200));
                if (frame->elements.empty() && !tween)
                    fill = theme::isDark() ? QColor(64, 64, 64) : QColor(235, 235, 235);
                painter.fillRect(QRect(x0, top + 2, std::max(1, x1 - x0 - 1), kCellRow - 4), fill);
                painter.setPen(QPen(ink, 1));
                if (tween && x1 - x0 > kCellWidth)
                {
                    const int y = top + kCellRow / 2;
                    painter.drawLine(x0 + kCellWidth / 2 + 4, y, x1 - 4, y);
                    painter.drawLine(x1 - 8, y - 3, x1 - 4, y);
                    painter.drawLine(x1 - 8, y + 3, x1 - 4, y);
                }
                if (frame->index >= _first)
                {
                    painter.setRenderHint(QPainter::Antialiasing);
                    painter.setBrush(frame->elements.empty() ? QBrush(Qt::NoBrush) : QBrush(ink));
                    painter.drawEllipse(QPointF(x0 + kCellWidth / 2.0, top + kCellRow / 2.0), 3.0, 3.0);
                    painter.setBrush(Qt::NoBrush);
                    painter.setRenderHint(QPainter::Antialiasing, false);
                }
            }
        }
        if (current >= _first && current < _first + shown)
        {
            const int x = (current - _first) * kCellWidth;
            painter.fillRect(QRect(x, 0, kCellWidth, height()), theme::withAlpha(pal.color(QPalette::Highlight), 70));
            painter.fillRect(QRect(x + kCellWidth / 2 - 1, 0, 2, height()), pal.color(QPalette::Highlight));
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton)
            scrub(event->position().x());
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (event->buttons() & Qt::LeftButton)
            scrub(event->position().x());
    }

private:
    void scrub(double x)
    {
        if (!_player)
            return;
        const int frame = std::max(0, _first + static_cast<int>(x) / kCellWidth);
        if (frame != _player->currentFrame())
            _player->setCurrentFrame(frame);
    }

    Player* _player;
    std::function<const fla::Layer*()> _layer;
    int _first = 0;
};

} // namespace

/// The time strip floating over the foot of the stage while the timeline
/// is closed: step, play and step buttons, the current layer's frames, the
/// frame number and New Frame. A window of its own, over the stage's
/// canvas.
class ArtistTimeStrip : public QWidget
{
public:
    struct Actions
    {
        std::function<void()> stepBack;
        std::function<void()> playStop;
        std::function<void()> stepForward;
        std::function<void()> newFrame;
    };

    ArtistTimeStrip(QWidget* window, Player* player, std::function<const fla::Layer*()> layer, Actions actions)
        : QWidget(window, Qt::Tool | Qt::FramelessWindowHint)
        , _player(player)
    {
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAutoFillBackground(true);
        setObjectName("artistTimeStrip");
        QHBoxLayout* row = new QHBoxLayout(this);
        row->setContentsMargins(6, 4, 8, 4);
        row->setSpacing(2);
        const auto button = [this, row](ArtistIcon icon, const char* tip, std::function<void()> go) {
            QToolButton* made = new QToolButton(this);
            made->setAutoRaise(true);
            made->setIcon(artistIcon(icon));
            made->setIconSize(QSize(18, 18));
            made->setFixedSize(28, 28);
            made->setToolTip(tip);
            connect(made, &QToolButton::clicked, this, [go]() {
                if (go)
                    go();
            });
            row->addWidget(made);
            return made;
        };
        button(ArtistIcon::Back, "Step Back", actions.stepBack);
        _play = button(ArtistIcon::Play, "Play (Enter)", actions.playStop);
        button(ArtistIcon::Forward, "Step Forward", actions.stepForward);
        _cells = new TimeCells(player, std::move(layer), this);
        row->addWidget(_cells, 1);
        _frame = new QLabel(this);
        _frame->setMinimumWidth(28);
        _frame->setAlignment(Qt::AlignCenter);
        row->addWidget(_frame);
        button(ArtistIcon::NewFrame, "New Frame: a blank frame after this drawing, to draw the next one on", actions.newFrame);
        refresh();
    }

    void setPlaying(bool playing)
    {
        _play->setIcon(artistIcon(playing ? ArtistIcon::Stop : ArtistIcon::Play));
        _play->setToolTip(playing ? "Stop (Enter)" : "Play (Enter)");
    }

    void refresh()
    {
        _frame->setText(QString::number((_player ? _player->currentFrame() : 0) + 1));
        _cells->update();
    }

    /// Wide enough for every frame worth showing.
    int preferredWidth() const { return 14 + 4 * 30 + 30 + _cells->framesWanted() * TimeCells::kCellWidth; }

private:
    Player* _player;
    QToolButton* _play = nullptr;
    TimeCells* _cells = nullptr;
    QLabel* _frame = nullptr;
};

/// A move under way at a frame the layout keyed for it: what was there,
/// what the keyframe holds, and how it stood before the drag, to tell a
/// move from a click.
struct MainWindow::ArtistMove
{
    struct Keyed
    {
        fla::Layer* layer = nullptr;
        /// The span the frame lay in, and its elements as they were before
        /// any of them became a symbol.
        fla::Frame* span = nullptr;
        std::vector<fla::Element*> before;
        bool converted = false;
        fla::Frame* key = nullptr;
        std::vector<fla::Transform> poses;
    };
    std::vector<Keyed> layers;
    const fla::FLADocument* document = nullptr;
    const fla::Command* topBefore = nullptr;
};

void MainWindow::setArtistLayout(bool on)
{
    if (on == _artistLayout || _secondary)
        return;
    if (on)
    {
        if (_canvasOnly && _canvasOnly->isOn())
            _canvasOnly->leave();
        // The panels as they are now, to come back as they were.
        _artistSavedWorkspace = currentWorkspace();
        _artistLayout = true;
        if (_artistBars.isEmpty())
            buildArtistBars();
        for (QDockWidget* dock : findChildren<QDockWidget*>(Qt::FindDirectChildrenOnly))
            dock->hide();
        for (QToolBar* bar : findChildren<QToolBar*>(Qt::FindDirectChildrenOnly))
            if (!_artistBars.contains(bar))
                bar->hide();
        if (_breadcrumbRow)
            _breadcrumbRow->hide();
        statusBar()->hide();
        // On a phone the menu bar is too wide: its menus go in the strip's
        // menu button, and the stage's least size comes down so the page
        // holds the strip and the stage (web_page.cpp fits the window to it
        // again once that comes down).
#ifdef PHOENIX_WEB
        _artistCompact = web_page::isPhoneSized();
#endif
        if (_artistCompact)
        {
            menuBar()->hide();
            _phoenixView->setMinimumSize(100, 100);
            _artistFitPending = true;
            _phoenixView->zoomToFit();
        }
        if (_artistMenuAction)
            _artistMenuAction->setVisible(_artistCompact);
        // The timeline moves under the frame row, with rows and frames
        // large enough for a finger; the drawers then take that and
        // Properties, closed until their buttons open them.
        _timelineDock->setWidget(nullptr);
        static_cast<QVBoxLayout*>(_artistDock->widget()->layout())->addWidget(_timelineView);
        _timelineView->show();
        _timelineView->setPhoneView(true);
        _timelineView->setLayersWidth(_artistCompact ? 150 : 220);
        for (QToolBar* bar : _artistBars)
            bar->show();
        _artistDrawers->setActive(true);
        placeArtistProperties();
        placeArtistStrip();
        placeArtistStraighten();
        _artistStage->setEnabled(true);
        _phoenixView->setForgivingStage(true);
        // A finger has no right button: a press held on the stage opens the
        // pop-up palette.
        bool touch = false;
#ifdef PHOENIX_WEB
        touch = web_page::isPhoneSized() || web_page::isTouchTablet();
#endif
        _artistHold->setEnabled(touch);
        // A tool the strip has, the brush unless one is in hand.
        bool inStrip = false;
        for (const QAction* action : _artistBars.first()->actions())
            inStrip = inStrip || (action->isCheckable() && action->isChecked());
        if (!inStrip && _brushTool)
            chooseTool(_brushTool.get());
        refreshArtistContext();
    }
    else
    {
        _artistLayout = false;
        artistEndMove();
        _artistStage->setEnabled(false);
        _phoenixView->setForgivingStage(false);
        for (QToolBar* bar : _artistBars)
            bar->hide();
        // The panels back in their docks before the docks come back.
        _artistDrawers->setActive(false);
        _artistProperties->hide();
        _artistStrip->hide();
        _artistStraighten->hide();
        _artistHold->setEnabled(false);
        _artistSheet = nullptr;
        if (_artistCompact)
        {
            _phoenixView->setMinimumSize(400, 300);
            menuBar()->show();
            _artistCompact = false;
        }
        _timelineView->setPhoneView(false);
        static_cast<QVBoxLayout*>(_artistDock->widget()->layout())->removeWidget(_timelineView);
        _timelineDock->setWidget(_timelineView);
        if (!applyWorkspace(_artistSavedWorkspace))
            applyBuiltInWorkspace(simpleworkspace::fullName());
        if (_breadcrumbRow)
            _breadcrumbRow->show();
        statusBar()->show();
    }
    if (_artistLayoutAction && _artistLayoutAction->isChecked() != on)
        _artistLayoutAction->setChecked(on);
    if (QAction* classic = findChild<QAction*>("viewClassicMode"); classic && !on)
        classic->setChecked(true);
}

bool MainWindow::wantsArtistLayout()
{
    const QSettings settings;
    if (settings.contains("workspace/artist"))
        return settings.value("workspace/artist").toBool();
#ifdef PHOENIX_WEB
    // The phone layout's choice, from before it became this one; else a
    // phone-sized touch screen.
    if (settings.contains("web/phoneLayout"))
        return settings.value("web/phoneLayout").toBool();
    return web_page::isPhoneSized();
#else
    return false;
#endif
}

void MainWindow::buildArtistBars()
{
    // The tools down the left: the few a drawing needs, as the toolbox has
    // them, the stroke and fill colors, and undo and redo.
    QToolBar* tools = new QToolBar("Artist Tools", this);
    tools->setObjectName("artistTools");
    tools->setMovable(false);
    tools->setFloatable(false);
    tools->setIconSize(QSize(kButtonSize - 16, kButtonSize - 16));
    tools->setToolButtonStyle(Qt::ToolButtonIconOnly);
    tools->layout()->setContentsMargins(2, 4, 2, 4);
    _artistBars << tools;
    addToolBar(Qt::LeftToolBarArea, tools);
    for (Tool* wanted : std::initializer_list<Tool*>{_selectionTool.get(), _freeTransformTool.get(), _brushTool.get(),
             _pencilTool.get(), _eraserTool.get(), _paintBucketTool.get(), _handTool.get()})
    {
        for (const auto& [tool, action] : _toolActionList)
        {
            if (wanted && tool() == wanted)
            {
                tools->addAction(action);
                break;
            }
        }
    }
    tools->addSeparator();
    for (const bool stroke : {true, false})
    {
        QWidget* cell = new QWidget(tools);
        QHBoxLayout* centered = new QHBoxLayout(cell);
        centered->setContentsMargins(0, 0, 0, 0);
        ColorSwatch* swatch = new ColorSwatch(stroke, cell);
        swatch->setObjectName(stroke ? "artistStroke" : "artistFill");
        swatch->setFixedSize(30, 30);
        swatch->setToolTip(stroke ? "Stroke color" : "Fill color");
        centered->addWidget(swatch, 0, Qt::AlignCenter);
        connect(swatch, &ColorSwatch::clicked, this, [this, stroke, swatch]() { chooseStyleColor(stroke, swatch); });
        (stroke ? _artistStroke : _artistFill) = swatch;
        cell->setFixedSize(kButtonSize, kButtonSize);
        tools->addWidget(cell);
    }
    refreshStyleSwatches();
    tools->addSeparator();
    QAction* undo = tools->addAction(artistIcon(ArtistIcon::Undo), "Undo", _undoAction, &QAction::trigger);
    QAction* redo = tools->addAction(artistIcon(ArtistIcon::Redo), "Redo", _redoAction, &QAction::trigger);
    // Named, so a check (capture-window --steps) can press them.
    const auto name = [](QToolBar* bar, QAction* action, const char* name) {
        if (QWidget* button = bar->widgetForAction(action))
            button->setObjectName(name);
    };
    name(tools, undo, "artistUndo");
    name(tools, redo, "artistRedo");
    for (QToolButton* button : tools->findChildren<QToolButton*>())
        button->setMinimumSize(kButtonSize, kButtonSize);

    // The frame row and the timeline, in a panel of their own, which a
    // drawer shows over the foot of the stage.
    _artistDock = new QDockWidget("Animation", this);
    _artistDock->setObjectName("artistDock");
    _artistDock->setTitleBarWidget(new QWidget(_artistDock));
    _artistDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    _artistDock->setAllowedAreas(Qt::BottomDockWidgetArea);
    QWidget* body = new QWidget(_artistDock);
    QVBoxLayout* column = new QVBoxLayout(body);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    QToolBar* frames = new QToolBar("Artist Frames", body);
    frames->setObjectName("artistFrames");
    frames->setMovable(false);
    frames->setFloatable(false);
    frames->setIconSize(QSize(22, 22));
    frames->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    column->addWidget(frames);
    _artistDock->setWidget(body);
    addDockWidget(Qt::BottomDockWidgetArea, _artistDock);
    _artistDock->hide();

    QAction* newFrame = frames->addAction(artistIcon(ArtistIcon::NewFrame), "New Frame", this, [this]() { artistNewFrame(false); });
    newFrame->setToolTip("New Frame: a blank frame after this drawing, to draw the next one on");
    QAction* duplicate = frames->addAction(artistIcon(ArtistIcon::Duplicate), "Duplicate", this, [this]() { artistNewFrame(true); });
    duplicate->setToolTip("Duplicate: a copy of this drawing on a new frame after it, to work from");
    QAction* longer = frames->addAction(artistIcon(ArtistIcon::Longer), "Longer", this, [this]() { artistHold(1); });
    longer->setToolTip("Hold this drawing a frame longer");
    QAction* shorter = frames->addAction(artistIcon(ArtistIcon::Shorter), "Shorter", this, [this]() { artistHold(-1); });
    shorter->setToolTip("Hold this drawing a frame less");
    name(frames, newFrame, "artistNewFrame");
    name(frames, duplicate, "artistDuplicate");
    name(frames, longer, "artistLonger");
    name(frames, shorter, "artistShorter");
    frames->addSeparator();

    const auto timelines = [this]() { return _timelineView->timelines(); };
    QAction* back = frames->addAction(artistIcon(ArtistIcon::Back), "", this, [timelines]() {
        if (TimelinesPanel* panel = timelines())
            panel->stepBack();
    });
    back->setToolTip("Step Back");
    QAction* play = frames->addAction(artistIcon(ArtistIcon::Play), "", this, [timelines]() {
        if (TimelinesPanel* panel = timelines())
            panel->playStop();
    });
    play->setToolTip("Play (Enter)");
    connect(_player, &Player::playingChanged, play, [play](bool playing) {
        play->setIcon(artistIcon(playing ? ArtistIcon::Stop : ArtistIcon::Play));
        play->setToolTip(playing ? "Stop (Enter)" : "Play (Enter)");
    });
    QAction* forward = frames->addAction(artistIcon(ArtistIcon::Forward), "", this, [timelines]() {
        if (TimelinesPanel* panel = timelines())
            panel->stepForward();
    });
    forward->setToolTip("Step Forward");
    QLabel* frameLabel = new QLabel(frames);
    frameLabel->setMinimumWidth(40);
    frameLabel->setAlignment(Qt::AlignCenter);
    const auto showFrame = [frameLabel](int index) { frameLabel->setText(QString::number(index + 1)); };
    showFrame(_player->currentFrame());
    connect(_player, &Player::currentFrameChanged, frameLabel, showFrame);
    frames->addWidget(frameLabel);

    QAction* loop = frames->addAction(artistIcon(ArtistIcon::Loop), "");
    loop->setToolTip("Loop Playback");
    loop->setCheckable(true);
    if (TimelinesPanel* panel = timelines())
    {
        loop->setChecked(panel->loopPlayback());
        connect(loop, &QAction::toggled, panel, &TimelinesPanel::setLoopPlayback);
        connect(panel, &TimelinesPanel::loopPlaybackChanged, loop, &QAction::setChecked);
    }
    QAction* onion = frames->addAction(artistIcon(ArtistIcon::Onion), "");
    onion->setToolTip("Onion Skin: the drawings before and after this one, faintly");
    onion->setCheckable(true);
    onion->setChecked(_onionSkinAction->isChecked());
    connect(onion, &QAction::toggled, _onionSkinAction, &QAction::setChecked);
    connect(_onionSkinAction, &QAction::toggled, onion, &QAction::setChecked);

    // A slider through the frames, in the room left.
    QSlider* scrub = new QSlider(Qt::Horizontal, frames);
    scrub->setObjectName("artistTimeSlider");
    scrub->setToolTip("Time");
    scrub->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    scrub->setMinimumWidth(80);
    scrub->setPageStep(1);
    scrub->setStyleSheet("QSlider::groove:horizontal { height: 4px; border-radius: 2px; background: palette(mid); }"
                         "QSlider::handle:horizontal { width: 16px; margin: -6px 0; border-radius: 8px;"
                         " background: palette(highlight); }");
    const auto showTime = [this, scrub]() {
        const QSignalBlocker blocker(scrub);
        const TimelinesPanel* panel = _timelineView->timelines();
        const int count = panel ? panel->grid()->frameCount() : 1;
        scrub->setRange(0, std::max({1, count, _player->currentFrame() + 1}) - 1);
        scrub->setValue(_player->currentFrame());
    };
    showTime();
    connect(_player, &Player::currentFrameChanged, scrub, showTime);
    connect(_timelineView, &TimelineView::currentLayerChanged, scrub, showTime);
    connect(this, &MainWindow::editStateChanged, scrub, showTime);
    connect(scrub, &QSlider::valueChanged, this, [this](int frame) {
        if (frame != _player->currentFrame())
            _player->setCurrentFrame(frame);
    });
    frames->addWidget(scrub);
    frames->addSeparator();

    // How the drawing moves to the next one: the tween's ease, or none.
    _artistEaseBox = new QWidget(frames);
    QHBoxLayout* easeRow = new QHBoxLayout(_artistEaseBox);
    easeRow->setContentsMargins(6, 0, 6, 0);
    easeRow->setSpacing(6);
    easeRow->addWidget(new QLabel("Motion", _artistEaseBox));
    _artistEase = new QComboBox(_artistEaseBox);
    _artistEase->setObjectName("artistEase");
    _artistEase->setToolTip("How this drawing gets to the next one: it slides there (evenly, or easing), or jumps");
    for (const auto& [key, title] : eases())
        _artistEase->addItem(title, QString::fromLatin1(key));
    connect(_artistEase, &QComboBox::currentIndexChanged, this, [this](int index) {
        artistSetEase(_artistEase->itemData(index).toString());
    });
    easeRow->addWidget(_artistEase);
    frames->addWidget(_artistEaseBox);
    frames->addSeparator();
    QAction* newLayer = frames->addAction(artistIcon(ArtistIcon::NewLayer), "", this, [this]() { addLayer(false); });
    newLayer->setToolTip("New Layer");
    name(frames, newLayer, "artistNewLayer");
    frames->addSeparator();
    _artistStripAction = frames->addAction(artistIcon(ArtistIcon::Strip), "");
    _artistStripAction->setToolTip("Time Strip: this layer's frames, small, over the stage while the timeline is closed");
    _artistStripAction->setCheckable(true);
    _artistStripAction->setChecked(QSettings().value("artist/timeStrip", true).toBool());
    connect(_artistStripAction, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue("artist/timeStrip", on);
        placeArtistStrip();
    });
    name(frames, _artistStripAction, "artistStripToggle");

    connect(_player, &Player::currentFrameChanged, this, [this]() { refreshArtistContext(); });
    connect(_timelineView, &TimelineView::currentLayerChanged, this, [this]() { refreshArtistContext(); });
    connect(this, &MainWindow::editStateChanged, this, [this]() { refreshArtistContext(); });

    _artistStage = new ArtistStage(
        _phoenixView, [this](const QPoint& at) { artistBeginMove(at); }, [this]() { artistEndMove(); });

    // At the foot of the tool strip, the button that opens the timeline as
    // a drawer over the foot of the stage.
    QWidget* space = new QWidget(tools);
    space->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    tools->addWidget(space);
    tools->addSeparator();
    _artistDrawers = new StageDrawers(this, _phoenixView);
    QToolButton* timeline = new QToolButton(tools);
    timeline->setObjectName("artistTimelineButton");
    timeline->setCheckable(true);
    timeline->setIcon(artistIcon(ArtistIcon::Timeline));
    timeline->setIconSize(tools->iconSize());
    timeline->setToolTip("Timeline: the frames and layers, with New Frame, holds and Motion");
    timeline->setMinimumSize(kButtonSize, kButtonSize);
    tools->addWidget(timeline);
    _artistDrawers->add(_artistDock, Qt::BottomEdge, timeline, 300);

    // The strip's menu, shown while the menu bar is put away (a phone): the
    // other panels one at a time as a sheet, the menu bar's menus, and the
    // way back to Classic.
    QToolButton* menuButton = new QToolButton(tools);
    menuButton->setObjectName("artistMenuButton");
    menuButton->setIcon(artistIcon(ArtistIcon::Menu));
    menuButton->setIconSize(tools->iconSize());
    menuButton->setToolTip("Menu");
    menuButton->setMinimumSize(kButtonSize, kButtonSize);
    menuButton->setPopupMode(QToolButton::InstantPopup);
    QMenu* menu = new QMenu(menuButton);
    for (const auto& [dockName, title] : std::initializer_list<std::pair<const char*, const char*>>{
             {"colorDock", "Color"}, {"swatchesDock", "Swatches"}, {"libraryDock", "Library"}})
        if (QDockWidget* dock = findChild<QDockWidget*>(dockName))
            menu->addAction(title, this, [this, dock]() { showArtistSheet(dock); });
    menu->addSeparator();
    for (QAction* top : menuBar()->actions())
        if (top->menu() && top->isVisible())
            menu->addAction(top);
    menu->addSeparator();
    menu->addAction("Classic Mode", this, [this]() {
        QSettings().setValue("workspace/artist", false);
        setArtistLayout(false);
    });
    menuButton->setMenu(menu);
    _artistMenuAction = tools->addWidget(menuButton);
    _artistMenuAction->setVisible(_artistCompact);

    // Properties opens from a round button floating at the stage's right
    // edge, where its drawer comes from; the button moves out of the
    // drawer's way while it is open. A window of its own, so it shows over
    // the stage's canvas.
    QToolButton* properties = new QToolButton(this);
    properties->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
    properties->setAttribute(Qt::WA_ShowWithoutActivating);
    properties->setAttribute(Qt::WA_TranslucentBackground);
    properties->setObjectName("artistPropertiesButton");
    properties->setCheckable(true);
    properties->setIcon(artistIcon(ArtistIcon::Properties));
    properties->setIconSize(QSize(kFloatSize - 20, kFloatSize - 20));
    properties->setFixedSize(kFloatSize, kFloatSize);
    properties->setToolTip("Properties of the tool or what is selected");
    properties->setStyleSheet(QString("QToolButton { border-radius: %1px; border: 1px solid palette(mid);"
                                      " background: palette(button); }"
                                      "QToolButton:hover { background: palette(midlight); }"
                                      "QToolButton:checked { background: palette(highlight); }")
                                  .arg(kFloatSize / 2));
    _artistProperties = properties;
    _artistDrawers->add(findChild<QDockWidget*>("propertiesDock"), Qt::RightEdge, properties, 400);
    _artistDrawers->stageMoved = [this]() {
        placeArtistProperties();
        placeArtistStrip();
        placeArtistStraighten();
        // On a phone, fitted until the window has come down to the page.
        if (!_artistFitPending)
            return;
        _phoenixView->zoomToFit();
        if (QScreen* screen = this->screen(); screen && geometry() == screen->geometry())
            _artistFitPending = false;
    };
    _artistDrawers->changed = [this]() {
        placeArtistProperties();
        placeArtistStrip();
    };

    // The time strip, over the foot of the stage while the timeline is
    // closed: the current layer as the timeline has it.
    const auto shownLayer = [this]() -> const fla::Layer* {
        fla::Timeline* timeline = nullptr;
        int index = -1;
        if (!_timelineView->currentLayer(timeline, index) || !timeline || index < 0 ||
            index >= static_cast<int>(timeline->layers.size()))
            return nullptr;
        return timeline->layers[static_cast<size_t>(index)];
    };
    ArtistTimeStrip::Actions actions;
    actions.stepBack = [timelines]() {
        if (TimelinesPanel* panel = timelines())
            panel->stepBack();
    };
    actions.playStop = [timelines]() {
        if (TimelinesPanel* panel = timelines())
            panel->playStop();
    };
    actions.stepForward = [timelines]() {
        if (TimelinesPanel* panel = timelines())
            panel->stepForward();
    };
    actions.newFrame = [this]() { artistNewFrame(false); };
    _artistStrip = new ArtistTimeStrip(this, _player, shownLayer, actions);
    connect(_player, &Player::playingChanged, _artistStrip, [this](bool playing) { _artistStrip->setPlaying(playing); });
    connect(_player, &Player::currentFrameChanged, _artistStrip, [this]() { _artistStrip->refresh(); });
    connect(_timelineView, &TimelineView::currentLayerChanged, _artistStrip, [this]() {
        _artistStrip->refresh();
        placeArtistStrip();
    });
    connect(this, &MainWindow::editStateChanged, _artistStrip, [this]() {
        _artistStrip->refresh();
        placeArtistStrip();
    });

    // A turned stage's way back, over its corner (a window, so it shows
    // over the stage's canvas).
    QToolButton* straighten = new QToolButton(this);
    straighten->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
    straighten->setAttribute(Qt::WA_ShowWithoutActivating);
    straighten->setObjectName("artistStraighten");
    straighten->setIcon(artistIcon(ArtistIcon::Straighten));
    straighten->setIconSize(QSize(kButtonSize - 12, kButtonSize - 12));
    straighten->setFixedSize(kButtonSize, kButtonSize);
    straighten->setToolTip("Reset Rotation");
    connect(straighten, &QToolButton::clicked, this, [this]() { _phoenixView->setRotation(0.0); });
    _artistStraighten = straighten;
    connect(_phoenixView, &PhoenixView::rotationChanged, this, [this]() { placeArtistStraighten(); });

    // A press held still on the stage opens the pop-up palette, as a
    // right-click does with a mouse; on while the layout is, on a touch
    // screen.
    _artistHold = new StageHold(_phoenixView, [this](const QPoint& at) { showPopupPalette(at); });
}

void MainWindow::placeArtistStraighten()
{
    if (!_artistStraighten)
        return;
    if (!_artistLayout || !_phoenixView->isVisible() || _phoenixView->rotation() == 0.0)
    {
        _artistStraighten->hide();
        return;
    }
    const QPoint corner = _phoenixView->mapToGlobal(QPoint(_phoenixView->width(), 0));
    _artistStraighten->move(corner + QPoint(-_artistStraighten->width() - 8, 8));
    _artistStraighten->show();
    _artistStraighten->raise();
}

void MainWindow::showArtistSheet(QDockWidget* dock)
{
    // One sheet at a time: the one up goes; asked for again, it closes.
    QDockWidget* previous = _artistSheet;
    if (previous && previous != dock)
        previous->hide();
    if (previous == dock && dock->isVisible())
    {
        dock->hide();
        _artistSheet = nullptr;
        return;
    }
    if (dock->isFloating())
        dock->setFloating(false);
    removeDockWidget(dock);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
    dock->show();
    dock->raise();
    resizeDocks({dock}, {std::max(160, height() * 2 / 5)}, Qt::Vertical);
    _artistSheet = dock;
}

void MainWindow::placeArtistStrip()
{
    if (!_artistStrip)
        return;
    const bool wanted = _artistLayout && _phoenixView->isVisible() && _artistStripAction &&
        _artistStripAction->isChecked() && _artistDrawers->openDrawer(Qt::BottomEdge).isEmpty();
    if (!wanted)
    {
        _artistStrip->hide();
        return;
    }
    // Centered over the foot of the stage, or of what the Properties drawer
    // leaves of it; as wide as its frames want, within that.
    const QRect stage(_phoenixView->mapToGlobal(QPoint(0, 0)), _phoenixView->size());
    const QRect side = _artistDrawers->openDrawer(Qt::RightEdge);
    const int right = side.isEmpty() ? stage.right() : side.left() - 1;
    const int room = right - stage.left() - 2 * 16;
    if (room < 220)
    {
        _artistStrip->hide();
        return;
    }
    const int width = std::min(room, _artistStrip->preferredWidth());
    const int height = _artistStrip->sizeHint().height();
    _artistStrip->setGeometry(stage.left() + (right - stage.left() - width) / 2, stage.bottom() - 12 - height, width, height);
    _artistStrip->show();
    _artistStrip->raise();
}

void MainWindow::placeArtistProperties()
{
    if (!_artistProperties)
        return;
    if (!_artistLayout || !_phoenixView->isVisible())
    {
        _artistProperties->hide();
        return;
    }
    // Halfway down the stage's right edge, or just left of the open drawer.
    const QRect stage(_phoenixView->mapToGlobal(QPoint(0, 0)), _phoenixView->size());
    const QRect drawer = _artistDrawers->openDrawer(Qt::RightEdge);
    const int right = drawer.isEmpty() ? stage.right() : drawer.left() - 1;
    _artistProperties->move(right - kFloatSize - 14, stage.center().y() - kFloatSize / 2);
    _artistProperties->show();
    _artistProperties->raise();
}

namespace {

/// The layer the timeline's current one, when it can be drawn on.
fla::Layer* currentLayerOf(TimelineView* view)
{
    fla::Timeline* timeline = nullptr;
    int index = -1;
    if (!view || !view->currentLayer(timeline, index) || !timeline || index < 0 ||
        index >= static_cast<int>(timeline->layers.size()))
        return nullptr;
    fla::Layer* layer = timeline->layers[static_cast<size_t>(index)];
    if (!layer || layer->isLocked() || layer->layerType == fla::Layer::Type::Folder)
        return nullptr;
    return layer;
}

} // namespace

void MainWindow::refreshArtistContext()
{
    if (!_artistEaseBox || !_artistLayout)
        return;
    bool shown = false;
    std::string ease = "jump";
    if (_flaDocument && _flaDocument->document)
    {
        if (fla::Layer* layer = currentLayerOf(_timelineView))
        {
            const int at = _player->currentFrame();
            if (const fla::Frame* span = fla::timeline::spanAt(*layer, at))
            {
                // A drawing with another after it can move there.
                const int end = span->index + std::max(span->duration, 1);
                for (const fla::Frame* frame : layer->frames)
                    shown = shown || (frame && frame->index == end);
                ease = fla::artist::easeOf(*layer, at);
            }
        }
    }
    _artistEaseBox->setEnabled(shown);
    const QSignalBlocker quiet(_artistEase);
    const int index = _artistEase->findData(QString::fromStdString(ease));
    _artistEase->setCurrentIndex(std::max(0, index));
}

void MainWindow::artistNewFrame(bool copy)
{
    fla::Layer* layer = currentLayerOf(_timelineView);
    if (!layer || !_flaDocument || !_flaDocument->document || _player->isPlaying())
        return;
    const int at = _player->currentFrame();
    int made = -1;
    // What was selected is on the frame being left.
    _selection.clear();
    editLayer(_editContext.commandStack(), layer, copy ? "Duplicate Frame" : "New Frame",
        [at, copy, &made](fla::Layer& edited, std::vector<fla::Frame*>&) {
            made = fla::artist::addFrameAfter(edited, at, copy);
        });
    _phoenixView->clearCaches();
    _timelineView->refreshFrames();
    if (made >= 0)
        _player->setCurrentFrame(made);
    _phoenixView->update();
    refreshArtistContext();
}

void MainWindow::artistHold(int frames)
{
    fla::Layer* layer = currentLayerOf(_timelineView);
    if (!layer || !_flaDocument || !_flaDocument->document || _player->isPlaying() || frames == 0)
        return;
    const int at = _player->currentFrame();
    const fla::Frame* span = fla::timeline::spanAt(*layer, at);
    bool changed = false;
    if (frames > 0)
    {
        changed = editLayer(_editContext.commandStack(), layer, "Longer",
            [at, frames](fla::Layer& edited, std::vector<fla::Frame*>&) {
                fla::timeline::insertFrames(edited, at, frames);
            });
    }
    else if (span && span->duration > 1)
    {
        // The span's last frames go; the playhead stays on the span.
        const int count = std::min(-frames, span->duration - 1);
        const int first = span->index + span->duration - count;
        changed = editLayer(_editContext.commandStack(), layer, "Shorter",
            [first, count](fla::Layer& edited, std::vector<fla::Frame*>& removed) {
                fla::timeline::removeFrames(edited, first, count, removed);
            });
        if (changed && at >= first)
            _player->setCurrentFrame(first - 1);
    }
    if (!changed)
        return;
    _phoenixView->clearCaches();
    _phoenixView->update();
    _timelineView->refreshFrames();
    refreshArtistContext();
}

void MainWindow::artistSetEase(const QString& ease)
{
    fla::Layer* layer = currentLayerOf(_timelineView);
    if (!layer || !_flaDocument || !_flaDocument->document)
        return;
    const int at = _player->currentFrame();
    const std::string name = ease.toStdString();
    fla::CommandStack& stack = _editContext.commandStack();
    _selection.clear();
    stack.beginMacro(name == "jump" ? "Remove Tween" : "Ease");
    if (name != "jump")
    {
        // A slide needs a symbol at each end.
        fla::Frame* span = fla::timeline::spanAt(*layer, at);
        fla::Frame* next = span ? fla::InsertKeyframeCommand::keyframeAt(*layer, span->index + std::max(span->duration, 1))
                                : nullptr;
        for (fla::Frame* frame : {span, next})
            symbolize(_flaDocument->document, _phoenixView, nullptr, stack, layer, frame);
    }
    editLayer(stack, layer, name == "jump" ? "Remove Tween" : "Ease",
        [at, name](fla::Layer& edited, std::vector<fla::Frame*>&) { fla::artist::setEase(edited, at, name); });
    stack.endMacro();
    _phoenixView->clearCaches();
    _phoenixView->update();
    _timelineView->refreshFrames();
    refreshArtistContext();
}

void MainWindow::artistBeginMove(const QPoint& viewPos)
{
    if (!_artistLayout || !_flaDocument || !_flaDocument->document || !_phoenixView || _player->isPlaying())
        return;
    // A release that never came.
    if (_artistMove)
        artistEndMove();
    Tool* tool = _phoenixView->activeTool();
    const bool arrow = tool == _selectionTool.get();
    if (!arrow && tool != _freeTransformTool.get())
        return;

    // The layers the press would move something on: what is under it when
    // that is not selected, else the selection's (the arrow on empty stage
    // starts a marquee, the transform tool may be on a handle).
    const int at = _player->currentFrame();
    const HitResult hit = _phoenixView->hitTest(_phoenixView->mapToDocument(QPointF(viewPos)), _phoenixView->pickTolerance());
    std::vector<fla::Layer*> layers;
    const auto consider = [&layers](fla::DOMElement* element) {
        fla::Layer* layer = layerOf(frameOf(element));
        if (layer && !layer->isLocked() && std::find(layers.begin(), layers.end(), layer) == layers.end())
            layers.push_back(layer);
    };
    if (hit && !_selection.contains(hit.element))
        consider(hit.element);
    else if (hit || !arrow)
        for (fla::DOMElement* element : _selection.elements())
            consider(element);
    layers.erase(std::remove_if(layers.begin(), layers.end(),
                     [at](const fla::Layer* layer) { return !fla::artist::insideSpan(*layer, at); }),
        layers.end());
    if (layers.empty())
        return;

    fla::CommandStack& stack = _editContext.commandStack();
    _artistMove = std::make_shared<ArtistMove>();
    _artistMove->document = _flaDocument;
    _artistMove->topBefore = stack.topCommand();
    stack.beginMacro("Move");
    std::vector<fla::DOMElement*> selected = _selection.elements();
    bool reselect = false;
    for (fla::Layer* layer : layers)
    {
        ArtistMove::Keyed keyed;
        keyed.layer = layer;
        keyed.span = fla::timeline::spanAt(*layer, at);
        if (!keyed.span)
            continue;
        keyed.before = keyed.span->elements;
        // The drawing becomes a symbol when it has to, so a tween can carry it.
        if (symbolize(_flaDocument->document, _phoenixView, &_selection, stack, layer, keyed.span))
        {
            keyed.converted = true;
            selected = _selection.elements();
        }
        // The frame of its own, holding the pose shown, the span before it
        // tweened to it.
        const std::vector<fla::Element*> spanElements = keyed.span->elements;
        editLayer(stack, layer, "Keyframe",
            [at](fla::Layer& edited, std::vector<fla::Frame*>&) { fla::artist::keyForMove(edited, at); });
        keyed.key = fla::InsertKeyframeCommand::keyframeAt(*layer, at);
        if (!keyed.key)
            continue;
        for (const fla::Element* element : keyed.key->elements)
            keyed.poses.push_back(element->transform);
        // The selection follows the drawing into its new frame.
        for (fla::DOMElement*& element : selected)
        {
            const auto found = std::find(spanElements.begin(), spanElements.end(), element);
            if (found == spanElements.end())
                continue;
            const size_t index = static_cast<size_t>(found - spanElements.begin());
            if (index < keyed.key->elements.size())
            {
                element = keyed.key->elements[index];
                reselect = true;
            }
        }
        _artistMove->layers.push_back(std::move(keyed));
    }
    if (reselect)
        _selection.select(selected);
    _phoenixView->clearCaches();
    _phoenixView->update();
    _timelineView->refreshFrames();
}

void MainWindow::artistEndMove()
{
    if (!_artistMove)
        return;
    const std::shared_ptr<ArtistMove> move = std::move(_artistMove);
    fla::CommandStack& stack = _editContext.commandStack();
    for (int guard = 0; guard < 4 && stack.isInMacro(); ++guard)
        stack.endMacro();
    if (move->document != _flaDocument || !_flaDocument || !_phoenixView)
        return;

    bool moved = false;
    for (const ArtistMove::Keyed& keyed : move->layers)
    {
        if (keyed.key->elements.size() != keyed.poses.size())
            moved = true;
        for (size_t i = 0; !moved && i < keyed.poses.size(); ++i)
            moved = !sameTransform(keyed.key->elements[i]->transform, keyed.poses[i]);
    }
    // A press that moved nothing leaves no keyframe behind: the step goes,
    // and the selection goes back to the drawing as it was.
    if (!moved && stack.topCommand() != move->topBefore)
    {
        std::vector<fla::DOMElement*> restored;
        for (fla::DOMElement* element : _selection.elements())
        {
            bool mapped = false;
            for (const ArtistMove::Keyed& keyed : move->layers)
            {
                const auto& copies = keyed.key->elements;
                const auto found = std::find(copies.begin(), copies.end(), element);
                if (found == copies.end())
                    continue;
                mapped = true;
                if (keyed.converted)
                    restored.insert(restored.end(), keyed.before.begin(), keyed.before.end());
                else if (const size_t index = static_cast<size_t>(found - copies.begin()); index < keyed.before.size())
                    restored.push_back(keyed.before[index]);
            }
            if (!mapped)
                restored.push_back(element);
        }
        _selection.clear();
        if (stack.discardLast())
        {
            if (!restored.empty())
                _selection.select(restored);
        }
    }
    _phoenixView->clearCaches();
    _phoenixView->update();
    _timelineView->refreshFrames();
    refreshArtistContext();
}
