#include "behaviors_panel.h"

#include "behavior_library.h"

#include "../data/symbol_instance.h"
#include "../edit/command_stack.h"
#include "../edit/property_command.h"
#include "../edit/shape_operators.h"
#include "../edit/text_animators.h"
#include "../edit/adjustments.h"
#include "../data/layer.h"
#include "../data/static_text.h"
#include "../data/shape.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QRegularExpression>
#include <QUrl>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
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
    _add->setObjectName("behaviorsAdd");
    _add->setText("Add Behavior");
    _add->setToolButtonStyle(Qt::ToolButtonTextOnly);
    _add->setPopupMode(QToolButton::InstantPopup);
    QMenu* menu = new QMenu(_add);
    connect(menu, &QMenu::aboutToShow, this, [this, menu]() {
        menu->clear();
        for (const fla::behaviors::Definition& definition : available())
        {
            QAction* action = menu->addAction(QString::fromStdString(definition.name));
            action->setToolTip(QString::fromStdString(definition.description));
            action->setStatusTip(QString::fromStdString(definition.description));
            const std::string id = definition.id;
            connect(action, &QAction::triggered, this, [this, id]() { add(id); });
        }
        // Behaviors of one's own: a new one to write, and where they are.
        if (mode() == Mode::Behaviors)
        {
            menu->addSeparator();
            QAction* write = menu->addAction("New Behavior...");
            write->setStatusTip("Write a behavior of your own in JavaScript, starting from an example");
            connect(write, &QAction::triggered, this, &BehaviorsPanel::newBehavior);
            QAction* folder = menu->addAction("Open Behaviors Folder");
            folder->setStatusTip("Show the folder your own behaviors are kept in");
            connect(folder, &QAction::triggered, this, []() {
                QDesktopServices::openUrl(QUrl::fromLocalFile(BehaviorLibrary::folder()));
            });
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
    _bake = new QToolButton(this);
    _bake->setObjectName("behaviorsBake");
    _bake->setText("Bake");
    _bake->setToolTip("Bake to Keyframes: leave the selected instances' behaviors as keyframes, a keyframe a frame, "
                      "their copies as instances of their own");
    connect(_bake, &QToolButton::clicked, this, [this]() {
        if (_links.bake)
            _links.bake();
    });

    QHBoxLayout* buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->addWidget(_add);
    buttons->addStretch();
    buttons->addWidget(_up);
    buttons->addWidget(_down);
    buttons->addWidget(_remove);
    buttons->addWidget(_bake);

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

BehaviorsPanel::Mode BehaviorsPanel::mode() const
{
    // Instances first, then shapes, then text fields.
    const std::vector<fla::SymbolInstance*> instances = _links.instances ? _links.instances() : std::vector<fla::SymbolInstance*>();
    if (!instances.empty())
        return Mode::Behaviors;
    if (_links.shapes && !_links.shapes().empty())
        return Mode::Shapes;
    if (_links.texts && !_links.texts().empty())
        return Mode::Texts;
    if (_links.adjustmentLayer && _links.adjustmentLayer())
        return Mode::Adjustments;
    return Mode::Behaviors;
}

bool BehaviorsPanel::shapeMode() const
{
    return mode() == Mode::Shapes;
}

std::vector<fla::DOMElement*> BehaviorsPanel::targets() const
{
    std::vector<fla::DOMElement*> list;
    switch (mode())
    {
    case Mode::Shapes:
        for (fla::Shape* shape : _links.shapes())
            list.push_back(shape);
        break;
    case Mode::Texts:
        for (fla::StaticText* text : _links.texts())
            list.push_back(text);
        break;
    case Mode::Adjustments:
        list.push_back(_links.adjustmentLayer());
        break;
    default:
        if (_links.instances)
            for (fla::SymbolInstance* instance : _links.instances())
                list.push_back(instance);
    }
    return list;
}

const char* BehaviorsPanel::attribute() const
{
    switch (mode())
    {
    case Mode::Shapes:
        return fla::shapeops::kAttribute;
    case Mode::Texts:
        return fla::textanim::kAttribute;
    case Mode::Adjustments:
        return fla::compositing::kAdjustments;
    default:
        return "phoenixBehaviors";
    }
}

const fla::behaviors::Definition* BehaviorsPanel::definitionOf(const std::string& id) const
{
    switch (mode())
    {
    case Mode::Shapes:
        return fla::shapeops::find(id);
    case Mode::Texts:
        return fla::textanim::find(id);
    case Mode::Adjustments:
        return fla::compositing::find(id);
    default:
        return BehaviorLibrary::instance().engine().find(id);
    }
}

std::vector<fla::behaviors::Definition> BehaviorsPanel::available() const
{
    switch (mode())
    {
    case Mode::Shapes:
        return fla::shapeops::definitions();
    case Mode::Texts:
        return fla::textanim::definitions();
    case Mode::Adjustments:
        return fla::compositing::definitions();
    default:
        return BehaviorLibrary::instance().engine().definitions();
    }
}

std::vector<fla::behaviors::Applied> BehaviorsPanel::shown() const
{
    const std::vector<fla::DOMElement*> list = targets();
    return list.empty() ? std::vector<fla::behaviors::Applied>() : fla::behaviors::listOf(*list.front(), attribute());
}

void BehaviorsPanel::refresh()
{
    if (_refreshing)
        return;
    _refreshing = true;
    const std::vector<fla::DOMElement*> selected = targets();
    const bool shapes = shapeMode();
    const bool texts = mode() == Mode::Texts;
    const bool adjusting = mode() == Mode::Adjustments;
    const int keep = _list->currentRow();
    _list->clear();
    _add->setText(shapes ? "Add Operator" : texts ? "Add Animator" : adjusting ? "Add Effect" : "Add Behavior");
    if (selected.empty())
        _what->setText("Select a symbol instance to give it behaviors (motion worked out every frame, on top of its "
                       "keys), a drawn shape to give it operators (Trim Paths, Wiggle Path, Zig-zag), or a text field "
                       "to animate its letters.");
    else if (adjusting)
        _what->setText(QString("Effects of the adjustment layer %1, on everything below it")
                           .arg(QString::fromStdString(static_cast<fla::Layer*>(selected.front())->name)));
    else if (texts)
        _what->setText(selected.size() == 1 ? QString("Letter animators of the text")
                                            : QString("Letter animators of %1 text fields (shown: the first one's)").arg(selected.size()));
    else if (shapes)
        _what->setText(selected.size() == 1 ? QString("Operators of the shape")
                                            : QString("Operators of %1 shapes (shown: the first one's)").arg(selected.size()));
    else if (selected.size() == 1)
        _what->setText(QString("Behaviors of %1")
                           .arg(QString::fromStdString(static_cast<fla::SymbolInstance*>(selected.front())->libraryItemName)));
    else
        _what->setText(QString("Behaviors of %1 instances (shown: the first one's)").arg(selected.size()));
    for (const fla::behaviors::Applied& applied : shown())
    {
        const fla::behaviors::Definition* definition = definitionOf(applied.type);
        QListWidgetItem* item = new QListWidgetItem(
            definition ? QString::fromStdString(definition->name) : QString("%1 (missing)").arg(QString::fromStdString(applied.type)),
            _list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(applied.enabled ? Qt::Checked : Qt::Unchecked);
        if (!definition)
            item->setToolTip("No behavior of this name is known: its file may have been taken out of the Behaviors folder");
    }
    _add->setEnabled(!selected.empty());
    if (_list->count() > 0)
        _list->setCurrentRow(std::clamp(keep, 0, _list->count() - 1));
    const bool any = _list->count() > 0;
    _up->setEnabled(any);
    _down->setEnabled(any);
    _remove->setEnabled(any);
    _bake->setEnabled(any);
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
    const fla::behaviors::Definition* definition = definitionOf(applied.type);
    if (!definition)
    {
        _description->setText("This behavior's file is missing; its settings are kept.");
        return;
    }
    _description->setText(QString::fromStdString(definition->description));
    const int now = frame();
    for (const fla::behaviors::ParamSpec& spec : definition->params)
    {
        // Keyed settings show their value on the frame shown.
        const std::string current = applied.valueAt(spec.name, now, spec.text);
        const QString label = QString::fromStdString(spec.label);
        const std::string name = spec.name;
        // The key button: filled with a key on this frame, hollow when keyed
        // on other frames, and plain when not keyed.
        QToolButton* key = new QToolButton(_settings);
        key->setAutoRaise(true);
        bool keyedHere = false;
        for (const auto& each : applied.keys(spec.name))
            keyedHere = keyedHere || each.first == now;
        key->setText(QString(QChar(keyedHere ? 0x25C6 : applied.keyed(spec.name) ? 0x25C7 : 0x00B7)));
        key->setToolTip(keyedHere ? "Keyed on this frame: click to take the key away"
                : applied.keyed(spec.name) ? "Keyed on other frames: click to key it here"
                                           : "Click to key this setting on the frame shown, so it can change over time");
        connect(key, &QToolButton::clicked, this, [this, row, name]() { toggleKey(row, name); });
        const auto addRow = [this, key, &label](QWidget* control) {
            QWidget* holder = new QWidget(_settings);
            QHBoxLayout* line = new QHBoxLayout(holder);
            line->setContentsMargins(0, 0, 0, 0);
            line->addWidget(control, 1);
            line->addWidget(key);
            _form->addRow(label, holder);
        };
        if (spec.type == "seed")
            key->hide();
        if (spec.type == "checkbox")
        {
            QCheckBox* box = new QCheckBox(_settings);
            box->setChecked(current == "true" || current == "1");
            connect(box, &QCheckBox::toggled, this,
                [this, row, name](bool on) { setParam(row, name, on ? "true" : "false", false); });
            addRow(box);
        }
        else if (spec.type == "choice")
        {
            QComboBox* combo = new QComboBox(_settings);
            for (const std::string& choice : spec.choices)
                combo->addItem(QString::fromStdString(choice));
            combo->setCurrentText(QString::fromStdString(current));
            connect(combo, &QComboBox::currentTextChanged, this,
                [this, row, name](const QString& text) { setParam(row, name, text.toStdString(), false); });
            addRow(combo);
        }
        else if (spec.type == "object" || spec.type == "controller" || spec.type == "text")
        {
            // A name: picked from the instances (or controllers) there are,
            // or typed.
            QComboBox* combo = new QComboBox(_settings);
            combo->setEditable(true);
            combo->setInsertPolicy(QComboBox::NoInsert);
            QStringList names;
            if (spec.type == "object" && _links.objectNames)
                names = _links.objectNames();
            else if (spec.type == "controller" && _links.controllerNames)
                names = _links.controllerNames();
            combo->addItems(names);
            combo->setCurrentText(QString::fromStdString(current));
            if (spec.type == "object")
                combo->setToolTip(names.isEmpty() ? "Give the other instance a name in the Properties panel first"
                                                  : "The instance name of another symbol instance on this timeline");
            else if (spec.type == "controller")
                combo->setToolTip(names.isEmpty() ? "Make a controller in the Controllers panel first"
                                                  : "A master controller, from the Controllers panel");
            connect(combo->lineEdit(), &QLineEdit::editingFinished, this, [this, row, name, combo]() {
                setParam(row, name, combo->currentText().toStdString(), false);
            });
            connect(combo, &QComboBox::activated, this, [this, row, name, combo](int) {
                setParam(row, name, combo->currentText().toStdString(), false);
            });
            addRow(combo);
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
            seed->setMaximumWidth(84);
            seed->setToolTip("Another seed gives another, equally random, result");
            QPushButton* again = new QPushButton("New", holder);
            again->setToolTip("A new seed");
            line->addWidget(seed, 1);
            line->addWidget(again);
            connect(seed, &QSpinBox::valueChanged, this,
                [this, row, name](int value) { setParam(row, name, std::to_string(value), false); });
            connect(again, &QPushButton::clicked, seed, [seed]() { seed->setValue(newSeed()); });
            addRow(holder);
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
            box->setMaximumWidth(84);
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
            addRow(holder);
        }
    }
}

void BehaviorsPanel::edit(const std::string& name, const std::function<void(std::vector<fla::behaviors::Applied>&)>& change,
    bool merge)
{
    fla::CommandStack* stack = _links.commands ? _links.commands() : nullptr;
    const std::vector<fla::DOMElement*> selected = targets();
    const std::string key = attribute();
    if (!stack || selected.empty())
        return;
    // The panel's own change: what it shows stays as it is while the
    // history tells the window.
    _refreshing = true;
    const bool several = selected.size() > 1;
    if (several)
        stack->beginMacro(name);
    for (fla::DOMElement* element : selected)
    {
        std::vector<fla::behaviors::Applied> list = fla::behaviors::listOf(*element, key);
        change(list);
        const Attributes after = fla::behaviors::withList(*element, key, list);
        if (after == element->extraAttributes)
            continue;
        stack->push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(element->extraAttributes,
            element->extraAttributes, after, name, merge && !several ? kMergeSetting : -1)));
    }
    if (several)
        stack->endMacro();
    _refreshing = false;
    if (_links.changed)
        _links.changed();
}

void BehaviorsPanel::add(const std::string& id)
{
    const Mode now = mode();
    const fla::behaviors::Applied applied = now == Mode::Shapes ? fla::shapeops::defaults(id, newSeed())
        : now == Mode::Texts                                    ? fla::textanim::defaults(id, newSeed())
        : now == Mode::Adjustments                              ? fla::compositing::defaults(id, newSeed())
                                                                : BehaviorLibrary::instance().engine().defaults(id, newSeed());
    edit(now == Mode::Shapes ? "Add Operator" : now == Mode::Texts ? "Add Letter Animator"
            : now == Mode::Adjustments                                 ? "Add Effect"
                                                                       : "Add Behavior", [&applied](std::vector<fla::behaviors::Applied>& list) { list.push_back(applied); });
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
    // On each selected instance that has this behavior in this place; a
    // keyed setting is keyed on the frame shown.
    const int now = frame();
    edit("Behavior Settings",
        [row, &type, &name, &value, now](std::vector<fla::behaviors::Applied>& list) {
            if (row >= static_cast<int>(list.size()) || list[static_cast<size_t>(row)].type != type)
                return;
            fla::behaviors::Applied& applied = list[static_cast<size_t>(row)];
            if (applied.keyed(name))
                applied.setKey(name, now, value);
            else
                applied.params[name] = value;
        },
        merge);
    // The key button shows the key now made.
    if (!first[static_cast<size_t>(row)].keyed(name))
        return;
    bool keyedHere = false;
    for (const auto& each : first[static_cast<size_t>(row)].keys(name))
        keyedHere = keyedHere || each.first == now;
    if (!keyedHere)
        QTimer::singleShot(0, this, [this]() { fillSettings(); });
}

void BehaviorsPanel::toggleKey(int row, const std::string& name)
{
    const std::vector<fla::behaviors::Applied> first = shown();
    if (row < 0 || row >= static_cast<int>(first.size()))
        return;
    const std::string type = first[static_cast<size_t>(row)].type;
    const fla::behaviors::Definition* definition = definitionOf(type);
    std::string fallback;
    if (definition)
        for (const fla::behaviors::ParamSpec& spec : definition->params)
            if (spec.name == name)
                fallback = spec.text;
    const int now = frame();
    bool keyedHere = false;
    for (const auto& each : first[static_cast<size_t>(row)].keys(name))
        keyedHere = keyedHere || each.first == now;
    edit(keyedHere ? "Remove Behavior Key" : "Key Behavior Setting",
        [row, &type, &name, now, keyedHere, &fallback](std::vector<fla::behaviors::Applied>& list) {
            if (row >= static_cast<int>(list.size()) || list[static_cast<size_t>(row)].type != type)
                return;
            fla::behaviors::Applied& applied = list[static_cast<size_t>(row)];
            const std::string value = applied.valueAt(name, now, fallback);
            if (keyedHere)
                applied.removeKey(name, now, value);
            else
                applied.setKey(name, now, value);
        });
    fillSettings();
}

int BehaviorsPanel::frame() const
{
    return _links.currentFrame ? _links.currentFrame() : 0;
}

void BehaviorsPanel::newBehavior()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, "New Behavior", "Name:", QLineEdit::Normal, "My Behavior", &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    // The file is named after it, in letters and digits.
    QString base = name;
    base.remove(QRegularExpression("[^A-Za-z0-9_ -]"));
    base = base.simplified().replace(' ', '-');
    if (base.isEmpty())
        base = "behavior";
    const QString path = QDir(BehaviorLibrary::folder()).filePath(base + ".js");
    if (!QFileInfo::exists(path))
    {
        QString quoted = name;
        quoted.replace("\\", "\\\\").replace("\"", "\\\"");
        const QString source = QString(
            "// A behavior of your own. Phoenix calls evaluate for each frame the\n"
            "// instance is on, and adds what it returns to the keyed motion: x and y in\n"
            "// pixels, rotation in degrees, scale and alpha as factors, frame to run a\n"
            "// graphic's loop on. Save, and the behavior is in Add Behavior at once.\n"
            "behavior({\n"
            "    name: \"%1\",\n"
            "    description: \"Drifts sideways and bobs up and down.\",\n"
            "    params: {\n"
            "        speed: { label: \"Speed (pixels a second)\", value: 40, min: -500, max: 500 },\n"
            "        bob: { label: \"Bob (pixels)\", value: 4, min: 0, max: 50 }\n"
            "    },\n"
            "    evaluate: function (ctx, p) {\n"
            "        return {\n"
            "            x: p.speed * (ctx.frame - ctx.start) / ctx.fps,\n"
            "            y: Math.sin(ctx.time * 2) * p.bob\n"
            "        };\n"
            "    }\n"
            "});\n").arg(quoted);
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            QMessageBox::warning(this, "New Behavior", QString("Could not write %1.").arg(QDir::toNativeSeparators(path)));
            return;
        }
        out.write(source.toUtf8());
        out.close();
        BehaviorLibrary::instance().reload();
        refresh();
    }
    if (_links.openScript)
        _links.openScript(path);
    else
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}
