// Menus and dialogs that would wait, in a browser.
//
// A browser page cannot run an event loop of its own, and Qt stops the
// page when one starts. Phoenix's own dialogs and menus answer through
// modal.h and never wait, but Qt's widgets open some of their own with
// exec(): a tool button's or push button's menu, for one. The web build
// links with QMenu::exec and QDialog::exec wrapped (-Wl,--wrap in
// CMakeLists.txt), so those come here instead:
//
// - a menu pops up and exec() returns at once with no action; the action
//   chosen is triggered as usual, which is all those menus need;
// - a dialog that something still waits on is not shown, and exec() answers
//   as if it was canceled, rather than stopping the page. Nothing in
//   Phoenix should get here; the console says which dialog did.

#include <QDebug>
#include <QDialog>
#include <QMenu>

class QAction;

// QMenu::exec(const QPoint&, QAction*) and QMenu::exec().
extern "C" QAction* __wrap__ZN5QMenu4execERK6QPointP7QAction(QMenu* menu, const QPoint& at, QAction* action)
{
    menu->popup(at, action);
    return nullptr;
}

extern "C" QAction* __wrap__ZN5QMenu4execEv(QMenu* menu)
{
    menu->popup(menu->pos());
    return nullptr;
}

// QDialog::exec().
extern "C" int __wrap__ZN7QDialog4execEv(QDialog* dialog)
{
    qWarning("A dialog waited for an answer, which a browser cannot do; it was answered as canceled: %s \"%s\"",
        dialog->metaObject()->className(), qPrintable(dialog->windowTitle()));
    dialog->setResult(QDialog::Rejected);
    return QDialog::Rejected;
}
