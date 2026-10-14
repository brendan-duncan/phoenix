#include "controllers_panel.h"

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
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <cmath>

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
    return QString::fromStdString(property);
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

    QHBoxLayout* sliderRow = new QHBoxLayout();
    _slider = new QSlider(Qt::Horizontal, this);
    _slider->setRange(0, 1000);
    _slider->setToolTip("Drag to set what the controller drives on the frame shown");
    _value = new QLabel("0.00", this);
    _value->setMinimumWidth(40);
    sliderRow->addWidget(_slider, 1);
    sliderRow->addWidget(_value);
    layout->addLayout(sliderRow);

    QHBoxLayout* buttons = new QHBoxLayout();
    QPushButton* add = new QPushButton("New...", this);
    add->setToolTip("A new controller");
    QPushButton* bind = new QPushButton("Bind...", this);
    bind->setToolTip("Have the selected controller drive a property of the current layer's instances");
    QPushButton* remove = new QPushButton("Remove", this);
    remove->setToolTip("Take out the selected binding or controller");
    buttons->addWidget(add);
    buttons->addWidget(bind);
    buttons->addStretch(1);
    buttons->addWidget(remove);
    layout->addLayout(buttons);

    connect(add, &QPushButton::clicked, this, &ControllersPanel::addController);
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
        _slider->setEnabled(valid);
        const QSignalBlocker blocker(_slider);
        _slider->setValue(valid ? static_cast<int>(std::lround(list[static_cast<size_t>(index)].value * 1000.0)) : 0);
        _value->setText(QString::number(_slider->value() / 1000.0, 'f', 2));
    });
    connect(_slider, &QSlider::sliderPressed, this, [this]() {
        const int index = currentController();
        fla::CommandStack* commands = _links.commands ? _links.commands() : nullptr;
        if (index < 0 || !commands)
            return;
        // A drag is one undo step.
        commands->beginMacro("Controller");
        _dragging = true;
    });
    connect(_slider, &QSlider::valueChanged, this, &ControllersPanel::moved);
    connect(_slider, &QSlider::sliderReleased, this, [this]() {
        fla::CommandStack* commands = _links.commands ? _links.commands() : nullptr;
        fla::Document* document = _links.document ? _links.document() : nullptr;
        const int index = currentController();
        if (_dragging && commands && document && index >= 0)
        {
            // The controller remembers where it was left.
            std::vector<fla::controllers::Controller> list = fla::controllers::load(*document);
            if (index < static_cast<int>(list.size()))
            {
                list[static_cast<size_t>(index)].value = _slider->value() / 1000.0;
                commands->push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(document->extraAttributes,
                    document->extraAttributes, fla::controllers::saved(*document, list), "Controller")));
            }
            commands->endMacro();
        }
        _dragging = false;
    });
    refresh();
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
            item->setText(1, QString("%1 propert%2").arg(controller.bindings.size()).arg(controller.bindings.size() == 1 ? "y" : "ies"));
            item->setData(0, ControllerRole, static_cast<int>(c));
            item->setData(0, BindingRole, -1);
            for (size_t b = 0; b < controller.bindings.size(); ++b)
            {
                const fla::controllers::Binding& binding = controller.bindings[b];
                QTreeWidgetItem* child = new QTreeWidgetItem(item);
                child->setText(0, QString::fromStdString(binding.layer));
                child->setText(1, QString("%1: %2 → %3")
                                      .arg(propertyLabel(binding.property))
                                      .arg(binding.from, 0, 'g', 6)
                                      .arg(binding.to, 0, 'g', 6));
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

void ControllersPanel::addController()
{
    fla::Document* document = _links.document ? _links.document() : nullptr;
    fla::CommandStack* commands = _links.commands ? _links.commands() : nullptr;
    if (!document || !commands)
        return;
    std::vector<fla::controllers::Controller> list = fla::controllers::load(*document);
    bool ok = false;
    const QString name = QInputDialog::getText(this, "New Controller", "Name:", QLineEdit::Normal,
        QString("Controller %1").arg(list.size() + 1), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    fla::controllers::Controller controller;
    controller.name = name.toStdString();
    list.push_back(controller);
    commands->push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(document->extraAttributes,
        document->extraAttributes, fla::controllers::saved(*document, list), "New Controller")));
    refresh();
    _list->setCurrentItem(_list->topLevelItem(_list->topLevelItemCount() - 1));
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
        QMessageBox::information(this, "Bind", "Make or pick a controller first.");
        return;
    }
    if (!layer || !timeline)
    {
        QMessageBox::information(this, "Bind", "Pick the layer to drive in the timeline first.");
        return;
    }
    const int frame = _links.currentFrame ? _links.currentFrame() : 0;

    QDialog dialog(this);
    dialog.setWindowTitle("Bind to Controller");
    QFormLayout* form = new QFormLayout(&dialog);
    form->addRow("Layer:", new QLabel(QString::fromStdString(layer->name), &dialog));
    QComboBox* property = new QComboBox(&dialog);
    for (const std::string& name : fla::controllers::properties())
        property->addItem(propertyLabel(name), QString::fromStdString(name));
    form->addRow("Property:", property);
    HotDoubleSpinBox* from = new HotDoubleSpinBox(&dialog);
    HotDoubleSpinBox* to = new HotDoubleSpinBox(&dialog);
    for (HotDoubleSpinBox* box : {from, to})
    {
        box->setRange(-100000.0, 100000.0);
        box->setDecimals(2);
    }
    from->setToolTip("The value with the controller at 0");
    to->setToolTip("The value with the controller at 1");
    form->addRow("At 0:", from);
    form->addRow("At 1:", to);
    // Both start at the value it has now.
    const auto fill = [&]() {
        double now = 0.0;
        if (fla::controllers::currentValue(*layer, frame, property->currentData().toString().toStdString(), now))
        {
            from->setValue(now);
            to->setValue(now);
        }
    };
    fill();
    connect(property, &QComboBox::currentIndexChanged, &dialog, fill);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
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
    list[static_cast<size_t>(index)].bindings.push_back(binding);
    commands->push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(document->extraAttributes,
        document->extraAttributes, fla::controllers::saved(*document, list), "Bind to Controller")));
    refresh();
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
    if (fla::CommandPtr command = fla::controllers::apply(*document, list[static_cast<size_t>(index)], position / 1000.0,
            _links.currentFrame ? _links.currentFrame() : 0))
        commands->push(std::move(command));
    if (alone)
    {
        std::vector<fla::controllers::Controller> updated = list;
        updated[static_cast<size_t>(index)].value = position / 1000.0;
        commands->push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(document->extraAttributes,
            document->extraAttributes, fla::controllers::saved(*document, updated), "Controller")));
        commands->endMacro();
    }
    if (_links.changed)
        _links.changed();
}
