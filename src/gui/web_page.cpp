#include "web_page.h"

// Compiled only into the web build (CMakeLists.txt).

#include <QApplication>
#include <QColor>
#include <QDockWidget>
#include <QEvent>
#include <QMainWindow>
#include <QPalette>
#include <QPointer>
#include <QScreen>
#include <QSet>
#include <QTabBar>
#include <QTimer>
#include <QWidget>

#include <emscripten/val.h>

#include <cstdlib>

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
        // Panels moving into place are animated, and a main window hides the
        // tab bars of the panels it closed when that animation ends, which
        // in a page it never does: the old tab bars stayed up, over and
        // beside the new ones. Not animated, they go at once.
        if (window->isAnimated())
            window->setAnimated(false);
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

// Whether  bar labels a panel of  docks that is open beside it: the tab
// bars a main window makes for grouped panels carry each panel in their
// tabs' data, and sit over or under the group, as wide as it.
bool labelsOpenPanel(const QTabBar* bar, const QList<QDockWidget*>& docks)
{
    for (int i = 0; i < bar->count(); ++i)
    {
        // Looked up among the panels there are, never followed: a closed
        // panel's tab can outlive it.
        const quintptr id = bar->tabData(i).value<quintptr>();
        for (const QDockWidget* dock : docks)
        {
            if (reinterpret_cast<quintptr>(dock) != id || !dock->isVisible())
                continue;
            const bool above = std::abs(dock->y() - bar->geometry().bottom()) <= 2;
            const bool below = std::abs(bar->y() - dock->geometry().bottom()) <= 2;
            if (dock->x() == bar->x() && dock->width() == bar->width() && (above || below))
                return true;
        }
    }
    return false;
}

// Leftovers. In a page, a main window has been seen to leave the tab bars of
// panel groups up after the panels in them closed or moved: a second row of
// tabs beside the real one, tabs of hidden panels over the stage. After any
// change to a main window's panels, once it is done, tab bars over no open
// panel of theirs are hidden (the window shows one again when it uses it),
// and the whole window is drawn again.
class RedrawAfterPanelChanges : public QObject
{
protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        const QEvent::Type type = event->type();
        if (type != QEvent::LayoutRequest && type != QEvent::Resize && type != QEvent::Move &&
            type != QEvent::Show && type != QEvent::Hide)
            return false;
        if (!qobject_cast<QDockWidget*>(watched) && !qobject_cast<QTabBar*>(watched) &&
            !qobject_cast<QMainWindow*>(watched))
            return false;
        QWidget* window = static_cast<QWidget*>(watched)->window();
        if (!qobject_cast<QMainWindow*>(window) || _pending.contains(window))
            return false;
        _pending.insert(window);
        QPointer<QWidget> guarded(window);
        QTimer::singleShot(50, this, [this, guarded, window]() {
            _pending.remove(window);
            if (!guarded)
                return;
            const QList<QDockWidget*> docks = guarded->findChildren<QDockWidget*>(Qt::FindDirectChildrenOnly);
            for (QTabBar* bar : guarded->findChildren<QTabBar*>(Qt::FindDirectChildrenOnly))
                if (bar->isVisible() && bar->count() > 0 && !labelsOpenPanel(bar, docks))
                    bar->hide();
            guarded->update();
            // ?debugdocks: what the panels and their tab bars are doing.
            if (qEnvironmentVariableIsSet("PHOENIX_DEBUG_DOCKS"))
            {
                for (QDockWidget* dock : guarded->findChildren<QDockWidget*>())
                    qWarning("dock %s visible=%d floating=%d geometry=%d,%d %dx%d", qPrintable(dock->objectName()),
                        dock->isVisible(), dock->isFloating(), dock->x(), dock->y(), dock->width(), dock->height());
                for (QTabBar* bar : guarded->findChildren<QTabBar*>())
                {
                    QStringList tabs;
                    for (int i = 0; i < bar->count(); ++i)
                        tabs << bar->tabText(i);
                    qWarning("tabbar parent=%s visible=%d geometry=%d,%d %dx%d tabs=%s",
                        bar->parentWidget() ? bar->parentWidget()->metaObject()->className() : "-", bar->isVisible(),
                        bar->x(), bar->y(), bar->width(), bar->height(), qPrintable(tabs.join("|")));
                }
            }
        });
        return false;
    }

private:
    QSet<QWidget*> _pending;
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
    static RedrawAfterPanelChanges redraw;
    qApp->installEventFilter(&redraw);
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
