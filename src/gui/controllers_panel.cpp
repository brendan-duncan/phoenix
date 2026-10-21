#include "controllers_panel.h"
#include "modal.h"

#include "../data/document.h"
#include "../data/layer.h"
#include "../edit/command_stack.h"
#include "../edit/controllers.h"
#include "../edit/property_command.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include "hot_text.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QGuiApplication>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

/// A joystick's square: drag the handle about it, or click to put it
/// there. Across is 0 at the left to 1 at the right, down 0 at the top to
/// 1 at the bottom.
class JoystickPad : public QWidget
{
public:
    explicit JoystickPad(QWidget* parent)
        : QWidget(parent)
    {
        setMinimumSize(120, 120);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setCursor(Qt::CrossCursor);
        setToolTip("Drag the handle to set what the joystick drives on the frame shown; double-click to center it");
    }

    void setPosition(double x, double y)
    {
        _x = std::clamp(x, 0.0, 1.0);
        _y = std::clamp(y, 0.0, 1.0);
        update();
    }
    double x() const { return _x; }
    double y() const { return _y; }

    std::function<void()> pressed;
    std::function<void(double, double)> moved;
    std::function<void(double, double)> released;

    QSize sizeHint() const override { return QSize(160, 160); }
    int heightForWidth(int width) const override { return std::min(width, 220); }
    bool hasHeightForWidth() const override { return true; }

protected:
    QRectF square() const
    {
        const double side = std::max(20, std::min(width(), height()) - 8);
        return QRectF((width() - side) / 2.0, (height() - side) / 2.0, side, side);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF box = square();
        const QPalette& colors = palette();
        painter.setPen(QPen(colors.color(QPalette::Mid), 1));
        painter.setBrush(colors.color(QPalette::Base));
        painter.drawRoundedRect(box, 4, 4);
        // Quarters, and the middle a little stronger.
        QColor line = colors.color(QPalette::Mid);
        line.setAlpha(110);
        painter.setPen(QPen(line, 1, Qt::DotLine));
        for (const double f : {0.25, 0.75})
        {
            painter.drawLine(QPointF(box.left() + box.width() * f, box.top()), QPointF(box.left() + box.width() * f, box.bottom()));
            painter.drawLine(QPointF(box.left(), box.top() + box.height() * f), QPointF(box.right(), box.top() + box.height() * f));
        }
        painter.setPen(QPen(colors.color(QPalette::Mid), 1));
        painter.drawLine(QPointF(box.center().x(), box.top()), QPointF(box.center().x(), box.bottom()));
        painter.drawLine(QPointF(box.left(), box.center().y()), QPointF(box.right(), box.center().y()));
        if (!isEnabled())
            return;
        const QPointF handle(box.left() + box.width() * _x, box.top() + box.height() * _y);
        QColor accent = colors.color(QPalette::Highlight);
        painter.setPen(QPen(accent, 1.5));
        painter.drawLine(box.center(), handle);
        painter.setBrush(accent);
        painter.setPen(QPen(colors.color(QPalette::Base), 2));
        painter.drawEllipse(handle, 7, 7);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton)
            return;
        _dragging = true;
        if (pressed)
            pressed();
        follow(event->position());
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (_dragging)
            follow(event->position());
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (!_dragging || event->button() != Qt::LeftButton)
            return;
        _dragging = false;
        if (released)
            released(_x, _y);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton)
            return;
        if (pressed)
            pressed();
        setPosition(0.5, 0.5);
        if (moved)
            moved(_x, _y);
        if (released)
            released(_x, _y);
    }

private:
    void follow(const QPointF& at)
    {
        const QRectF box = square();
        double x = (at.x() - box.left()) / box.width();
        double y = (at.y() - box.top()) / box.height();
        // Shift keeps to the nearest quarter.
        if (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier)
        {
            x = std::round(x * 4.0) / 4.0;
            y = std::round(y * 4.0) / 4.0;
        }
        x = std::clamp(x, 0.0, 1.0);
        y = std::clamp(y, 0.0, 1.0);
        if (x == _x && y == _y)
            return;
        setPosition(x, y);
        if (moved)
            moved(_x, _y);
    }

    double _x = 0.5;
    double _y = 0.5;
    bool _dragging = false;
};

namespace {

constexpr int ControllerRole = Qt::UserRole;
constexpr int BindingRole = Qt::UserRole + 1;

/// Each property as the panel names it.
QString propertyLabel(const std::string& property)
{
    if (property == "x") return "X";
    if (property == "y") return "Y";
    if (property == "rotation") return "Rotation";
    if (property == "scale") return "Scale";
    if (property == "scaleX") return "Scale X";
    if (property == "scaleY") return "Scale Y";
    if (property == "alpha") return "Alpha";
    if (property == "frame") return "Frame (graphic)";
    if (property == "frameGrid") return "Frame grid (graphic)";
    return QString::fromStdString(property);
}

/// What a binding drives, for the list: "X: 0 → 40", "across, Rotation...".
QString bindingText(const fla::controllers::Controller& controller, const fla::controllers::Binding& binding)
{
    if (binding.property == "frameGrid")
        return QString("%1: frames %2 to %3, %4 a row")
            .arg(propertyLabel(binding.property))
            .arg(binding.from, 0, 'g', 6)
            .arg(binding.to, 0, 'g', 6)
            .arg(binding.columns);
    QString text = QString("%1: %2 → %3")
                       .arg(propertyLabel(binding.property))
                       .arg(binding.from, 0, 'g', 6)
                       .arg(binding.to, 0, 'g', 6);
    if (controller.joystick)
        text = (binding.axis == 'y' ? "Down, " : "Across, ") + text;
    return text;
}

using Attributes = std::vector<std::pair<std::string, std::string>>;

} // namespace

ControllersPanel::ControllersPanel(ControllersLinks links, QWidget* parent)
    : QWidget(parent)
    , _links(std::move(links))
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    _list = new QTreeWidget(this);
    _list->setHeaderLabels({"Controller", "Drives"});
    _list->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    _list->header()->setStretchLastSection(true);
    layout->addWidget(_list, 1);

    _sliderRow = new QWidget(this);
    QHBoxLayout* sliderRow = new QHBoxLayout(_sliderRow);
    sliderRow->setContentsMargins(0, 0, 0, 0);
    _slider = new QSlider(Qt::Horizontal, _sliderRow);
    _slider->setRange(0, 1000);
    _slider->setToolTip("Drag to set what the controller drives on the frame shown");
    _value = new QLabel("0.00", this);
    _value->setMinimumWidth(40);
    sliderRow->addWidget(_slider, 1);
    layout->addWidget(_sliderRow);
    _pad = new JoystickPad(this);
    _pad->hide();
    layout->addWidget(_pad);
    sliderRow->addWidget(_value);

    QHBoxLayout* buttons = new QHBoxLayout();
    QPushButton* add = new QPushButton("New", this);
    add->setToolTip("A new slider or joystick");
    QMenu* kinds = new QMenu(add);
    connect(kinds->addAction("Slider..."), &QAction::triggered, this, [this]() { addController(false); });
    connect(kinds->addAction("Joystick..."), &QAction::triggered, this, [this]() { addController(true); });
    add->setMenu(kinds);
    QPushButton* bind = new QPushButton("Bind...", this);
    bind->setToolTip("Have the selected controller drive a property of the current layer's instances");
    QPushButton* remove = new QPushButton("Remove", this);
    remove->setToolTip("Take out the selected binding or controller");
    buttons->addWidget(add);
    buttons->addWidget(bind);
    buttons->addStretch(1);
    buttons->addWidget(remove);
    layout->addLayout(buttons);

    connect(bind, &QPushButton::clicked, this, &ControllersPanel::bind);
    connect(remove, &QPushButton::clicked, this, &ControllersPanel::removeSelected);
    connect(_list, &QTreeWidget::currentItemChanged, this, [this]() {
        if (_refreshing)
            return;
        fla::Document* document = _links.document ? _links.document() : nullptr;
        const int index = currentController();
        const std::vector<fla::controllers::Controller> list = document ? fla::controllers::load(*document)
                                                                         : std::vector<fla::controllers::Controller>();
        const bool valid = index >= 0 && index < static_cast<int>(list.size());
        const bool joystick = valid && list[static_cast<size_t>(index)].joystick;
        _sliderRow->setVisible(!joystick);
        _pad->setVisible(joystick);
        _slider->setEnabled(valid);
        _pad->setEnabled(valid);
        const QSignalBlocker blocker(_slider);
        _slider->setValue(valid ? static_cast<int>(std::lround(list[static_cast<size_t>(index)].value * 1000.0)) : 0);
        if (joystick)
            _pad->setPosition(list[static_cast<size_t>(index)].value, list[static_cast<size_t>(index)].valueY);
        _value->setText(QString::number(_slider->value() / 1000.0, 'f', 2));
    });
    connect(_slider, &QSlider::sliderPressed, this, &ControllersPanel::startDrag);
    connect(_slider, &QSlider::valueChanged, this, &ControllersPanel::moved);
    connect(_slider, &QSlider::sliderReleased, this, [this]() { endDrag(_slider->value() / 1000.0, 0.5); });
    _pad->pressed = [this]() { startDrag(); };
    _pad->moved = [this](double x, double y) { moveTo(x, y); };
    _pad->released = [this](double x, double y) { endDrag(x, y); };
    refresh();
}

void ControllersPanel::startDrag()
{
    const int index = currentController();
    fla::CommandStack* commands = _links.commands ? _links.commands() : nullptr;
    if (index < 0 || !commands || _dragging)
        return;
    // A drag is one undo step.
    commands->beginMacro("Controller");
    _dragging = true;
}

void ControllersPanel::endDrag(double x, double y)
{
    fla::CommandStack* commands = _links.commands ? _links.commands() : nullptr;
    fla::Document* document = _links.document ? _links.document() : nullptr;
    const int index = currentController();
    if (_dragging && commands && document && index >= 0)
    {
        // The controller remembers where it was left.
        std::vector<fla::controllers::Controller> list = fla::controllers::load(*document);
        if (index < static_cast<int>(list.size()))
        {
            fla::controllers::Controller& controller = list[static_cast<size_t>(index)];
            controller.value = x;
            if (controller.joystick)
                controller.valueY = y;
            commands->push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(document->extraAttributes,
                document->extraAttributes, fla::controllers::saved(*document, list), "Controller")));
        }
        commands->endMacro();
    }
    _dragging = false;
}

int ControllersPanel::currentController() const
{
    const QTreeWidgetItem* item = _list->currentItem();
    return item ? item->data(0, ControllerRole).toInt() : -1;
}

void ControllersPanel::refresh()
{
    fla::Document* document = _links.document ? _links.document() : nullptr;
    const int keep = currentController();
    _refreshing = true;
    _list->clear();
    if (document)
    {
        const std::vector<fla::controllers::Controller> list = fla::controllers::load(*document);
        for (size_t c = 0; c < list.size(); ++c)
        {
            const fla::controllers::Controller& controller = list[c];
            QTreeWidgetItem* item = new QTreeWidgetItem(_list);
            item->setText(0, QString::fromStdString(controller.name));
            item->setText(1, QString("%1%2 propert%3")
                                 .arg(controller.joystick ? "Joystick, " : "")
                                 .arg(controller.bindings.size())
                                 .arg(controller.bindings.size() == 1 ? "y" : "ies"));
            item->setData(0, ControllerRole, static_cast<int>(c));
            item->setData(0, BindingRole, -1);
            for (size_t b = 0; b < controller.bindings.size(); ++b)
            {
                const fla::controllers::Binding& binding = controller.bindings[b];
                QTreeWidgetItem* child = new QTreeWidgetItem(item);
                child->setText(0, QString::fromStdString(binding.layer));
                child->setText(1, bindingText(controller, binding));
                child->setData(0, ControllerRole, static_cast<int>(c));
                child->setData(0, BindingRole, static_cast<int>(b));
            }
            item->setExpanded(true);
            if (static_cast<int>(c) == keep)
                _list->setCurrentItem(item);
        }
    }
    _refreshing = false;
    if (!_list->currentItem() && _list->topLevelItemCount() > 0)
        _list->setCurrentItem(_list->topLevelItem(0));
    else
        emit _list->currentItemChanged(_list->currentItem(), nullptr);
}

void ControllersPanel::addController(bool joystick)
{
    fla::Document* document = _links.document ? _links.document() : nullptr;
    fla::CommandStack* commands = _links.commands ? _links.commands() : nullptr;
    if (!document || !commands)
        return;
    const size_t count = fla::controllers::load(*document).size();
    modal::getText(this, joystick ? "New Joystick" : "New Controller", "Name:",
        QString(joystick ? "Joystick %1" : "Controller %1").arg(count + 1), [this, document, joystick](const QString& text) {
            const QString name = text.trimmed();
            if (name.isEmpty())
                return;
            // The document may have changed while the name was asked for.
            fla::CommandStack* commands = _links.commands ? _links.commands() : nullptr;
            if (!commands || !_links.document || _links.document() != document)
                return;
            std::vector<fla::controllers::Controller> list = fla::controllers::load(*document);
            fla::controllers::Controller controller;
            controller.name = name.toStdString();
            controller.joystick = joystick;
            if (joystick)
                controller.value = 0.5;
            list.push_back(controller);
            commands->push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(document->extraAttributes,
                document->extraAttributes, fla::controllers::saved(*document, list),
                joystick ? "New Joystick" : "New Controller")));
            refresh();
            _list->setCurrentItem(_list->topLevelItem(_list->topLevelItemCount() - 1));
        });
}

void ControllersPanel::bind()
{
    fla::Document* document = _links.document ? _links.document() : nullptr;
    fla::CommandStack* commands = _links.commands ? _links.commands() : nullptr;
    const int index = currentController();
    fla::Timeline* timeline = nullptr;
    fla::Layer* layer = _links.currentLayer ? _links.currentLayer(timeline) : nullptr;
    if (!document || !commands || index < 0)
    {
        modal::information(this, "Bind", "Make or pick a controller first.");
        return;
    }
    if (!layer || !timeline)
    {
        modal::information(this, "Bind", "Pick the layer to drive in the timeline first.");
        return;
    }
    const int frame = _links.currentFrame ? _links.currentFrame() : 0;
    const std::vector<fla::controllers::Controller> controllers = fla::controllers::load(*document);
    const bool joystick = index < static_cast<int>(controllers.size()) && controllers[static_cast<size_t>(index)].joystick;

    QDialog* dialog = new QDialog(this);
    dialog->setWindowTitle(joystick ? "Bind to Joystick" : "Bind to Controller");
    QFormLayout* form = new QFormLayout(dialog);
    form->addRow("Layer:", new QLabel(QString::fromStdString(layer->name), dialog));
    QComboBox* property = new QComboBox(dialog);
    for (const std::string& name : joystick ? fla::controllers::joystickProperties() : fla::controllers::properties())
        property->addItem(propertyLabel(name), QString::fromStdString(name));
    form->addRow("Property:", property);
    QComboBox* axis = new QComboBox(dialog);
    axis->addItem("Across (left to right)", QString("x"));
    axis->addItem("Down (top to bottom)", QString("y"));
    axis->setToolTip("Which way of moving the handle drives the property");
    if (joystick)
        form->addRow("Axis:", axis);
    else
        axis->hide();
    HotDoubleSpinBox* from = new HotDoubleSpinBox(dialog);
    HotDoubleSpinBox* to = new HotDoubleSpinBox(dialog);
    for (HotDoubleSpinBox* box : {from, to})
    {
        box->setRange(-100000.0, 100000.0);
        box->setDecimals(2);
    }
    QSpinBox* columns = new QSpinBox(dialog);
    columns->setRange(1, 1000);
    columns->setValue(3);
    columns->setToolTip("How many drawings each row of the grid has: the handle goes across a row, and down from row to row");
    QLabel* fromLabel = new QLabel(dialog);
    QLabel* toLabel = new QLabel(dialog);
    QLabel* columnsLabel = new QLabel("Drawings a row:", dialog);
    form->addRow(fromLabel, from);
    form->addRow(toLabel, to);
    form->addRow(columnsLabel, columns);
    // The labels say where each value is: at the ends of the slider, at
    // the sides of the square, or the frames of a grid.
    const auto label = [joystick, property, axis, from, to, fromLabel, toLabel, columns, columnsLabel]() {
        const bool grid = property->currentData().toString() == "frameGrid";
        const bool down = axis->currentData().toString() == "y";
        axis->setEnabled(!grid);
        columns->setVisible(grid);
        columnsLabel->setVisible(grid);
        fromLabel->setText(grid ? "First frame:" : !joystick ? "At 0:" : down ? "At the top:" : "At the left:");
        toLabel->setText(grid ? "Last frame:" : !joystick ? "At 1:" : down ? "At the bottom:" : "At the right:");
        from->setToolTip(grid ? "The graphic's frame at the top left of the grid" : "The value with the controller at its start");
        to->setToolTip(grid ? "The graphic's frame at the bottom right of the grid" : "The value with the controller at its end");
    };
    // Both start at the value it has now.
    const auto fill = [layer, frame, property, from, to, label]() {
        label();
        double now = 0.0;
        if (fla::controllers::currentValue(*layer, frame, property->currentData().toString().toStdString(), now))
        {
            from->setValue(now);
            to->setValue(now);
        }
    };
    fill();
    connect(property, &QComboBox::currentIndexChanged, dialog, fill);
    connect(axis, &QComboBox::currentIndexChanged, dialog, label);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    modal::open(dialog, [this, document, commands, index, timeline, layer, property, axis, from, to, columns](int result) {
        if (result != QDialog::Accepted)
            return;
        // The document may have changed while the dialog was up.
        if (!_links.document || _links.document() != document || !_links.commands || _links.commands() != commands)
            return;

        std::vector<fla::controllers::Controller> list = fla::controllers::load(*document);
        if (index >= static_cast<int>(list.size()))
            return;
        fla::controllers::Binding binding;
        binding.timeline = fla::controllers::nameOf(*document, timeline);
        binding.layer = layer->name;
        binding.property = property->currentData().toString().toStdString();
        binding.from = from->value();
        binding.to = to->value();
        binding.axis = axis->currentData().toString() == "y" ? 'y' : 'x';
        if (binding.property == "frameGrid")
            binding.columns = columns->value();
        list[static_cast<size_t>(index)].bindings.push_back(binding);
        commands->push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(document->extraAttributes,
            document->extraAttributes, fla::controllers::saved(*document, list), "Bind to Controller")));
        refresh();
    });
}

void ControllersPanel::removeSelected()
{
    fla::Document* document = _links.document ? _links.document() : nullptr;
    fla::CommandStack* commands = _links.commands ? _links.commands() : nullptr;
    const QTreeWidgetItem* item = _list->currentItem();
    if (!document || !commands || !item)
        return;
    std::vector<fla::controllers::Controller> list = fla::controllers::load(*document);
    const int controller = item->data(0, ControllerRole).toInt();
    const int binding = item->data(0, BindingRole).toInt();
    if (controller < 0 || controller >= static_cast<int>(list.size()))
        return;
    if (binding >= 0)
    {
        auto& bindings = list[static_cast<size_t>(controller)].bindings;
        if (binding < static_cast<int>(bindings.size()))
            bindings.erase(bindings.begin() + binding);
    }
    else
    {
        list.erase(list.begin() + controller);
    }
    commands->push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(document->extraAttributes,
        document->extraAttributes, fla::controllers::saved(*document, list), "Remove from Controllers")));
    refresh();
}

void ControllersPanel::moved(int position)
{
    _value->setText(QString::number(position / 1000.0, 'f', 2));
    moveTo(position / 1000.0, 0.5);
}

void ControllersPanel::moveTo(double x, double y)
{
    fla::Document* document = _links.document ? _links.document() : nullptr;
    fla::CommandStack* commands = _links.commands ? _links.commands() : nullptr;
    const int index = currentController();
    if (!document || !commands || index < 0)
        return;
    const std::vector<fla::controllers::Controller> list = fla::controllers::load(*document);
    if (index >= static_cast<int>(list.size()))
        return;
    // A click or a key moves it without a drag: one step of its own.
    const bool alone = !_dragging;
    if (alone)
        commands->beginMacro("Controller");
    if (fla::CommandPtr command = fla::controllers::apply(*document, list[static_cast<size_t>(index)], x, y,
            _links.currentFrame ? _links.currentFrame() : 0))
        commands->push(std::move(command));
    if (alone)
    {
        std::vector<fla::controllers::Controller> updated = list;
        updated[static_cast<size_t>(index)].value = x;
        if (updated[static_cast<size_t>(index)].joystick)
            updated[static_cast<size_t>(index)].valueY = y;
        commands->push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(document->extraAttributes,
            document->extraAttributes, fla::controllers::saved(*document, updated), "Controller")));
        commands->endMacro();
    }
    if (_links.changed)
        _links.changed();
}
