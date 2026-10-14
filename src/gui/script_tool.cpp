#include "script_tool.h"

#include "phoenix_view.h"
#include "tool_icons.h"

#include "../jsfl/host.h"
#include "../jsfl/tool_script.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainterPath>

ScriptTool::ScriptTool(jsfl::Host& host, std::unique_ptr<jsfl::ToolScript> script)
    : _host(host)
    , _script(std::move(script))
{}

ScriptTool::~ScriptTool() = default;

QString ScriptTool::name() const
{
    const std::string& menu = _script->menuString();
    QString text = QString::fromStdString(menu.empty() ? _script->toolName() : menu);
    // The toolbox says "Tool" itself.
    if (text.endsWith(" Tool"))
        text.chop(5);
    return text;
}

QString ScriptTool::toolTip() const
{
    const std::string& tip = _script->toolTip();
    return QString::fromStdString(tip.empty() ? _script->menuString() : tip);
}

QIcon ScriptTool::icon() const
{
    const QString file = QString::fromStdString(_script->iconPath());
    if (!file.isEmpty() && QFileInfo::exists(file))
        return QIcon(file);
    return ToolIcons::icon(ToolIcons::Tool::Script);
}

QCursor ScriptTool::cursor() const
{
    switch (_script->cursor())
    {
    case 1:
    case 2: return Qt::ArrowCursor;
    case 3: return Qt::SizeAllCursor;
    case 4: return Qt::SizeHorCursor;
    case 5: return Qt::SizeVerCursor;
    case 6: return Qt::ForbiddenCursor;
    case 7: return Qt::OpenHandCursor;
    default: return Qt::CrossCursor;
    }
}

void ScriptTool::call(PhoenixView* view, const char* function, const QPointF& at, Qt::KeyboardModifiers modifiers, int key)
{
    jsfl::ToolScript::State state;
    state.penX = at.x();
    state.penY = at.y();
    state.downX = _down.x();
    state.downY = _down.y();
    state.mouseDown = _pressed;
    state.alt = modifiers & Qt::AltModifier;
    state.ctrl = modifiers & Qt::ControlModifier;
    state.shift = modifiers & Qt::ShiftModifier;
    state.key = key;
    const std::string error = _script->call(function, state);
    if (!error.empty())
        _host.trace(error);
    if (view)
        view->update();
}

void ScriptTool::activate()
{
    if (_active)
        return;
    _active = true;
    call(nullptr, "activate", QPointF(), Qt::NoModifier);
    call(nullptr, "setCursor", QPointF(), Qt::NoModifier);
}

void ScriptTool::deactivate(PhoenixView& view)
{
    if (!_active)
        return;
    _active = false;
    _pressed = false;
    call(&view, "deactivate", QPointF(), Qt::NoModifier);
}

bool ScriptTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;
    activate();
    _pressed = true;
    _down = documentPos;
    call(&view, "mouseDown", documentPos, event->modifiers());
    return true;
}

bool ScriptTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    activate();
    call(&view, "mouseMove", documentPos, event->modifiers());
    call(&view, "setCursor", documentPos, event->modifiers());
    view.setCursor(cursor());
    return _pressed;
}

bool ScriptTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton || !_pressed)
        return false;
    _pressed = false;
    call(&view, "mouseUp", documentPos, event->modifiers());
    return true;
}

bool ScriptTool::mouseDoubleClick(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (!_script->has("mouseDoubleClick"))
        return false;
    call(&view, "mouseDoubleClick", documentPos, event->modifiers());
    return true;
}

bool ScriptTool::keyPress(PhoenixView& view, QKeyEvent* event)
{
    if (!_script->has("keyDown"))
        return false;
    call(&view, "keyDown", _down, event->modifiers(), event->key());
    return true;
}

void ScriptTool::paintOverlay(PhoenixView& view, QPainter& painter, double scale)
{
    (void)view;
    for (const jsfl::ToolScript::Preview& preview : _script->preview())
    {
        QPainterPath path;
        for (const auto& piece : preview.pieces)
        {
            const auto& p = piece.points;
            if (piece.kind == 'M' && p.size() >= 2)
                path.moveTo(p[0], p[1]);
            else if (piece.kind == 'L' && p.size() >= 2)
                path.lineTo(p[0], p[1]);
            else if (piece.kind == 'Q' && p.size() >= 4)
                path.quadTo(p[0], p[1], p[2], p[3]);
            else if (piece.kind == 'C' && p.size() >= 6)
                path.cubicTo(p[0], p[1], p[2], p[3], p[4], p[5]);
        }
        QPen pen(QColor::fromRgb(preview.color & 0xFFFFFF));
        pen.setWidthF(scale);
        pen.setCosmetic(false);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    }
}

bool ScriptTool::hasOptions() const
{
    return !_script->options().empty();
}

void ScriptTool::showOptions(QWidget* parent)
{
    const std::vector<jsfl::ToolScript::Option> options = _script->options();
    if (options.empty())
        return;
    QDialog dialog(parent);
    dialog.setWindowTitle("Tool Settings");
    auto* form = new QFormLayout(&dialog);
    std::vector<std::function<std::pair<std::string, std::string>()>> readers;
    for (const jsfl::ToolScript::Option& option : options)
    {
        const QString label = QString::fromStdString(option.name.empty() ? option.variable : option.name) + ":";
        const std::string variable = option.variable;
        if (option.type == "Strings" || !option.list.empty())
        {
            auto* box = new QComboBox(&dialog);
            for (const std::string& item : option.list)
                box->addItem(QString::fromStdString(item));
            box->setCurrentIndex(std::max(0, std::atoi(option.value.c_str())));
            form->addRow(label, box);
            readers.push_back([box, variable]() { return std::make_pair(variable, std::to_string(box->currentIndex())); });
        }
        else if (option.type == "Boolean")
        {
            auto* box = new QCheckBox(&dialog);
            box->setChecked(option.value == "true" || option.value == "1");
            form->addRow(label, box);
            readers.push_back([box, variable]() { return std::make_pair(variable, std::string(box->isChecked() ? "true" : "false")); });
        }
        else if (option.type == "Number")
        {
            auto* box = new QDoubleSpinBox(&dialog);
            // Whole numbers stay whole (a count of sides); a fraction gets
            // two places.
            const bool whole = option.value.find('.') == std::string::npos;
            box->setDecimals(whole ? 0 : 2);
            box->setSingleStep(whole ? 1.0 : 0.05);
            box->setRange(option.min, option.max);
            box->setValue(std::atof(option.value.c_str()));
            form->addRow(label, box);
            readers.push_back([box, variable]() { return std::make_pair(variable, QString::number(box->value()).toStdString()); });
        }
        else
        {
            auto* edit = new QLineEdit(QString::fromStdString(option.value), &dialog);
            form->addRow(label, edit);
            readers.push_back([edit, variable]() { return std::make_pair(variable, edit->text().toStdString()); });
        }
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    std::vector<std::pair<std::string, std::string>> values;
    for (const auto& read : readers)
        values.push_back(read());
    const std::string error = _script->setOptions(values);
    if (!error.empty())
        _host.trace(error);
}
