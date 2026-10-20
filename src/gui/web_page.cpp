#include "web_page.h"

// Compiled only into the web build (CMakeLists.txt).

#include <QApplication>
#include <QColor>
#include <QDialog>
#include <QDockWidget>
#include <QEvent>
#include <QKeySequence>
#include <QLayout>
#include <QMainWindow>
#include <QPalette>
#include <QPointer>
#include <QScreen>
#include <QSet>
#include <QTabBar>
#include <QTimer>
#include <QWidget>

#include <emscripten/emscripten.h>
#include <emscripten/val.h>

#include <algorithm>
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

// Whether \a bar labels a panel of \a docks that is open beside it: the tab
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
// tabs beside the real one, tabs of hidden panels over the stage. The window
// takes these for bars still in use and shows them again whenever it lays
// out its panels, but never moves them, so they are moved off the window as
// the window itself puts away the bars it no longer uses (hiding them blinks,
// shown again by the next layout and hidden after). After any change to a
// main window's panels (a panel or tab bar shown, hidden, moved or
// resized, or the window's own layout), once it is done, tab bars over no
// open panel of theirs go. A panel's contents changing are not watched:
// playing changes some every frame.
class RedrawAfterPanelChanges : public QObject
{
protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        const QEvent::Type type = event->type();
        if (qobject_cast<QMainWindow*>(watched))
        {
            if (type != QEvent::LayoutRequest && type != QEvent::Resize)
                return false;
        }
        else if (qobject_cast<QDockWidget*>(watched) || qobject_cast<QTabBar*>(watched))
        {
            if (type != QEvent::Resize && type != QEvent::Move && type != QEvent::Show && type != QEvent::Hide)
                return false;
        }
        else
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
                if (bar->isVisible() && bar->count() > 0 && bar->x() >= 0 && !labelsOpenPanel(bar, docks))
                    bar->move(-500 - bar->width(), -500 - bar->height());
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

// A dialog made for a desktop's screen can be larger than a tablet's or a
// phone's page, its buttons out of reach. Shown larger than the page, it is
// made to fit, its contents squeezed rather than cut off, and kept on the
// page.
class DialogsFitPage : public QObject
{
protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() != QEvent::Show)
            return false;
        auto* dialog = qobject_cast<QDialog*>(watched);
        if (!dialog || !dialog->isWindow())
            return false;
        QPointer<QDialog> guarded(dialog);
        // Once it has its size.
        QTimer::singleShot(0, dialog, [guarded]() {
            if (!guarded || !guarded->isVisible() || !guarded->screen())
                return;
            QDialog* fitting = guarded;
            const QRect page = fitting->screen()->availableGeometry();
            const QRect frame = fitting->frameGeometry();
            const QSize margins = frame.size() - fitting->size();
            const QSize most = page.size() - margins;
            if (fitting->width() > most.width() || fitting->height() > most.height())
            {
                if (fitting->layout())
                    fitting->layout()->setSizeConstraint(QLayout::SetNoConstraint);
                fitting->setMinimumSize(0, 0);
                fitting->resize(fitting->size().boundedTo(most));
            }
            QRect placed = fitting->frameGeometry();
            placed.moveLeft(std::clamp(placed.left(), page.left(), std::max(page.left(), page.right() - placed.width() + 1)));
            placed.moveTop(std::clamp(placed.top(), page.top(), std::max(page.top(), page.bottom() - placed.height() + 1)));
            if (placed.topLeft() != fitting->frameGeometry().topLeft())
                fitting->move(placed.topLeft());
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
    static RedrawAfterPanelChanges redraw;
    qApp->installEventFilter(&redraw);
    static DialogsFitPage fit;
    qApp->installEventFilter(&fit);
    addWindowStyle();
}

namespace {

std::function<bool()> g_unsavedWorkCheck;

/// The keys a browser keeps: they close, open and switch its tabs and
/// windows, and a page never sees them.
bool browserKeeps(const QKeySequence& key)
{
    if (key.count() != 1)
        return false;
    const QKeyCombination combination = key[0];
    const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
    const Qt::Key code = combination.key();
    if (modifiers != Qt::ControlModifier && modifiers != (Qt::ControlModifier | Qt::ShiftModifier))
        return false;
    return code == Qt::Key_N || code == Qt::Key_T || code == Qt::Key_W || code == Qt::Key_Tab ||
        code == Qt::Key_Backtab || code == Qt::Key_PageUp || code == Qt::Key_PageDown;
}

/// The keys tried in place of \a key, best first.
QList<QKeySequence> replacements(const QKeySequence& key)
{
    const QKeyCombination combination = key[0];
    const Qt::Key code = combination.key();
    const bool shift = combination.keyboardModifiers().testFlag(Qt::ShiftModifier);
    if (code == Qt::Key_Tab || code == Qt::Key_Backtab)
        return {QKeySequence(Qt::CTRL | Qt::ALT | (shift ? Qt::Key_PageUp : Qt::Key_PageDown))};
    const QKeyCombination withAlt = QKeyCombination(combination.keyboardModifiers() | Qt::AltModifier, code);
    return {QKeySequence(withAlt), QKeySequence(QKeyCombination(withAlt.keyboardModifiers() | Qt::ShiftModifier, code))};
}

} // namespace

void avoidBrowserKeys(QWidget* window)
{
    const QList<QAction*> actions = window->findChildren<QAction*>();
    QSet<QKeySequence> taken;
    for (const QAction* action : actions)
        for (const QKeySequence& key : action->shortcuts())
            taken.insert(key);
    for (QAction* action : actions)
    {
        QList<QKeySequence> keys = action->shortcuts();
        bool changed = false;
        for (int i = keys.size() - 1; i >= 0; --i)
        {
            if (!browserKeeps(keys[i]))
                continue;
            changed = true;
            const QKeySequence kept = keys[i];
            keys.removeAt(i);
            for (const QKeySequence& other : replacements(kept))
            {
                if (taken.contains(other) || keys.contains(other))
                    continue;
                keys.insert(i, other);
                taken.insert(other);
                break;
            }
        }
        if (changed)
            action->setShortcuts(keys);
    }
}

void setUnsavedWorkCheck(std::function<bool()> check)
{
    g_unsavedWorkCheck = std::move(check);
}

namespace {

bool coarsePointer()
{
    emscripten::val window = emscripten::val::global("window");
    if (window["matchMedia"].isUndefined())
        return false;
    return window.call<emscripten::val>("matchMedia", std::string("(pointer: coarse)"))["matches"].as<bool>();
}

int shorterScreenSide()
{
    emscripten::val screen = emscripten::val::global("screen");
    return std::min(screen["width"].as<int>(), screen["height"].as<int>());
}

} // namespace

bool isPhoneSized()
{
    return coarsePointer() && shorterScreenSide() < 600;
}

bool isTouchTablet()
{
    return coarsePointer() && shorterScreenSide() >= 600;
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

// For the page (resources/web/index.html), before it is closed or reloaded.
extern "C" EMSCRIPTEN_KEEPALIVE int phoenix_has_unsaved_work()
{
    return web_page::g_unsavedWorkCheck && web_page::g_unsavedWorkCheck() ? 1 : 0;
}
