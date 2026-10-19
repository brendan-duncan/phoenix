#include "modal.h"

#include <QAction>
#include <QDialog>
#include <QIcon>
#include <QMenu>
#include <QPointer>

namespace modal {

bool cannotWait()
{
#ifdef PHOENIX_WEB
    return true;
#else
    return false;
#endif
}

void open(QDialog* dialog, std::function<void(int)> done)
{
    if (!dialog)
        return;
    dialog->setAttribute(Qt::WA_DeleteOnClose, false);
#ifdef PHOENIX_WEB
    dialog->setWindowModality(Qt::WindowModal);
    // finished() comes once, from done(), whichever way the dialog closes.
    QObject::connect(dialog, &QDialog::finished, dialog, [dialog, done](int result) {
        if (done)
            done(result);
        dialog->deleteLater();
    });
    dialog->open();
#else
    const int result = dialog->exec();
    if (done)
        done(result);
    delete dialog;
#endif
}

void open(QMessageBox* box, std::function<void(QMessageBox::StandardButton)> done)
{
    if (!box)
        return;
    const auto answer = [box]() {
        QAbstractButton* clicked = box->clickedButton();
        return clicked ? box->standardButton(clicked) : QMessageBox::NoButton;
    };
    open(static_cast<QDialog*>(box), [answer, done](int) {
        if (done)
            done(answer());
    });
}

void ask(QWidget* parent, QMessageBox::Icon icon, const QString& title, const QString& text,
    QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton,
    std::function<void(QMessageBox::StandardButton)> done)
{
    QMessageBox* box = new QMessageBox(icon, title, text, buttons, parent);
    if (defaultButton != QMessageBox::NoButton)
        box->setDefaultButton(defaultButton);
    // Escape, or closing the box, answers as QMessageBox's own functions do.
    open(box, [box, done](QMessageBox::StandardButton answer) {
        if (answer == QMessageBox::NoButton)
        {
            QAbstractButton* escape = box->escapeButton();
            answer = escape ? box->standardButton(escape) : QMessageBox::NoButton;
        }
        if (done)
            done(answer);
    });
}

void information(QWidget* parent, const QString& title, const QString& text)
{
    ask(parent, QMessageBox::Information, title, text, QMessageBox::Ok, QMessageBox::Ok, {});
}

void warning(QWidget* parent, const QString& title, const QString& text)
{
    ask(parent, QMessageBox::Warning, title, text, QMessageBox::Ok, QMessageBox::Ok, {});
}

void critical(QWidget* parent, const QString& title, const QString& text)
{
    ask(parent, QMessageBox::Critical, title, text, QMessageBox::Ok, QMessageBox::Ok, {});
}

void about(QWidget* parent, const QString& title, const QString& text)
{
    QMessageBox* box = new QMessageBox(QMessageBox::NoIcon, title, text, QMessageBox::Ok, parent);
    const QIcon icon = parent ? parent->windowIcon() : QIcon();
    if (!icon.isNull())
        box->setIconPixmap(icon.pixmap(64, 64));
    open(box, {});
}

void popup(QMenu* menu, const QPoint& globalPos, std::function<void(QAction*)> done)
{
    if (!menu)
        return;
#ifdef PHOENIX_WEB
    // An action chosen is triggered after the menu hides, in the same call:
    // the menu is deleted only once that has run.
    auto chosen = std::make_shared<QPointer<QAction>>();
    QObject::connect(menu, &QMenu::triggered, menu, [chosen](QAction* action) { *chosen = action; });
    QObject::connect(menu, &QMenu::aboutToHide, menu, [menu, chosen, done]() {
        QMetaObject::invokeMethod(menu, [menu, chosen, done]() {
            if (done)
                done(chosen->data());
            menu->deleteLater();
        }, Qt::QueuedConnection);
    });
    menu->popup(globalPos);
#else
    QAction* chosen = menu->exec(globalPos);
    if (done)
        done(chosen);
    delete menu;
#endif
}

} // namespace modal
