#include "behaviors_panel.h"

#include "behavior_library.h"
#include "text_layout.h"
#include "theme.h"

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
#include <QDialog>
#include <QDialogButtonBox>
#include <QPainter>
#include <QPainterPath>
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
#include <map>
#include <set>

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

/// One thing the presets browser offers: a behavior or letter animator on
/// its own, or several ready made together.
struct BrowsedPreset
{
    QString name;
    QString description;
    std::vector<fla::behaviors::Applied> list;
};

/// Behaviors ready made together, with the settings they need.
std::vector<BrowsedPreset> behaviorPresets()
{
    fla::behaviors::Engine& engine = BehaviorLibrary::instance().engine();
    const auto make = [&engine](const char* id, std::map<std::string, std::string> settings) {
        fla::behaviors::Applied applied = engine.defaults(id, 1);
        for (const auto& [name, value] : settings)
            applied.params[name] = value;
        return applied;
    };
    std::vector<BrowsedPreset> list = {
        {"Float", "Drifts gently up and down and about, as if on water.",
            {make("oscillate", {{"motion", "Up and down"}, {"amount", "8"}, {"period", "2.5"}}),
                make("wiggle", {{"frequency", "0.6"}, {"position", "4"}, {"rotation", "4"}, {"scale", "0"}})}},
        {"Heartbeat", "Beats bigger and back, again and again.", {make("pulse", {{"rate", "1.2"}, {"amount", "25"}})}},
        {"Swing", "Swings to and fro like a sign on a hook.", {make("oscillate", {{"motion", "Turn"}, {"amount", "20"}, {"period", "1.5"}})}},
        {"Bob and Turn", "Bobs up and down, tipping as it goes.",
            {make("oscillate", {{"motion", "Up and down"}, {"amount", "12"}, {"period", "1"}}),
                make("oscillate", {{"motion", "Turn"}, {"amount", "10"}, {"period", "1"}, {"phase", "90"}})}},
        {"Spin Forever", "Turns round and round at a steady speed.", {make("spin", {{"speed", "180"}})}},
        {"Flicker", "Blinks on and off at random, like a failing light.",
            {make("blink", {{"rate", "8"}, {"shown", "70"}, {"random", "true"}})}},
        {"Ring of Copies", "Copies of it in a ring, turning together.",
            {make("circle", {{"count", "10"}, {"radius", "90"}}), make("spin", {{"speed", "45"}})}},
        {"Fountain", "Copies of it spray up and fall, fading.", {make("particles", {})}},
        {"Shaky", "Shakes about nervously.", {make("wiggle", {{"frequency", "8"}, {"position", "5"}, {"rotation", "6"}})}},
    };
    return list;
}

/// A sample drawn moving as a preset makes it move, round and round: a
/// shape for behaviors, a word for letter animators.
class PresetPreview : public QWidget
{
public:
    PresetPreview(bool letters, QWidget* parent)
        : QWidget(parent)
        , _letters(letters)
        , _word(nullptr)
    {
        setMinimumSize(320, 220);
        fla::TextRun run;
        run.text = "Phoenix";
        run.size = 44;
        run.face = "Arial-BoldMT";
        run.fillColor[0] = run.fillColor[1] = run.fillColor[2] = 0xF0;
        _word.runs = {run};
        _word.autoExpand = true;
        _word.textType = fla::StaticText::TextType::Dynamic;
        _timer.setInterval(1000 / 24);
        QObject::connect(&_timer, &QTimer::timeout, this, [this]() {
            _frame = (_frame + 1) % kFrames;
            update();
        });
        _timer.start();
    }
    void show(const std::vector<fla::behaviors::Applied>& list)
    {
        _list = list;
        _frame = 0;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor(0x20, 0x26, 0x30));
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(rect().adjusted(6, 4, -6, -4), Qt::AlignRight | Qt::AlignBottom,
            QString("%1 / %2").arg(_frame + 1).arg(kFrames));
        if (_letters)
        {
            const std::vector<fla::textanim::Letter> letters = TextLayout::letters(_word);
            const std::vector<fla::textanim::Change> changes = fla::textanim::apply(_list, letters, _frame, 0, 24.0);
            const QRectF box = TextLayout::localBounds(_word);
            painter.translate(width() / 2.0 - box.width() / 2.0, height() / 2.0 - box.height() / 2.0);
            TextLayout::drawAnimated(painter, _word, changes);
            return;
        }
        fla::behaviors::Moment moment;
        moment.frame = _frame;
        moment.frameRate = 24.0;
        moment.start = 0;
        moment.end = kFrames - 1;
        const fla::behaviors::Copies copies = BehaviorLibrary::instance().engine().evaluate(_list, moment);
        painter.translate(width() / 2.0, height() / 2.0);
        painter.scale(0.7, 0.7);
        const QColor accent = theme::color("accent");
        for (const fla::behaviors::Delta& copy : copies)
        {
            painter.save();
            painter.translate(copy.x, copy.y);
            painter.rotate(copy.rotation);
            painter.scale(copy.scaleX, copy.scaleY);
            painter.setOpacity(std::clamp(copy.alpha, 0.0, 1.0));
            QColor fill = accent;
            if (copy.tint > 0.0)
            {
                const QColor tint(copy.tintColor[0], copy.tintColor[1], copy.tintColor[2]);
                fill = QColor::fromRgbF(fill.redF() + (tint.redF() - fill.redF()) * copy.tint,
                    fill.greenF() + (tint.greenF() - fill.greenF()) * copy.tint,
                    fill.blueF() + (tint.blueF() - fill.blueF()) * copy.tint);
            }
            // A star, so turning shows.
            QPainterPath star;
            for (int i = 0; i < 10; ++i)
            {
                const double angle = -1.5707963267948966 + i * 0.6283185307179586;
                const double r = i % 2 == 0 ? 26.0 : 11.0;
                const QPointF point(std::cos(angle) * r, std::sin(angle) * r);
                if (i == 0)
                    star.moveTo(point);
                else
                    star.lineTo(point);
            }
            star.closeSubpath();
            painter.setPen(QPen(fill.darker(160), 2.0));
            painter.setBrush(fill);
            painter.drawPath(star);
            painter.restore();
        }
    }

private:
    static constexpr int kFrames = 72;
    bool _letters;
    fla::StaticText _word;
    std::vector<fla::behaviors::Applied> _list;
    int _frame = 0;
    QTimer _timer;
};

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
        // Letter animations ready made, of one or more animators.
        if (mode() == Mode::Texts)
        {
            menu->addSeparator();
            QMenu* presets = menu->addMenu("Presets");
            presets->setToolTipsVisible(true);
            for (const fla::textanim::Preset& preset : fla::textanim::presets())
            {
                QAction* action = presets->addAction(QString::fromStdString(preset.name));
                action->setToolTip(QString::fromStdString(preset.description));
                action->setStatusTip(QString::fromStdString(preset.description));
                const std::vector<fla::behaviors::Applied> animators = preset.animators;
                connect(action, &QAction::triggered, this, [this, animators]() {
                    edit("Add Letter Animation", [&animators](std::vector<fla::behaviors::Applied>& list) {
                        list.insert(list.end(), animators.begin(), animators.end());
                    });
                    refresh();
                    _list->setCurrentRow(_list->count() - 1);
                });
            }
        }
        // Every one of them and some made together, each seen moving.
        if (mode() == Mode::Behaviors || mode() == Mode::Texts)
        {
            menu->addSeparator();
            QAction* browse = menu->addAction("Browse Presets...");
            browse->setStatusTip("See each behavior and ready-made animation moving before you add it");
            connect(browse, &QAction::triggered, this, &BehaviorsPanel::browsePresets);
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
    // A long label puts its controls on the line under it, rather than
    // pushing them out of a narrow panel.
    _form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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
    // An adjustment layer's effects are drawn live; there is nothing to bake.
    _bake->setVisible(mode() != Mode::Adjustments);
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
            if (spec.type == "object" && _links.pickObject)
            {
                // Or picked by clicking it on the stage.
                QWidget* holder = new QWidget(_settings);
                QHBoxLayout* line = new QHBoxLayout(holder);
                line->setContentsMargins(0, 0, 0, 0);
                combo->setParent(holder);
                line->addWidget(combo, 1);
                QToolButton* pick = new QToolButton(holder);
                pick->setText("Pick");
                pick->setToolTip("Click this, then the instance on the stage; one with no name is given one");
                line->addWidget(pick);
                connect(pick, &QToolButton::clicked, this, [this, row, name]() {
                    _links.pickObject([this, row, name](const QString& picked) {
                        if (picked.isEmpty())
                            return;
                        setParam(row, name, picked.toStdString(), false);
                        refresh();
                    });
                });
                addRow(holder);
            }
            else
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
            // Wide enough for four digits beside its arrows, whatever the font.
            seed->setFixedWidth(seed->sizeHint().width());
            seed->setToolTip("Another seed gives another, equally random, result");
            QPushButton* again = new QPushButton("New", holder);
            again->setToolTip("A new seed");
            line->addWidget(seed);
            line->addWidget(again, 1);
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
            slider->setMinimumWidth(60);
            QDoubleSpinBox* box = new QDoubleSpinBox(holder);
            const int decimals = step >= 1.0 ? 0 : step >= 0.1 ? 1 : step >= 0.01 ? 2 : 3;
            box->setDecimals(decimals);
            box->setRange(low, high);
            box->setSingleStep(step);
            box->setKeyboardTracking(false);
            box->setMinimumWidth(72);
            box->setMaximumWidth(96);
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

void BehaviorsPanel::browsePresets()
{
    const bool letters = mode() == Mode::Texts;
    std::vector<BrowsedPreset> presets;
    if (letters)
        for (const fla::textanim::Preset& preset : fla::textanim::presets())
            presets.push_back({QString::fromStdString(preset.name), QString::fromStdString(preset.description), preset.animators});
    else
        presets = behaviorPresets();
    const size_t made = presets.size();
    // Then every one on its own, as it comes, but those that need another
    // instance, a path or a sound to show anything.
    static const std::set<std::string> needMore = {"followpath", "pathrepeat", "lookat", "linkobject", "linkcontrol",
        "linkcontroller", "sound", "recorded", "timeremap", "slidercontrol", "anglecontrol", "pointcontrol", "colorcontrol",
        "checkboxcontrol", "effector", "follow", "jiggle", "path", "sourcetext"};
    for (const fla::behaviors::Definition& definition : available())
        if (!needMore.count(definition.id))
            presets.push_back({QString::fromStdString(definition.name), QString::fromStdString(definition.description),
                {letters ? fla::textanim::defaults(definition.id, 1)
                         : BehaviorLibrary::instance().engine().defaults(definition.id, 1)}});

    QDialog dialog(this);
    dialog.setWindowTitle(letters ? "Letter Animation Presets" : "Behavior Presets");
    auto* layout = new QHBoxLayout(&dialog);
    auto* list = new QListWidget(&dialog);
    list->setMinimumWidth(200);
    const auto heading = [list](const QString& text) {
        auto* item = new QListWidgetItem(text, list);
        item->setFlags(Qt::NoItemFlags);
        QFont font = item->font();
        font.setBold(true);
        item->setFont(font);
    };
    heading("Ready made");
    for (size_t i = 0; i < presets.size(); ++i)
    {
        if (i == made)
            heading(letters ? "Letter animators" : "Behaviors");
        auto* item = new QListWidgetItem(presets[i].name, list);
        item->setData(Qt::UserRole, static_cast<int>(i));
        item->setToolTip(presets[i].description);
    }
    layout->addWidget(list);
    auto* side = new QVBoxLayout();
    auto* preview = new PresetPreview(letters, &dialog);
    side->addWidget(preview, 1);
    auto* about = new QLabel(&dialog);
    about->setWordWrap(true);
    about->setMinimumHeight(48);
    about->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    side->addWidget(about);
    auto* buttons = new QDialogButtonBox(&dialog);
    QPushButton* apply = buttons->addButton(letters ? "Add to Text" : "Add to Selection", QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Close);
    side->addWidget(buttons);
    layout->addLayout(side, 1);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(list, &QListWidget::currentItemChanged, &dialog, [&presets, preview, about, apply](QListWidgetItem* item) {
        const QVariant index = item ? item->data(Qt::UserRole) : QVariant();
        apply->setEnabled(index.isValid());
        if (!index.isValid())
            return;
        const BrowsedPreset& preset = presets[static_cast<size_t>(index.toInt())];
        preview->show(preset.list);
        about->setText(preset.description);
    });
    // Added as a step of its own, and the dialog stays for another.
    const auto addChosen = [this, &presets, list, letters]() {
        const QVariant index = list->currentItem() ? list->currentItem()->data(Qt::UserRole) : QVariant();
        if (!index.isValid() || targets().empty())
            return;
        std::vector<fla::behaviors::Applied> chosen = presets[static_cast<size_t>(index.toInt())].list;
        for (fla::behaviors::Applied& applied : chosen)
            if (applied.params.count("seed"))
                applied.params["seed"] = std::to_string(newSeed());
        edit(letters ? "Add Letter Animation" : "Add Behavior", [&chosen](std::vector<fla::behaviors::Applied>& each) {
            each.insert(each.end(), chosen.begin(), chosen.end());
        });
        refresh();
        _list->setCurrentRow(_list->count() - 1);
    };
    connect(apply, &QPushButton::clicked, &dialog, addChosen);
    connect(list, &QListWidget::itemDoubleClicked, &dialog, addChosen);
    list->setCurrentRow(1);
    dialog.resize(640, 360);
    dialog.exec();
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
