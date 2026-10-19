#include "capture_steps.h"

#include "bone_tool.h"
#include "hot_text.h"
#include "main_window.h"
#include "phoenix_view.h"
#include "player.h"
#include "script_runner.h"
#include "toolbox.h"
#include "tutorial_runner.h"

#include "../data/frame.h"
#include "../edit/command_stack.h"
#include "../edit/ik_pose.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPixmap>
#include "process.h"
#include <QSpinBox>
#include <QTextStream>
#include <QTimer>

#include <cstdio>

namespace commands {

namespace {

void pump(int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

QString plain(QString text)
{
    text.remove('&');
    if (text.endsWith("..."))
        text.chop(3);
    if (text.endsWith(':'))
        text.chop(1);
    return text.trimmed();
}

bool point(const QString& text, QPointF& at)
{
    const QStringList xy = text.split(',');
    bool okX = false;
    bool okY = false;
    if (xy.size() == 2)
        at = QPointF(xy.at(0).toDouble(&okX), xy.at(1).toDouble(&okY));
    return okX && okY;
}

Qt::KeyboardModifiers modifiersOf(const QStringList& words, int from)
{
    Qt::KeyboardModifiers modifiers;
    for (int i = from; i < words.size(); ++i)
    {
        const QString word = words.at(i).toLower();
        if (word == "shift")
            modifiers |= Qt::ShiftModifier;
        else if (word == "ctrl")
            modifiers |= Qt::ControlModifier;
        else if (word == "alt")
            modifiers |= Qt::AltModifier;
    }
    return modifiers;
}

void mouse(QWidget* widget, QEvent::Type type, const QPointF& at, Qt::MouseButton button, Qt::MouseButtons buttons,
    Qt::KeyboardModifiers modifiers)
{
    QMouseEvent event(type, at, widget->mapToGlobal(at), button, buttons, modifiers);
    QApplication::sendEvent(widget, &event);
}

/// A press at \a from, moves to \a to and a release there, in \a widget's
/// own coordinates.
void dragIn(QWidget* widget, const QPointF& from, const QPointF& to, Qt::KeyboardModifiers modifiers)
{
    mouse(widget, QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton, modifiers);
    mouse(widget, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, modifiers);
    pump(20);
    const int steps = from == to ? 0 : 12;
    for (int i = 1; i <= steps; ++i)
    {
        mouse(widget, QEvent::MouseMove, from + (to - from) * (static_cast<double>(i) / steps), Qt::NoButton,
            Qt::LeftButton, modifiers);
        pump(5);
    }
    mouse(widget, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, modifiers);
    pump(60);
}

/// What the next dialog is to be given: values by label, then a button.
struct Answer
{
    QStringList values;
    QString press;
    QString picture;
    bool armed = false;
};

bool setField(QWidget* field, const QString& value)
{
    if (auto* hot = dynamic_cast<HotSpinBox*>(field))
        hot->setValue(value.toInt());
    else if (auto* hotDouble = dynamic_cast<HotDoubleSpinBox*>(field))
        hotDouble->setValue(value.toDouble());
    else if (auto* spin = qobject_cast<QSpinBox*>(field))
        spin->setValue(value.toInt());
    else if (auto* spinDouble = qobject_cast<QDoubleSpinBox*>(field))
        spinDouble->setValue(value.toDouble());
    else if (auto* line = qobject_cast<QLineEdit*>(field))
        line->setText(value);
    else if (auto* combo = qobject_cast<QComboBox*>(field))
    {
        const int index = combo->findText(value);
        // One that can be typed in takes what is not on its list.
        if (index < 0 && combo->isEditable())
            combo->setEditText(value);
        else if (index < 0)
            return false;
        else
            combo->setCurrentIndex(index);
    }
    else if (auto* box = qobject_cast<QCheckBox*>(field))
        box->setChecked(value.toLower() == "on" || value == "1" || value.toLower() == "true");
    else
        return false;
    return true;
}

/// Fills \a dialog as \a answer says and presses its button; what could not
/// be found goes to \a problems.
void answer(QWidget* dialog, const Answer& answer, QStringList& problems)
{
    for (const QString& pair : answer.values)
    {
        const QString label = pair.section('=', 0, 0);
        const QString value = pair.section('=', 1);
        bool done = false;
        for (QFormLayout* form : dialog->findChildren<QFormLayout*>())
        {
            for (int row = 0; row < form->rowCount() && !done; ++row)
            {
                QLayoutItem* labelItem = form->itemAt(row, QFormLayout::LabelRole);
                QLayoutItem* fieldItem = form->itemAt(row, QFormLayout::FieldRole);
                auto* text = labelItem ? qobject_cast<QLabel*>(labelItem->widget()) : nullptr;
                if (text && fieldItem && fieldItem->widget() &&
                    plain(text->text()).compare(label, Qt::CaseInsensitive) == 0)
                    done = setField(fieldItem->widget(), value);
            }
        }
        if (!done)
            for (QCheckBox* box : dialog->findChildren<QCheckBox*>())
                if (plain(box->text()).compare(label, Qt::CaseInsensitive) == 0)
                    done = setField(box, value);
        if (!done)
            problems << QString("the dialog has no %1").arg(label);
    }
    pump(50);
    if (!answer.picture.isEmpty())
    {
        if (dialog->grab().save(answer.picture))
            std::printf("Drew the dialog to %s\n", qPrintable(answer.picture));
        else
            problems << QString("could not write %1").arg(answer.picture);
    }
    if (answer.press.isEmpty())
        return;
    for (QAbstractButton* button : dialog->findChildren<QAbstractButton*>())
        if (plain(button->text()).compare(answer.press, Qt::CaseInsensitive) == 0)
        {
            button->click();
            return;
        }
    problems << QString("the dialog has no %1 button").arg(answer.press);
}

/// The pop-up open, if one is: the active one, or one showing off the
/// screen, which the application does not count as active.
QWidget* openPopup()
{
    if (QWidget* popup = QApplication::activePopupWidget())
        return popup;
    for (QWidget* widget : QApplication::topLevelWidgets())
        if (widget->isVisible() && widget->windowType() == Qt::Popup)
            return widget;
    return nullptr;
}

QDockWidget* dockNamed(MainWindow& window, const QString& name)
{
    for (QDockWidget* dock : window.findChildren<QDockWidget*>())
        if (dock->objectName() == name || plain(dock->windowTitle()).compare(name, Qt::CaseInsensitive) == 0)
            return dock;
    return nullptr;
}

} // namespace

bool runCaptureSteps(MainWindow& window, const QString& path, QString& error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        error = QString("cannot read %1").arg(path);
        return false;
    }
    const QStringList lines = QTextStream(&file).readAll().split('\n');
    Answer pending;
    // A stroke step's button still down, and where.
    bool held = false;
    QPointF heldAt;
    QStringList problems;
    // A dialog that opens while a step runs: answered when a dialog step
    // armed an answer, else reported and closed.
    QTimer watch;
    watch.setInterval(100);
    QObject::connect(&watch, &QTimer::timeout, [&]() {
        QWidget* dialog = QApplication::activeModalWidget();
        QWidget* popup = openPopup();
        if (!dialog && popup && popup->objectName() == "swatchPopup" && pending.armed)
        {
            // A swatch pop-up waited for as a dialog is: drawn, then given
            // the hex= color typed, or closed.
            const Answer now = pending;
            pending = Answer();
            pump(50);
            if (!now.picture.isEmpty())
            {
                if (popup->grab().save(now.picture))
                    std::printf("Drew the pop-up to %s\n", qPrintable(now.picture));
                else
                    problems << QString("could not write %1").arg(now.picture);
            }
            QLineEdit* hex = popup->findChild<QLineEdit*>("swatchPopupHex");
            for (const QString& value : now.values)
                if (hex && value.startsWith("hex=", Qt::CaseInsensitive))
                {
                    hex->setText(value.mid(4));
                    emit hex->returnPressed();
                    return;
                }
            popup->close();
            return;
        }
        if (!dialog)
            return;
        if (pending.armed)
        {
            const Answer now = pending;
            pending = Answer();
            answer(dialog, now, problems);
            return;
        }
        QString what = dialog->windowTitle();
        if (auto* box = qobject_cast<QMessageBox*>(dialog))
            what += ": " + box->text();
        problems << QString("a dialog no step answered: %1").arg(what);
        if (auto* asDialog = qobject_cast<QDialog*>(dialog))
            asDialog->reject();
        else
            dialog->close();
    });
    watch.start();

    PhoenixView* view = window.phoenixView();
    for (int number = 0; number < lines.size(); ++number)
    {
        const QString line = lines.at(number).trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        const QStringList words = Process::splitCommand(line);
        if (words.isEmpty())
            continue;
        const QString step = words.at(0).toLower();
        const QString where = QString("%1, line %2 (%3)").arg(QFileInfo(path).fileName()).arg(number + 1).arg(line);
        const auto fail = [&](const QString& why) {
            error = QString("%1: %2").arg(where, why);
            return false;
        };
        const QString first = words.size() > 1 ? words.at(1) : QString();
        if (step == "tool")
        {
            QAction* tool = window.toolbox() ? window.toolbox()->tool(first) : nullptr;
            if (!tool)
                return fail("no such tool");
            tool->trigger();
        }
        else if (step == "click" || step == "drag")
        {
            QPointF from;
            QPointF to;
            if (!view || !point(first, from))
                return fail("which point?");
            const bool drag = step == "drag";
            if (drag && (words.size() < 3 || !point(words.at(2), to)))
                return fail("drag where?");
            if (!drag)
                to = from;
            view->setFocus();
            dragIn(view, view->mapToWidget(from), view->mapToWidget(to), modifiersOf(words, drag ? 3 : 2));
        }
        else if (step == "stroke")
        {
            // A drag through several points, as a scribble or a loop; with
            // "hold" the button stays down for a picture mid-drag.
            std::vector<QPointF> through;
            for (const QString& word : words.mid(1))
            {
                QPointF at;
                if (point(word, at))
                    through.push_back(at);
            }
            if (!view || through.size() < 2)
                return fail("stroke through which points?");
            const Qt::KeyboardModifiers modifiers = modifiersOf(words, 1);
            view->setFocus();
            mouse(view, QEvent::MouseMove, view->mapToWidget(through.front()), Qt::NoButton, Qt::NoButton, modifiers);
            mouse(view, QEvent::MouseButtonPress, view->mapToWidget(through.front()), Qt::LeftButton, Qt::LeftButton,
                modifiers);
            pump(20);
            for (size_t i = 1; i < through.size(); ++i)
                for (int k = 1; k <= 6; ++k)
                {
                    const QPointF at = through[i - 1] + (through[i] - through[i - 1]) * (k / 6.0);
                    mouse(view, QEvent::MouseMove, view->mapToWidget(at), Qt::NoButton, Qt::LeftButton, modifiers);
                    pump(5);
                }
            held = words.contains("hold", Qt::CaseInsensitive);
            heldAt = through.back();
            if (!held)
                mouse(view, QEvent::MouseButtonRelease, view->mapToWidget(heldAt), Qt::LeftButton, Qt::NoButton,
                    modifiers);
            pump(60);
        }
        else if (step == "release")
        {
            if (!view || !held)
                return fail("no stroke is held");
            held = false;
            mouse(view, QEvent::MouseButtonRelease, view->mapToWidget(heldAt), Qt::LeftButton, Qt::NoButton, {});
            pump(60);
        }
        else if (step == "key")
        {
            const QKeySequence keys = QKeySequence::fromString(first);
            if (keys.isEmpty() || !view)
                return fail("which key?");
            const QKeyCombination combination = keys[0];
            QKeyEvent press(QEvent::KeyPress, combination.key(), combination.keyboardModifiers(),
                QKeySequence(combination.key()).toString().toLower());
            QKeyEvent release(QEvent::KeyRelease, combination.key(), combination.keyboardModifiers());
            // An open pop-up takes the keys before the stage, as it would.
            QWidget* to = openPopup() ? openPopup() : view;
            QApplication::sendEvent(to, &press);
            QApplication::sendEvent(to, &release);
            pump(60);
        }
        else if (step == "hover")
        {
            // widget:<name>@x,y: the pointer over that point, no button down.
            QString spec = first;
            QPointF at(-1.0, -1.0);
            if (const int sign = spec.indexOf('@'); sign > 0)
            {
                const QStringList xy = spec.mid(sign + 1).split(',');
                if (xy.size() == 2)
                    at = QPointF(xy.at(0).toDouble(), xy.at(1).toDouble());
                spec.truncate(sign);
            }
            const TutorialTarget target = tutorials::resolve(window, spec, true);
            if (!target.widget || at.x() < 0.0)
                return fail(target.problem.isEmpty() ? QString("hover where?") : target.problem);
            mouse(target.widget, QEvent::MouseMove, at, Qt::NoButton, Qt::NoButton, {});
            pump(60);
        }
        else if (step == "menu")
        {
            QString menuPath = words.mid(1).join(' ');
            QAction* action = tutorials::findMenuAction(window, menuPath.replace('>', '/'));
            if (!action)
                return fail("no such menu command");
            action->trigger();
        }
        else if (step == "target")
        {
            // widget:<name>@x,y clicks that point of the widget rather than
            // its middle (a swatch of a grid).
            QString spec = first;
            QPointF at(-1.0, -1.0);
            if (const int sign = spec.indexOf('@'); sign > 0)
            {
                const QStringList xy = spec.mid(sign + 1).split(',');
                if (xy.size() == 2)
                    at = QPointF(xy.at(0).toDouble(), xy.at(1).toDouble());
                spec.truncate(sign);
            }
            const TutorialTarget target = tutorials::resolve(window, spec, true);
            if (target.widget && at.x() >= 0.0)
                dragIn(target.widget, at, at, modifiersOf(words, 2));
            else if (target.action)
                target.action->trigger();
            else if (target.widget && !target.rect.isEmpty())
            {
                QWidget* widget = target.widget;
                // A tree's rows are clicked on its viewport.
                const QPointF center = QRectF(target.rect).center();
                if (auto* button = qobject_cast<QAbstractButton*>(widget))
                    button->click();
                else
                    dragIn(widget, center, center, modifiersOf(words, 2));
            }
            else
                return fail(target.problem.isEmpty() ? QString("no such target") : target.problem);
        }
        else if (step == "goto")
        {
            if (!view || !view->player() || first.toInt() < 1)
                return fail("which frame?");
            view->player()->setCurrentFrame(first.toInt() - 1);
        }
        else if (step == "dialog")
        {
            pending = Answer();
            pending.armed = true;
            for (const QString& word : words.mid(1))
            {
                if (word.startsWith("press=", Qt::CaseInsensitive))
                    pending.press = word.mid(6);
                else if (word.startsWith("picture=", Qt::CaseInsensitive))
                    pending.picture = word.mid(8);
                else
                    pending.values << word;
            }
            continue;
        }
        else if (step == "check")
        {
            bool done = false;
            for (QCheckBox* box : window.findChildren<QCheckBox*>())
                if (box->isVisible() && plain(box->text()).compare(first, Qt::CaseInsensitive) == 0)
                {
                    const bool on = words.size() < 3 || words.at(2).toLower() != "off";
                    if (box->isChecked() != on)
                        box->click();
                    done = true;
                    break;
                }
            if (!done)
                return fail("no such check box showing");
        }
        else if (step == "combo" || step == "pick" || step == "show")
        {
            QDockWidget* dock = dockNamed(window, first);
            if (!dock)
                return fail("no such panel");
            dock->show();
            dock->raise();
            pump(200);
            if (step == "combo")
            {
                bool done = false;
                for (QComboBox* combo : dock->findChildren<QComboBox*>())
                    if (combo->isVisible() && combo->findText(words.value(2)) >= 0)
                    {
                        combo->setCurrentIndex(combo->findText(words.value(2)));
                        done = true;
                        break;
                    }
                if (!done)
                    return fail("no list with that item");
            }
            else if (step == "pick")
            {
                QListWidget* list = dock->findChild<QListWidget*>();
                const int row = words.value(2).toInt() - 1;
                if (!list || row < 0 || row >= list->count())
                    return fail("no such picture");
                const QRect rect = list->visualItemRect(list->item(row));
                dragIn(list->viewport(), QRectF(rect).center(), QRectF(rect).center(), {});
            }
        }
        else if (step == "script")
        {
            if (!window.scripts() || !window.scripts()->runFile(QFileInfo(first).absoluteFilePath()))
                return fail("the script failed");
            window.refreshAfterScript();
        }
        else if (step == "capture" || step == "grab")
        {
            pump(400);
            QPixmap picture;
            QString out = first;
            if (step == "grab")
            {
                QWidget* widget = window.findChild<QWidget*>(first);
                if (!widget)
                    return fail("no such widget");
                picture = widget->grab();
                out = words.value(2);
            }
            else
            {
                picture = window.grab();
                const QStringList crop = words.value(2).split(',');
                if (crop.size() == 4)
                {
                    const double ratio = picture.devicePixelRatio();
                    picture = picture.copy(QRect(QPoint(crop.at(0).toInt(), crop.at(1).toInt()) * ratio,
                        QSize(crop.at(2).toInt(), crop.at(3).toInt()) * ratio));
                }
            }
            if (out.isEmpty() || !picture.save(out))
                return fail("could not write the picture");
            std::printf("Drew %dx%d to %s\n", picture.width(), picture.height(), qPrintable(out));
        }
        else if (step == "bones")
        {
            // Where each armature's joints show now, to aim the next drag.
            const std::vector<std::pair<fla::Frame*, int>> frames =
                view ? view->armatureFrames() : std::vector<std::pair<fla::Frame*, int>>();
            for (const auto& entry : frames)
                for (const std::string& child : entry.first->extraChildren)
                {
                    fla::IKArmature armature;
                    if (!armature.parse(child))
                        continue;
                    QStringList joints;
                    for (const fla::IKJoint& joint : armature.shownAt(entry.second))
                        joints << QString("%1,%2").arg(joint.position.x, 0, 'f', 1).arg(joint.position.y, 0, 'f', 1);
                    std::printf("%s frame %d: %s\n", armature.name().c_str(), entry.second + 1,
                        qPrintable(joints.join(' ')));
                }
        }
        else if (step == "selected")
        {
            // What a click picked: "instance", "shape", ... or "none".
            const QString kind = window.selectionKind();
            if (words.size() > 1 && kind != first)
                return fail(QString("%1 is selected").arg(kind));
            std::printf("selected: %s\n", qPrintable(kind));
        }
        else if (step == "points")
        {
            // A boned shape's points as they show, and the bones each follows.
            const std::vector<std::pair<fla::Frame*, int>> frames =
                view ? view->armatureFrames() : std::vector<std::pair<fla::Frame*, int>>();
            for (const auto& entry : frames)
                for (size_t child = 0; child < entry.first->extraChildren.size(); ++child)
                {
                    BoundShape shape;
                    if (!BoundShape::find(entry.first, child, entry.second, shape))
                        continue;
                    for (size_t i = 0; i < shape.shown.size(); ++i)
                    {
                        QStringList bones;
                        for (const fla::BoneWeight& bound : shape.binding[i])
                            bones << QString::number(bound.bone);
                        std::printf("point %d at %.1f,%.1f follows %s\n", static_cast<int>(i), shape.shown[i].x(),
                            shape.shown[i].y(), bones.isEmpty() ? "none" : qPrintable(bones.join(',')));
                    }
                }
        }
        else if (step == "expect")
        {
            const fla::CommandStack& stack = window.commandStack();
            const QString last = stack.index() > 0 ? QString::fromStdString(stack.nameAt(stack.index() - 1)) : QString();
            if (!last.startsWith(words.mid(1).join(' '), Qt::CaseInsensitive))
                return fail(QString("the last command is \"%1\"").arg(last));
        }
        else
            return fail("not a step");
        pump(150);
        if (pending.armed)
            return fail("no dialog opened to answer");
        if (!problems.isEmpty())
            return fail(problems.join("; "));
        std::printf("%s\n", qPrintable(line));
    }
    return true;
}

} // namespace commands
