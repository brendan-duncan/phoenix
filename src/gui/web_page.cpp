#include "web_page.h"

// Compiled only into the web build (CMakeLists.txt).

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QMainWindow>
#include <QPalette>
#include <QScreen>
#include <QTimer>
#include <QWidget>

#include <emscripten/val.h>

namespace web_page {

namespace {

// The page stacks a window that belongs to another just above it, newest
// lowest: a menu opened after a panel was floated would come up under the
// panel. Pop-ups are kept on top of everything instead, set so before
// they are first shown (setting it later hides the window).
class PopupsOnTop : public QObject
{
protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Polish && watched->isWidgetType())
        {
            QWidget* widget = static_cast<QWidget*>(watched);
            const Qt::WindowType type = widget->windowType();
            if (widget->isWindow() && !widget->isVisible() && (type == Qt::Popup || type == Qt::ToolTip) &&
                !widget->windowFlags().testFlag(Qt::WindowStaysOnTopHint))
                widget->setWindowFlag(Qt::WindowStaysOnTopHint, true);
        }
        return false;
    }
};

// The page is Phoenix's screen, and a main window always fills it. On the
// desktop a main window takes back the size and place it was saved with
// (a workspace, a window of its own for a tab); in a page that would leave
// part of the page uncovered and never drawn again, still showing whatever
// was there before.
class MainWindowsFillPage : public QObject
{
protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        switch (event->type())
        {
        case QEvent::Show:
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::WindowStateChange:
            break;
        default:
            return false;
        }
        auto* window = qobject_cast<QMainWindow*>(watched);
        if (!window || !window->isWindow() || !window->isVisible())
            return false;
        // Once the change in hand is done.
        // Once the change in hand is done. A saved size taken back can leave
        // the window full screen in name only, so its size is what counts.
        QTimer::singleShot(0, window, [window]() {
            QScreen* screen = window->screen();
            if (!window->isVisible() || !screen || window->geometry() == screen->geometry())
                return;
            window->setWindowState((window->windowState() & ~(Qt::WindowMaximized | Qt::WindowMinimized)) |
                Qt::WindowFullScreen);
            window->setGeometry(screen->geometry());
        });
        return false;
    }
};

QString css(const QColor& color)
{
    return color.name(QColor::HexRgb);
}

// The frames the page draws around Phoenix's dialogs and floating panels,
// in the theme's colors: setWindowColors() sets the variables on the page,
// and they reach in to Qt's own elements, which live in a shadow root of
// their own that the page's style sheet cannot.
const char* const kWindowStyle = R"css(
.qt-decorated-window { background-color: var(--phoenix-window, #2d2d2d); }
.qt-decorated-window.has-border { border-color: var(--phoenix-window-border, #282828); }
.qt-decorated-window .title-bar {
    color: var(--phoenix-window-text, #e1e1e1);
    background-color: var(--phoenix-window, #2d2d2d);
}
.qt-decorated-window .title-bar .window-name {
    font-family: system-ui, -apple-system, "Segoe UI", sans-serif;
    font-size: 13px;
}
.qt-decorated-window.inactive .title-bar { opacity: 0.6; }
.title-bar .action-button img { filter: var(--phoenix-window-buttons, none); }
)css";

void addWindowStyle()
{
    emscripten::val document = emscripten::val::global("document");
    emscripten::val container = document.call<emscripten::val>("getElementById", std::string("qt-shadow-container"));
    if (container.isNull() || container.isUndefined() || container["shadowRoot"].isNull())
        return;
    emscripten::val style = document.call<emscripten::val>("createElement", std::string("style"));
    style.set("textContent", std::string(kWindowStyle));
    container["shadowRoot"].call<void>("appendChild", style);
}

} // namespace

void install()
{
    static PopupsOnTop popups;
    qApp->installEventFilter(&popups);
    static MainWindowsFillPage fill;
    qApp->installEventFilter(&fill);
    addWindowStyle();
}

void setWindowColors(const QPalette& palette)
{
    // The page's style sheet (resources/web/index.html) draws the title
    // bars with these.
    emscripten::val style = emscripten::val::global("document")["documentElement"]["style"];
    style.call<void>("setProperty", std::string("--phoenix-window"),
        css(palette.color(QPalette::Window)).toStdString());
    style.call<void>("setProperty", std::string("--phoenix-window-text"),
        css(palette.color(QPalette::WindowText)).toStdString());
    style.call<void>("setProperty", std::string("--phoenix-window-border"),
        css(palette.color(QPalette::Mid)).toStdString());
    // The close and maximize buttons are black pictures: white on a dark bar.
    style.call<void>("setProperty", std::string("--phoenix-window-buttons"),
        std::string(palette.color(QPalette::Window).lightness() < 128 ? "invert(1)" : "none"));
}

} // namespace web_page
