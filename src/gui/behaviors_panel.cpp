#include "behaviors_panel.h"

#include "behavior_library.h"

#include "../data/symbol_instance.h"
#include "../edit/command_stack.h"
#include "../edit/property_command.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

using Attributes = std::vector<std::pair<std::string, std::string>>;

/// Folds a run of changes to the same setting into one step.
constexpr int kMergeSetting = 7301;

std::string numberText(double value)
{
    return QString::number(value, 'g', 10).toStdString();
}

int newSeed()
{
    return static_cast<int>(QRandomGenerator::global()->bounded(1, 10000));
}

} // namespace

BehaviorsPanel::BehaviorsPanel(BehaviorsLinks links, QWidget* parent)
    : QWidget(parent)
    , _links(std::move(links))
{
    _what = new QLabel(this);
    _what->setWordWrap(true);

    _list = new QListWidget(this);
    _list->setMaximumHeight(130);
    _list->setToolTip("The behaviors on the selection, applied top to bottom; the box turns one on or off");

    _add = new QToolButton(this);
    _add->setText("Add Behavior");
    _add->setToolButtonStyle(Qt::ToolButtonTextOnly);
    _add->setPopupMode(QToolButton::InstantPopup);
    QMenu* menu = new QMenu(_add);
    connect(menu, &QMenu::aboutToShow, this, [this, menu]() {
        menu->clear();
        for (const fla::behaviors::Definition& definition : BehaviorLibrary::instance().engine().definitions())
        {
            QAction* action = menu->addAction(QString::fromStdString(definition.name));
            action->setToolTip(QString::fromStdString(definition.description));
            action->setStatusTip(QString::fromStdString(definition.description));
            const std::string id = definition.id;
            connect(action, &QAction::triggered, this, [this, id]() { add(id); });
        }
        menu->setToolTipsVisible(true);
    });
    _add->setMenu(menu);
    _up = new QToolButton(this);
    _up->setArrowType(Qt::UpArrow);
    _up->setToolTip("Apply it earlier");
    _down = new QToolButton(this);
    _down->setArrowType(Qt::DownArrow);
    _down->setToolTip("Apply it later");
    _remove = new QToolButton(this);
    _remove->setText("Remove");
    _remove->setToolTip("Take the behavior off the selection");
    connect(_up, &QToolButton::clicked, this, [this]() { move(-1); });
    connect(_down, &QToolButton::clicked, this, [this]() { move(1); });
    connect(_remove, &QToolButton::clicked, this, &BehaviorsPanel::remove);

    QHBoxLayout* buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->addWidget(_add);
    buttons->addStretch();
    buttons->addWidget(_up);
    buttons->addWidget(_down);
    buttons->addWidget(_remove);

    _description = new QLabel(this);
    _description->setWordWrap(true);
    _description->setStyleSheet("color: palette(placeholder-text);");

    _settings = new QWidget(this);
    _form = new QFormLayout(_settings);
    _form->setContentsMargins(0, 0, 0, 0);
    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(_settings);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addWidget(_what);
    layout->addWidget(_list);
    layout->addLayout(buttons);
    layout->addWidget(_description);
    layout->addWidget(scroll, 1);

    connect(_list, &QListWidget::currentRowChanged, this, [this](int) {
        if (!_refreshing)
            fillSettings();
    });
    connect(_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (_refreshing)
            return;
        const int row = _list->row(item);
        const bool on = item->checkState() == Qt::Checked;
        edit(on ? "Turn Behavior On" : "Turn Behavior Off", [row, on](std::vector<fla::behaviors::Applied>& list) {
            if (row >= 0 && row < static_cast<int>(list.size()))
                list[static_cast<size_t>(row)].enabled = on;
        });
    });
    connect(&BehaviorLibrary::instance(), &BehaviorLibrary::changed, this, &BehaviorsPanel::refresh);
    refresh();
}

std::vector<fla::behaviors::Applied> BehaviorsPanel::shown() const
{
    const std::vector<fla::SymbolInstance*> instances = _links.instances ? _links.instances() : std::vector<fla::SymbolInstance*>();
    return instances.empty() ? std::vector<fla::behaviors::Applied>() : fla::behaviors::of(*instances.front());
}

void BehaviorsPanel::refresh()
{
    if (_refreshing)
        return;
    _refreshing = true;
    const std::vector<fla::SymbolInstance*> instances = _links.instances ? _links.instances() : std::vector<fla::SymbolInstance*>();
    const int keep = _list->currentRow();
    _list->clear();
    if (instances.empty())
        _what->setText("Select a symbol instance to give it behaviors: motion worked out every frame, on top of its keys.");
    else if (instances.size() == 1)
        _what->setText(QString("Behaviors of %1").arg(QString::fromStdString(instances.front()->libraryItemName)));
    else
        _what->setText(QString("Behaviors of %1 instances (shown: the first one's)").arg(instances.size()));
    const fla::behaviors::Engine& engine = BehaviorLibrary::instance().engine();
    for (const fla::behaviors::Applied& applied : shown())
    {
        const fla::behaviors::Definition* definition = engine.find(applied.type);
        QListWidgetItem* item = new QListWidgetItem(
            definition ? QString::fromStdString(definition->name) : QString("%1 (missing)").arg(QString::fromStdString(applied.type)),
            _list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(applied.enabled ? Qt::Checked : Qt::Unchecked);
        if (!definition)
            item->setToolTip("No behavior of this name is known: its file may have been taken out of the Behaviors folder");
    }
    _add->setEnabled(!instances.empty());
    if (_list->count() > 0)
        _list->setCurrentRow(std::clamp(keep, 0, _list->count() - 1));
    const bool any = _list->count() > 0;
    _up->setEnabled(any);
    _down->setEnabled(any);
    _remove->setEnabled(any);
    _refreshing = false;
    fillSettings();
}

void BehaviorsPanel::fillSettings()
{
    while (QLayoutItem* item = _form->takeAt(0))
    {
        delete item->widget();
        delete item;
    }
    const std::vector<fla::behaviors::Applied> list = shown();
    const int row = _list->currentRow();
    if (row < 0 || row >= static_cast<int>(list.size()))
    {
        _description->clear();
        return;
    }
    const fla::behaviors::Applied& applied = list[static_cast<size_t>(row)];
    const fla::behaviors::Definition* definition = BehaviorLibrary::instance().engine().find(applied.type);
    if (!definition)
    {
        _description->setText("This behavior's file is missing; its settings are kept.");
        return;
    }
    _description->setText(QString::fromStdString(definition->description));
    for (const fla::behaviors::ParamSpec& spec : definition->params)
    {
        const auto found = applied.params.find(spec.name);
        const std::string current = found != applied.params.end() ? found->second : spec.text;
        const QString label = QString::fromStdString(spec.label);
        const std::string name = spec.name;
        if (spec.type == "checkbox")
        {
            QCheckBox* box = new QCheckBox(_settings);
            box->setChecked(current == "true" || current == "1");
            connect(box, &QCheckBox::toggled, this,
                [this, row, name](bool on) { setParam(row, name, on ? "true" : "false", false); });
            _form->addRow(label, box);
        }
        else if (spec.type == "choice")
        {
            QComboBox* combo = new QComboBox(_settings);
            for (const std::string& choice : spec.choices)
                combo->addItem(QString::fromStdString(choice));
            combo->setCurrentText(QString::fromStdString(current));
            connect(combo, &QComboBox::currentTextChanged, this,
                [this, row, name](const QString& text) { setParam(row, name, text.toStdString(), false); });
            _form->addRow(label, combo);
        }
        else if (spec.type == "seed")
        {
            QWidget* holder = new QWidget(_settings);
            QHBoxLayout* line = new QHBoxLayout(holder);
            line->setContentsMargins(0, 0, 0, 0);
            QSpinBox* seed = new QSpinBox(holder);
            seed->setRange(0, 9999);
            seed->setValue(static_cast<int>(std::lround(std::atof(current.c_str()))));
            seed->setKeyboardTracking(false);
            seed->setToolTip("Another seed gives another, equally random, result");
            QPushButton* again = new QPushButton("New", holder);
            again->setToolTip("A new seed");
            line->addWidget(seed, 1);
            line->addWidget(again);
            connect(seed, &QSpinBox::valueChanged, this,
                [this, row, name](int value) { setParam(row, name, std::to_string(value), false); });
            connect(again, &QPushButton::clicked, seed, [seed]() { seed->setValue(newSeed()); });
            _form->addRow(label, holder);
        }
        else
        {
            // A number: a slider over its range, and the box to type in.
            QWidget* holder = new QWidget(_settings);
            QHBoxLayout* line = new QHBoxLayout(holder);
            line->setContentsMargins(0, 0, 0, 0);
            const double step = spec.step > 0.0 ? spec.step : 1.0;
            const double low = std::min(spec.min, spec.max);
            const double high = std::max(spec.min, spec.max);
            const int steps = static_cast<int>(std::lround((high - low) / step));
            QSlider* slider = new QSlider(Qt::Horizontal, holder);
            slider->setRange(0, std::max(1, steps));
            QDoubleSpinBox* box = new QDoubleSpinBox(holder);
            const int decimals = step >= 1.0 ? 0 : step >= 0.1 ? 1 : step >= 0.01 ? 2 : 3;
            box->setDecimals(decimals);
            box->setRange(low, high);
            box->setSingleStep(step);
            box->setKeyboardTracking(false);
            const double value = current.empty() ? spec.value : std::atof(current.c_str());
            box->setValue(value);
            slider->setValue(static_cast<int>(std::lround((value - low) / step)));
            line->addWidget(slider, 1);
            line->addWidget(box);
            connect(slider, &QSlider::valueChanged, this, [this, row, name, box, low, step](int position) {
                const double v = low + position * step;
                const QSignalBlocker quiet(box);
                box->setValue(v);
                setParam(row, name, numberText(v), true);
            });
            connect(box, &QDoubleSpinBox::valueChanged, this, [this, row, name, slider, low, step](double v) {
                const QSignalBlocker quiet(slider);
                slider->setValue(static_cast<int>(std::lround((v - low) / step)));
                setParam(row, name, numberText(v), true);
            });
            _form->addRow(label, holder);
        }
    }
}

void BehaviorsPanel::edit(const std::string& name, const std::function<void(std::vector<fla::behaviors::Applied>&)>& change,
    bool merge)
{
    fla::CommandStack* stack = _links.commands ? _links.commands() : nullptr;
    const std::vector<fla::SymbolInstance*> instances = _links.instances ? _links.instances() : std::vector<fla::SymbolInstance*>();
    if (!stack || instances.empty())
        return;
    // The panel's own change: what it shows stays as it is while the
    // history tells the window.
    _refreshing = true;
    const bool several = instances.size() > 1;
    if (several)
        stack->beginMacro(name);
    for (fla::SymbolInstance* instance : instances)
    {
        std::vector<fla::behaviors::Applied> list = fla::behaviors::of(*instance);
        change(list);
        const Attributes after = fla::behaviors::withBehaviors(*instance, list);
        if (after == instance->extraAttributes)
            continue;
        stack->push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(instance->extraAttributes,
            instance->extraAttributes, after, name, merge && !several ? kMergeSetting : -1)));
    }
    if (several)
        stack->endMacro();
    _refreshing = false;
    if (_links.changed)
        _links.changed();
}

void BehaviorsPanel::add(const std::string& id)
{
    const fla::behaviors::Applied applied = BehaviorLibrary::instance().engine().defaults(id, newSeed());
    edit("Add Behavior", [&applied](std::vector<fla::behaviors::Applied>& list) { list.push_back(applied); });
    refresh();
    _list->setCurrentRow(_list->count() - 1);
}

void BehaviorsPanel::move(int by)
{
    const int row = _list->currentRow();
    const int to = row + by;
    if (row < 0 || to < 0 || to >= _list->count())
        return;
    edit("Reorder Behaviors", [row, to](std::vector<fla::behaviors::Applied>& list) {
        if (row < static_cast<int>(list.size()) && to < static_cast<int>(list.size()))
            std::swap(list[static_cast<size_t>(row)], list[static_cast<size_t>(to)]);
    });
    refresh();
    _list->setCurrentRow(to);
}

void BehaviorsPanel::remove()
{
    const int row = _list->currentRow();
    if (row < 0)
        return;
    edit("Remove Behavior", [row](std::vector<fla::behaviors::Applied>& list) {
        if (row < static_cast<int>(list.size()))
            list.erase(list.begin() + row);
    });
    refresh();
}

void BehaviorsPanel::setParam(int row, const std::string& name, const std::string& value, bool merge)
{
    const std::vector<fla::behaviors::Applied> first = shown();
    if (row < 0 || row >= static_cast<int>(first.size()))
        return;
    const std::string type = first[static_cast<size_t>(row)].type;
    // On each selected instance that has this behavior in this place.
    edit("Behavior Settings",
        [row, &type, &name, &value](std::vector<fla::behaviors::Applied>& list) {
            if (row < static_cast<int>(list.size()) && list[static_cast<size_t>(row)].type == type)
                list[static_cast<size_t>(row)].params[name] = value;
        },
        merge);
}
