#pragma once

// Dialogs, message boxes and pop-up menus that answer through a callback.
//
// On the desktop each of these waits for its answer (exec()) and calls back
// before returning, exactly as the code that waited itself did. A browser
// cannot wait: the page's thread has to return to it for anything to be
// drawn or clicked. There they show the dialog or menu and return at once,
// and call back when it is answered. Code written this way behaves the same
// in both, and the desktop keeps testing the path the web build takes.
//
// What a callback needs from the dialog, it reads from the dialog it is
// given: the dialog is deleted only after the callback returns.

#include <QMessageBox>
#include <QPoint>
#include <QString>

#include <functional>

class QAction;
class QDialog;
class QMenu;
class QWidget;

namespace modal {

/// True where nothing can wait for an answer (the web build): a call that
/// cannot be turned around to answer through a callback has to do without.
bool cannotWait();

/// Shows \a dialog modally and calls \a done with its result once it is
/// closed (QDialog::Accepted or Rejected, or what done() gave), then deletes
/// it. \a dialog is created with new and handed over.
void open(QDialog* dialog, std::function<void(int result)> done = {});

/// Shows \a box and calls \a done with the button chosen (the standard
/// button, or NoButton for one added with addButton: \a box's
/// clickedButton() says which), then deletes it.
void open(QMessageBox* box, std::function<void(QMessageBox::StandardButton)> done);

/// A message box of \a icon with \a buttons, answered through \a done.
void ask(QWidget* parent, QMessageBox::Icon icon, const QString& title, const QString& text,
    QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton,
    std::function<void(QMessageBox::StandardButton)> done);

/// QMessageBox's information(), warning() and critical() for messages that
/// only need reading: shown, and on the desktop waited for, as before.
void information(QWidget* parent, const QString& title, const QString& text);
void warning(QWidget* parent, const QString& title, const QString& text);
void critical(QWidget* parent, const QString& title, const QString& text);
/// QMessageBox::about(): the application's icon beside \a text.
void about(QWidget* parent, const QString& title, const QString& text);

/// Pops \a menu up at \a globalPos and calls \a done with the action chosen,
/// or nullptr; then deletes it. \a menu is created with new and handed over.
/// Actions connected to as usual are triggered as usual.
void popup(QMenu* menu, const QPoint& globalPos, std::function<void(QAction*)> done = {});

} // namespace modal
