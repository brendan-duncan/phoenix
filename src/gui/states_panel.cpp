#include "states_panel.h"
#include "modal.h"

#include "../data/document.h"
#include "../data/timeline.h"
#include "../edit/command_stack.h"
#include "../edit/html5_actionscript2.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace st = fla::states;

namespace {

/// The events a transition can wait for, as the menu words them.
const std::vector<std::pair<st::When, const char*>>& transitionWhens()
{
    static const std::vector<std::pair<st::When, const char*>> list = {
        {st::When::Click, "Something is clicked"},
        {st::When::Press, "Something is pressed"},
        {st::When::Release, "The mouse button is let go over something"},
        {st::When::HoverIn, "The pointer moves onto something"},
        {st::When::HoverOut, "The pointer moves off something"},
        {st::When::Key, "A key is pressed"},
        {st::When::Finished, "Its animation finishes"},
        {st::When::Timer, "Some time goes by"},
        {st::When::Condition, "Only when an input is set"},
    };
    return list;
}

bool pointerWhen(st::When when)
{
    return when == st::When::Click || when == st::When::Press || when == st::When::Release ||
        when == st::When::HoverIn || when == st::When::HoverOut;
}

QString inputSummary(const st::Input& input)
{
    QString text = QString::fromStdString(input.name) + "  (" + QString::fromStdString(st::words(input.kind));
    if (input.kind == st::InputKind::Switch)
        text += input.value != 0.0 ? ", starts on" : ", starts off";
    else if (input.kind == st::InputKind::Number)
        text += ", starts at " + QString::number(input.value);
    return text + ")";
}

void setCurrentData(QComboBox* combo, const QVariant& data)
{
    const int index = combo->findData(data);
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

} // namespace

StatesPanel::StatesPanel(StatesLinks links, QWidget* parent)
    : QWidget(parent)
    , _links(std::move(links))
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    QHBoxLayout* top = new QHBoxLayout();
    _title = new QLabel(this);
    QFont bold = _title->font();
    bold.setBold(true);
    _title->setFont(bold);
    top->addWidget(_title);
    top->addStretch(1);
    QPushButton* addState = new QPushButton("Add State...", this);
    addState->setObjectName("statesAddState");
    addState->setToolTip("Make a frame label a state: what the clip shows, and how it plays, until something sends it on");
    _addTransition = new QPushButton("Add Transition", this);
    _addTransition->setObjectName("statesAddTransition");
    _addTransition->setToolTip("An arrow from the state picked to another: when to go from one to the other");
    _remove = new QPushButton("Remove", this);
    _remove->setObjectName("statesRemove");
    _remove->setToolTip("Take out the state, transition, input or listener picked (the frame labels stay)");
    QPushButton* test = new QPushButton("Test", this);
    test->setObjectName("statesTest");
    test->setToolTip("Play the movie in Test Movie, to click and press keys and see the states change");
    top->addWidget(addState);
    top->addWidget(_addTransition);
    top->addWidget(_remove);
    top->addWidget(test);
    layout->addLayout(top);

    _notice = new QWidget(this);
    QHBoxLayout* notice = new QHBoxLayout(_notice);
    notice->setContentsMargins(0, 0, 0, 0);
    _noticeText = new QLabel(_notice);
    _noticeText->setWordWrap(true);
    QPushButton* update = new QPushButton("Update Scripts", _notice);
    update->setObjectName("statesUpdate");
    update->setToolTip("Write the scripts again from the states as they are now");
    notice->addWidget(_noticeText, 1);
    notice->addWidget(update);
    _notice->hide();
    layout->addWidget(_notice);

    QSplitter* split = new QSplitter(Qt::Horizontal, this);
    QScrollArea* graphArea = new QScrollArea(split);
    graphArea->setWidgetResizable(true);
    _view = new StatesView(graphArea);
    _view->setObjectName("statesGraph");
    graphArea->setWidget(_view);
    split->addWidget(graphArea);

    QScrollArea* sideArea = new QScrollArea(split);
    sideArea->setWidgetResizable(true);
    sideArea->setFrameShape(QFrame::NoFrame);
    QWidget* side = new QWidget(sideArea);
    QVBoxLayout* column = new QVBoxLayout(side);
    column->setContentsMargins(4, 0, 0, 0);
    QGroupBox* picked = new QGroupBox("Picked", side);
    QVBoxLayout* pickedLayout = new QVBoxLayout(picked);
    _editors = new QStackedWidget(picked);
    QLabel* nothing = new QLabel("Click a state or an arrow in the graph, or an input or a listener, to change it.");
    nothing->setWordWrap(true);
    nothing->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    _editors->addWidget(nothing);
    _editors->addWidget(buildStateEditor());
    _editors->addWidget(buildTransitionEditor());
    _editors->addWidget(buildInputEditor());
    _editors->addWidget(buildListenerEditor());
    pickedLayout->addWidget(_editors);
    column->addWidget(picked);
    column->addStretch(1);
    sideArea->setWidget(side);
    split->addWidget(sideArea);

    // The inputs and listeners in a column of their own.
    QScrollArea* listsArea = new QScrollArea(split);
    listsArea->setWidgetResizable(true);
    listsArea->setFrameShape(QFrame::NoFrame);
    QWidget* lists = new QWidget(listsArea);
    QVBoxLayout* listsColumn = new QVBoxLayout(lists);
    listsColumn->setContentsMargins(4, 0, 0, 0);
    QGroupBox* inputs = new QGroupBox("Inputs", lists);
    inputs->setToolTip("Values the states can go by: switches that are on or off, numbers, and triggers that fire once");
    QVBoxLayout* inputsLayout = new QVBoxLayout(inputs);
    _inputs = new QListWidget(inputs);
    _inputs->setObjectName("statesInputs");
    _inputs->setMinimumHeight(48);
    _inputs->setWordWrap(true);
    QPushButton* addInput = new QPushButton("Add Input", inputs);
    addInput->setObjectName("statesAddInput");
    inputsLayout->addWidget(_inputs);
    inputsLayout->addWidget(addInput, 0, Qt::AlignLeft);
    listsColumn->addWidget(inputs);

    QGroupBox* listeners = new QGroupBox("Listeners", lists);
    listeners->setToolTip("Clicks and keys that change an input, whatever state is playing");
    QVBoxLayout* listenersLayout = new QVBoxLayout(listeners);
    _listeners = new QListWidget(listeners);
    _listeners->setObjectName("statesListeners");
    _listeners->setMinimumHeight(48);
    _listeners->setWordWrap(true);
    QPushButton* addListener = new QPushButton("Add Listener", listeners);
    addListener->setObjectName("statesAddListener");
    listenersLayout->addWidget(_listeners);
    listenersLayout->addWidget(addListener, 0, Qt::AlignLeft);
    listsColumn->addWidget(listeners);
    listsArea->setWidget(lists);
    split->addWidget(listsArea);
    split->setStretchFactor(0, 4);
    split->setStretchFactor(1, 3);
    split->setStretchFactor(2, 2);
    split->setSizes({440, 360, 280});
    // The columns narrow as the panel does, their lists and fields with them.
    sideArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    listsArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    for (QComboBox* combo : findChildren<QComboBox*>())
    {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(6);
    }
    layout->addWidget(split, 1);

    connect(addState, &QPushButton::clicked, this, &StatesPanel::addState);
    connect(_addTransition, &QPushButton::clicked, this, [this]() {
        const int from = _kind == Picked::State ? _index : 0;
        int to = static_cast<int>(_graph.states.size()) > 1 ? (from + 1) % static_cast<int>(_graph.states.size()) : 0;
        addTransition(static_cast<int>(_graph.states.size()) > 1 ? from : -1, to);
    });
    connect(_remove, &QPushButton::clicked, this, &StatesPanel::removePicked);
    connect(test, &QPushButton::clicked, this, [this]() {
        updateScripts();
        if (_links.test)
            _links.test();
    });
    connect(update, &QPushButton::clicked, this, &StatesPanel::updateScripts);
    connect(addInput, &QPushButton::clicked, this, &StatesPanel::addInput);
    connect(addListener, &QPushButton::clicked, this, &StatesPanel::addListener);
    connect(_view, &StatesView::picked, this, [this](StatesView::Pick kind, int index) {
        pick(kind == StatesView::Pick::State ? Picked::State
                : kind == StatesView::Pick::Transition ? Picked::Transition
                                                       : Picked::None,
            index);
    });
    connect(_view, &StatesView::moved, this, [this](int state, double x, double y) {
        if (state < 0 || state >= static_cast<int>(_graph.states.size()))
            return;
        st::Graph graph = _graph;
        graph.states[static_cast<size_t>(state)].x = x;
        graph.states[static_cast<size_t>(state)].y = y;
        commit(graph, st::Edit(), "Move State");
    });
    connect(_view, &StatesView::connectRequested, this, &StatesPanel::addTransition);
    connect(_inputs, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!_refreshing && row >= 0)
            pick(Picked::Input, row);
    });
    connect(_listeners, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!_refreshing && row >= 0)
            pick(Picked::Listener, row);
    });
    refresh();
}

fla::Timeline* StatesPanel::timeline() const
{
    return _links.timeline ? _links.timeline() : nullptr;
}

int StatesPanel::version() const
{
    const fla::Document* document = _links.document ? _links.document() : nullptr;
    return document && fla::html5as2::usesActionScript2(*document) ? 2 : 3;
}

double StatesPanel::frameRate() const
{
    const fla::Document* document = _links.document ? _links.document() : nullptr;
    return document && document->frameRate > 0.0 ? document->frameRate : 24.0;
}

void StatesPanel::commit(const st::Graph& graph, const st::Edit& edit, const QString& name)
{
    fla::CommandStack* stack = _links.commands ? _links.commands() : nullptr;
    fla::Timeline* edited = timeline();
    if (!stack || !edited)
        return;
    st::apply(*stack, *edited, graph, edit, version(), frameRate(), name.toStdString());
    if (_links.changed)
        _links.changed();
    refresh();
}

void StatesPanel::refresh()
{
    _refreshing = true;
    fla::Timeline* edited = timeline();
    if (edited != _shown)
    {
        _shown = edited;
        _kind = Picked::None;
        _index = -1;
    }
    _graph = edited ? st::of(*edited) : st::Graph();
    _title->setText(edited ? "States of " + QString::fromStdString(edited->name) : "No document open");
    // Each state's frames, or that its label has gone.
    std::vector<QString> ranges;
    QStringList missing;
    for (const st::State& state : _graph.states)
    {
        int first = 0, last = 0;
        if (edited && st::range(*edited, state, first, last))
            ranges.push_back(first == last ? QString("frame %1").arg(first + 1)
                                           : QString("frames %1-%2").arg(first + 1).arg(last + 1));
        else
        {
            ranges.push_back("no such label");
            missing << QString::fromStdString(state.label);
        }
    }
    const int count = _kind == Picked::State ? static_cast<int>(_graph.states.size())
        : _kind == Picked::Transition         ? static_cast<int>(_graph.transitions.size())
        : _kind == Picked::Input              ? static_cast<int>(_graph.inputs.size())
        : _kind == Picked::Listener           ? static_cast<int>(_graph.listeners.size())
                                              : 0;
    if (_index < 0 || _index >= count)
    {
        _kind = Picked::None;
        _index = -1;
    }
    _view->setGraph(_graph, ranges);
    _view->setPicked(_kind == Picked::State ? StatesView::Pick::State
            : _kind == Picked::Transition ? StatesView::Pick::Transition
                                          : StatesView::Pick::None,
        _kind == Picked::State || _kind == Picked::Transition ? _index : -1);

    _inputs->clear();
    for (const st::Input& input : _graph.inputs)
        _inputs->addItem(inputSummary(input));
    _inputs->setCurrentRow(_kind == Picked::Input ? _index : -1);
    _listeners->clear();
    for (const st::Listener& listener : _graph.listeners)
        _listeners->addItem(QString::fromStdString(st::describe(listener)));
    _listeners->setCurrentRow(_kind == Picked::Listener ? _index : -1);

    QString notice;
    if (!missing.isEmpty())
        notice = "These states have no frame label on the timeline any more: " + missing.join(", ") +
            ". Label their frames again, or remove them.";
    else if (edited && !_graph.empty() && st::stale(*edited, version(), frameRate()))
        notice = "The labels have moved, or the document's ActionScript version changed, since the scripts were written.";
    _noticeText->setText(notice);
    _notice->setVisible(!notice.isEmpty());
    _addTransition->setEnabled(!_graph.states.empty());
    _remove->setEnabled(_kind != Picked::None);
    _refreshing = false;
    showEditor();
}

void StatesPanel::pick(Picked kind, int index)
{
    _kind = index < 0 ? Picked::None : kind;
    _index = index;
    _refreshing = true;
    if (_kind != Picked::Input)
        _inputs->setCurrentRow(-1);
    if (_kind != Picked::Listener)
        _listeners->setCurrentRow(-1);
    if (_kind != Picked::State && _kind != Picked::Transition)
        _view->setPicked(StatesView::Pick::None, -1);
    _refreshing = false;
    _remove->setEnabled(_kind != Picked::None);
    showEditor();
}

void StatesPanel::fillTargets(QComboBox* combo, const std::string& current)
{
    combo->clear();
    if (const fla::Timeline* edited = timeline())
        for (const std::string& name : st::instanceNames(*edited))
            combo->addItem(QString::fromStdString(name));
    if (!current.empty() && combo->findText(QString::fromStdString(current)) < 0)
        combo->addItem(QString::fromStdString(current));
    combo->setCurrentText(QString::fromStdString(current));
}

void StatesPanel::fillInputs(QComboBox* combo, const std::string& current, bool none)
{
    combo->clear();
    if (none)
        combo->addItem("(no input)", QString());
    for (const st::Input& input : _graph.inputs)
        combo->addItem(QString::fromStdString(input.name), QString::fromStdString(input.name));
    setCurrentData(combo, QString::fromStdString(current));
}

void StatesPanel::fillCompare(const st::Input* input, st::Compare current)
{
    _compare->clear();
    std::vector<st::Compare> list;
    if (input && input->kind == st::InputKind::Switch)
        list = {st::Compare::IsOn, st::Compare::IsOff};
    else if (input && input->kind == st::InputKind::Trigger)
        list = {st::Compare::Fired};
    else if (input)
        list = {st::Compare::More, st::Compare::Less, st::Compare::Equal, st::Compare::NotEqual, st::Compare::AtLeast,
            st::Compare::AtMost};
    for (st::Compare each : list)
        _compare->addItem(QString::fromStdString(st::words(each)), static_cast<int>(each));
    setCurrentData(_compare, static_cast<int>(current));
    _compare->setVisible(input != nullptr);
    _compareValue->setVisible(input && input->kind == st::InputKind::Number);
}

void StatesPanel::fillChanges(const st::Input* input, st::Change current)
{
    _listenerChange->clear();
    std::vector<st::Change> list;
    if (input && input->kind == st::InputKind::Switch)
        list = {st::Change::TurnOn, st::Change::TurnOff, st::Change::Toggle};
    else if (input && input->kind == st::InputKind::Trigger)
        list = {st::Change::Fire};
    else if (input)
        list = {st::Change::Add, st::Change::Set};
    for (st::Change each : list)
        _listenerChange->addItem(QString::fromStdString(st::words(each)), static_cast<int>(each));
    setCurrentData(_listenerChange, static_cast<int>(current));
}

QWidget* StatesPanel::buildStateEditor()
{
    QWidget* page = new QWidget();
    QFormLayout* form = new QFormLayout(page);
    form->setContentsMargins(0, 0, 0, 0);
    _stateName = new QLineEdit(page);
    _stateName->setToolTip("The state's name is its frame label: renaming it renames the label");
    _statePlay = new QComboBox(page);
    for (st::Play play : {st::Play::Loop, st::Play::Once, st::Play::Hold})
        _statePlay->addItem(QString::fromStdString(st::words(play)), static_cast<int>(play));
    _statePlay->setToolTip("Loop its frames, play them once and stop on the last, or stop on its first frame");
    _stateUntil = new QComboBox(page);
    _stateUntil->setToolTip("Play on through the frames of later labels too");
    _stateEntry = new QCheckBox("The movie starts in this state", page);
    _stateFrames = new QLabel(page);
    form->addRow("Name", _stateName);
    form->addRow("It", _statePlay);
    form->addRow("Through", _stateUntil);
    form->addRow(QString(), _stateEntry);
    form->addRow("Frames", _stateFrames);
    connect(_stateName, &QLineEdit::editingFinished, this, &StatesPanel::stateEdited);
    connect(_statePlay, qOverload<int>(&QComboBox::currentIndexChanged), this, &StatesPanel::stateEdited);
    connect(_stateUntil, qOverload<int>(&QComboBox::currentIndexChanged), this, &StatesPanel::stateEdited);
    connect(_stateEntry, &QCheckBox::toggled, this, &StatesPanel::stateEdited);
    return page;
}

QWidget* StatesPanel::buildTransitionEditor()
{
    QWidget* page = new QWidget();
    _transitionForm = new QFormLayout(page);
    _transitionForm->setContentsMargins(0, 0, 0, 0);
    _from = new QComboBox(page);
    _to = new QComboBox(page);
    _when = new QComboBox(page);
    for (const auto& [when, words] : transitionWhens())
        _when->addItem(words, static_cast<int>(when));
    _targetRow = new QWidget(page);
    QHBoxLayout* targetLayout = new QHBoxLayout(_targetRow);
    targetLayout->setContentsMargins(0, 0, 0, 0);
    _target = new QComboBox(_targetRow);
    _target->setEditable(true);
    _target->setToolTip("The instance name of the button or movie clip on the stage");
    QPushButton* useSelected = new QPushButton("Use Selected", _targetRow);
    useSelected->setToolTip("The button or movie clip selected on the stage (give it an instance name first)");
    targetLayout->addWidget(_target, 1);
    targetLayout->addWidget(useSelected);
    _key = new QComboBox(page);
    for (const auto& [name, code] : st::keyNames())
        _key->addItem(QString::fromStdString(name));
    _seconds = new QDoubleSpinBox(page);
    _seconds->setRange(0.1, 3600.0);
    _seconds->setDecimals(1);
    _seconds->setSuffix(" s");
    _seconds->setKeyboardTracking(false);
    QWidget* conditionRow = new QWidget(page);
    QHBoxLayout* conditionLayout = new QHBoxLayout(conditionRow);
    conditionLayout->setContentsMargins(0, 0, 0, 0);
    _conditionInput = new QComboBox(conditionRow);
    _compare = new QComboBox(conditionRow);
    _compareValue = new QDoubleSpinBox(conditionRow);
    _compareValue->setRange(-99999.0, 99999.0);
    _compareValue->setDecimals(2);
    _compareValue->setKeyboardTracking(false);
    conditionLayout->addWidget(_conditionInput, 1);
    conditionLayout->addWidget(_compare);
    conditionLayout->addWidget(_compareValue);
    _afterFinish = new QCheckBox("Wait for its animation to finish first", page);
    _transitionForm->addRow("From", _from);
    _transitionForm->addRow("To", _to);
    _transitionForm->addRow("When", _when);
    _transitionForm->addRow("What", _targetRow);
    _transitionForm->addRow("Key", _key);
    _transitionForm->addRow("After", _seconds);
    _transitionForm->addRow("Only if", conditionRow);
    _transitionForm->addRow(QString(), _afterFinish);
    for (QComboBox* combo : {_from, _to, _when, _key, _conditionInput, _compare})
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, &StatesPanel::transitionEdited);
    connect(_target->lineEdit(), &QLineEdit::editingFinished, this, &StatesPanel::transitionEdited);
    connect(_target, qOverload<int>(&QComboBox::currentIndexChanged), this, &StatesPanel::transitionEdited);
    connect(_seconds, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &StatesPanel::transitionEdited);
    connect(_compareValue, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &StatesPanel::transitionEdited);
    connect(_afterFinish, &QCheckBox::toggled, this, &StatesPanel::transitionEdited);
    connect(useSelected, &QPushButton::clicked, this, [this]() {
        const QString name = _links.selectedInstance ? _links.selectedInstance() : QString();
        if (name.isEmpty())
            return;
        _target->setCurrentText(name);
        transitionEdited();
    });
    return page;
}

QWidget* StatesPanel::buildInputEditor()
{
    QWidget* page = new QWidget();
    _inputForm = new QFormLayout(page);
    _inputForm->setContentsMargins(0, 0, 0, 0);
    _inputName = new QLineEdit(page);
    _inputKind = new QComboBox(page);
    for (st::InputKind kind : {st::InputKind::Switch, st::InputKind::Number, st::InputKind::Trigger})
        _inputKind->addItem(QString::fromStdString(st::words(kind)), static_cast<int>(kind));
    _inputKind->setToolTip("A switch is on or off; a number counts; a trigger fires once and is used up by the "
                           "transition it sets off");
    _inputSwitch = new QComboBox(page);
    _inputSwitch->addItems({"Off", "On"});
    _inputNumber = new QDoubleSpinBox(page);
    _inputNumber->setRange(-99999.0, 99999.0);
    _inputNumber->setDecimals(2);
    _inputNumber->setKeyboardTracking(false);
    _inputForm->addRow("Name", _inputName);
    _inputForm->addRow("Kind", _inputKind);
    _inputForm->addRow("Starts", _inputSwitch);
    _inputForm->addRow("Starts at", _inputNumber);
    connect(_inputName, &QLineEdit::editingFinished, this, &StatesPanel::inputEdited);
    connect(_inputKind, qOverload<int>(&QComboBox::currentIndexChanged), this, &StatesPanel::inputEdited);
    connect(_inputSwitch, qOverload<int>(&QComboBox::currentIndexChanged), this, &StatesPanel::inputEdited);
    connect(_inputNumber, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &StatesPanel::inputEdited);
    return page;
}

QWidget* StatesPanel::buildListenerEditor()
{
    QWidget* page = new QWidget();
    _listenerForm = new QFormLayout(page);
    _listenerForm->setContentsMargins(0, 0, 0, 0);
    _listenerWhen = new QComboBox(page);
    for (const auto& [when, words] : transitionWhens())
        if (pointerWhen(when) || when == st::When::Key)
            _listenerWhen->addItem(words, static_cast<int>(when));
    _listenerTargetRow = new QWidget(page);
    QHBoxLayout* targetLayout = new QHBoxLayout(_listenerTargetRow);
    targetLayout->setContentsMargins(0, 0, 0, 0);
    _listenerTarget = new QComboBox(_listenerTargetRow);
    _listenerTarget->setEditable(true);
    QPushButton* useSelected = new QPushButton("Use Selected", _listenerTargetRow);
    targetLayout->addWidget(_listenerTarget, 1);
    targetLayout->addWidget(useSelected);
    _listenerKey = new QComboBox(page);
    for (const auto& [name, code] : st::keyNames())
        _listenerKey->addItem(QString::fromStdString(name));
    _listenerInput = new QComboBox(page);
    _listenerChange = new QComboBox(page);
    _listenerValue = new QDoubleSpinBox(page);
    _listenerValue->setRange(-99999.0, 99999.0);
    _listenerValue->setDecimals(2);
    _listenerValue->setKeyboardTracking(false);
    _listenerForm->addRow("When", _listenerWhen);
    _listenerForm->addRow("What", _listenerTargetRow);
    _listenerForm->addRow("Key", _listenerKey);
    _listenerForm->addRow("Input", _listenerInput);
    _listenerForm->addRow("Then", _listenerChange);
    _listenerForm->addRow("By", _listenerValue);
    for (QComboBox* combo : {_listenerWhen, _listenerKey, _listenerInput, _listenerChange, _listenerTarget})
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, &StatesPanel::listenerEdited);
    connect(_listenerTarget->lineEdit(), &QLineEdit::editingFinished, this, &StatesPanel::listenerEdited);
    connect(_listenerValue, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &StatesPanel::listenerEdited);
    connect(useSelected, &QPushButton::clicked, this, [this]() {
        const QString name = _links.selectedInstance ? _links.selectedInstance() : QString();
        if (name.isEmpty())
            return;
        _listenerTarget->setCurrentText(name);
        listenerEdited();
    });
    return page;
}

void StatesPanel::showEditor()
{
    const bool was = _refreshing;
    _refreshing = true;
    const fla::Timeline* edited = timeline();
    switch (_kind)
    {
    case Picked::State:
    {
        const st::State& state = _graph.states[static_cast<size_t>(_index)];
        _stateName->setText(QString::fromStdString(state.label));
        setCurrentData(_statePlay, static_cast<int>(state.play));
        _stateUntil->clear();
        _stateUntil->addItem("(only its own frames)", QString());
        int first = 0, last = 0;
        const bool there = edited && st::range(*edited, state, first, last);
        if (edited)
        {
            int start = -1;
            for (const auto& [label, frame] : st::labels(*edited))
            {
                if (label == state.label)
                    start = frame;
                else if (start >= 0 && frame > start)
                    _stateUntil->addItem("the end of " + QString::fromStdString(label), QString::fromStdString(label));
            }
        }
        setCurrentData(_stateUntil, QString::fromStdString(state.until));
        _stateEntry->setChecked(_graph.entry == _index);
        _stateEntry->setEnabled(_graph.entry != _index);
        _stateFrames->setText(there ? QString("%1 to %2").arg(first + 1).arg(last + 1) : "Its label is not on the timeline");
        _editors->setCurrentIndex(1);
        break;
    }
    case Picked::Transition:
    {
        const st::Transition& transition = _graph.transitions[static_cast<size_t>(_index)];
        _from->clear();
        _to->clear();
        _from->addItem("Any state", -1);
        for (size_t i = 0; i < _graph.states.size(); ++i)
        {
            _from->addItem(QString::fromStdString(_graph.states[i].label), static_cast<int>(i));
            _to->addItem(QString::fromStdString(_graph.states[i].label), static_cast<int>(i));
        }
        setCurrentData(_from, transition.from);
        setCurrentData(_to, transition.to);
        setCurrentData(_when, static_cast<int>(transition.when));
        fillTargets(_target, pointerWhen(transition.when) ? transition.target : std::string());
        const int key = _key->findText(QString::fromStdString(transition.target));
        _key->setCurrentIndex(transition.when == st::When::Key && key >= 0 ? key : 0);
        _seconds->setValue(transition.seconds);
        fillInputs(_conditionInput, transition.condition.input, true);
        fillCompare(_graph.input(transition.condition.input), transition.condition.compare);
        _compareValue->setValue(transition.condition.value);
        _afterFinish->setChecked(transition.afterFinish);
        _transitionForm->setRowVisible(_targetRow, pointerWhen(transition.when));
        _transitionForm->setRowVisible(_key, transition.when == st::When::Key);
        _transitionForm->setRowVisible(_seconds, transition.when == st::When::Timer);
        _transitionForm->setRowVisible(_afterFinish, transition.when != st::When::Finished);
        _editors->setCurrentIndex(2);
        break;
    }
    case Picked::Input:
    {
        const st::Input& input = _graph.inputs[static_cast<size_t>(_index)];
        _inputName->setText(QString::fromStdString(input.name));
        setCurrentData(_inputKind, static_cast<int>(input.kind));
        _inputSwitch->setCurrentIndex(input.value != 0.0 ? 1 : 0);
        _inputNumber->setValue(input.value);
        _inputForm->setRowVisible(_inputSwitch, input.kind == st::InputKind::Switch);
        _inputForm->setRowVisible(_inputNumber, input.kind == st::InputKind::Number);
        _editors->setCurrentIndex(3);
        break;
    }
    case Picked::Listener:
    {
        const st::Listener& listener = _graph.listeners[static_cast<size_t>(_index)];
        setCurrentData(_listenerWhen, static_cast<int>(listener.when));
        fillTargets(_listenerTarget, pointerWhen(listener.when) ? listener.target : std::string());
        const int key = _listenerKey->findText(QString::fromStdString(listener.target));
        _listenerKey->setCurrentIndex(listener.when == st::When::Key && key >= 0 ? key : 0);
        fillInputs(_listenerInput, listener.input, false);
        const st::Input* input = _graph.input(listener.input);
        fillChanges(input, listener.change);
        _listenerValue->setValue(listener.value);
        _listenerForm->setRowVisible(_listenerTargetRow, pointerWhen(listener.when));
        _listenerForm->setRowVisible(_listenerKey, listener.when == st::When::Key);
        _listenerForm->setRowVisible(_listenerValue,
            listener.change == st::Change::Set || listener.change == st::Change::Add);
        _editors->setCurrentIndex(4);
        break;
    }
    case Picked::None:
        _editors->setCurrentIndex(0);
        break;
    }
    // Only the page showing sizes the column.
    for (int i = 0; i < _editors->count(); ++i)
    {
        const QSizePolicy::Policy policy = i == _editors->currentIndex() ? QSizePolicy::Preferred : QSizePolicy::Ignored;
        _editors->widget(i)->setSizePolicy(policy, policy);
    }
    _editors->adjustSize();
    _refreshing = was;
}

void StatesPanel::stateEdited()
{
    if (_refreshing || _kind != Picked::State)
        return;
    st::Graph graph = _graph;
    st::State& state = graph.states[static_cast<size_t>(_index)];
    st::Edit edit;
    const std::string name = _stateName->text().trimmed().toStdString();
    if (!name.empty() && name != state.label)
    {
        // A name another label has already would join the two.
        bool taken = false;
        if (const fla::Timeline* edited = timeline())
            for (const auto& [label, frame] : st::labels(*edited))
                taken = taken || label == name;
        if (taken || graph.stateNamed(name) >= 0)
        {
            _stateName->setText(QString::fromStdString(state.label));
            _stateName->setToolTip("There is a frame label of that name already");
            return;
        }
        edit.renames.emplace_back(state.label, name);
        for (st::State& other : graph.states)
            if (other.until == state.label)
                other.until = name;
        state.label = name;
    }
    state.play = static_cast<st::Play>(_statePlay->currentData().toInt());
    state.until = _stateUntil->currentData().toString().toStdString();
    if (_stateEntry->isChecked())
        graph.entry = _index;
    if (graph == _graph && edit.renames.empty())
        return;
    commit(graph, edit, edit.renames.empty() ? "Change State" : "Rename State");
}

void StatesPanel::transitionEdited()
{
    if (_refreshing || _kind != Picked::Transition)
        return;
    st::Graph graph = _graph;
    st::Transition& transition = graph.transitions[static_cast<size_t>(_index)];
    transition.from = _from->currentData().toInt();
    transition.to = _to->currentData().toInt();
    transition.when = static_cast<st::When>(_when->currentData().toInt());
    if (pointerWhen(transition.when))
    {
        transition.target = _target->currentText().trimmed().toStdString();
        // A new click on nothing yet: the first instance there is.
        if (transition.target.empty() && _target->count() > 0)
            transition.target = _target->itemText(0).toStdString();
    }
    else if (transition.when == st::When::Key)
        transition.target = _key->currentText().toStdString();
    else
        transition.target.clear();
    transition.seconds = _seconds->value();
    transition.condition.input = _conditionInput->currentData().toString().toStdString();
    const st::Input* input = graph.input(transition.condition.input);
    const st::Compare compare = static_cast<st::Compare>(_compare->currentData().toInt());
    transition.condition.compare = !input ? st::Compare::None : _compare->count() ? compare : st::Compare::None;
    // The input changed kind: the first way to test it.
    if (input && _graph.transitions[static_cast<size_t>(_index)].condition.input != transition.condition.input)
        transition.condition.compare = input->kind == st::InputKind::Switch ? st::Compare::IsOn
            : input->kind == st::InputKind::Trigger                         ? st::Compare::Fired
                                                                            : st::Compare::More;
    transition.condition.value = _compareValue->value();
    transition.afterFinish = _afterFinish->isChecked();
    if (graph == _graph)
        return;
    commit(graph, st::Edit(), "Change Transition");
}

void StatesPanel::inputEdited()
{
    if (_refreshing || _kind != Picked::Input)
        return;
    st::Graph graph = _graph;
    st::Input& input = graph.inputs[static_cast<size_t>(_index)];
    const std::string name = _inputName->text().trimmed().toStdString();
    if (!name.empty() && name != input.name)
    {
        if (graph.input(name))
        {
            _inputName->setText(QString::fromStdString(input.name));
            return;
        }
        // What tests or sets it follows it.
        for (st::Transition& transition : graph.transitions)
            if (transition.condition.input == input.name)
                transition.condition.input = name;
        for (st::Listener& listener : graph.listeners)
            if (listener.input == input.name)
                listener.input = name;
        input.name = name;
    }
    const st::InputKind kind = static_cast<st::InputKind>(_inputKind->currentData().toInt());
    if (kind != input.kind)
    {
        // Tests and changes that no longer fit it are made to.
        input.kind = kind;
        input.value = 0.0;
        for (st::Transition& transition : graph.transitions)
            if (transition.condition.input == input.name)
                transition.condition.compare = kind == st::InputKind::Switch ? st::Compare::IsOn
                    : kind == st::InputKind::Trigger                         ? st::Compare::Fired
                                                                             : st::Compare::More;
        for (st::Listener& listener : graph.listeners)
            if (listener.input == input.name)
                listener.change = kind == st::InputKind::Switch ? st::Change::TurnOn
                    : kind == st::InputKind::Trigger            ? st::Change::Fire
                                                                : st::Change::Add;
    }
    else if (kind == st::InputKind::Switch)
        input.value = _inputSwitch->currentIndex() == 1 ? 1.0 : 0.0;
    else if (kind == st::InputKind::Number)
        input.value = _inputNumber->value();
    if (graph == _graph)
        return;
    commit(graph, st::Edit(), "Change Input");
}

void StatesPanel::listenerEdited()
{
    if (_refreshing || _kind != Picked::Listener)
        return;
    st::Graph graph = _graph;
    st::Listener& listener = graph.listeners[static_cast<size_t>(_index)];
    listener.when = static_cast<st::When>(_listenerWhen->currentData().toInt());
    if (pointerWhen(listener.when))
    {
        listener.target = _listenerTarget->currentText().trimmed().toStdString();
        if (listener.target.empty() && _listenerTarget->count() > 0)
            listener.target = _listenerTarget->itemText(0).toStdString();
    }
    else
        listener.target = _listenerKey->currentText().toStdString();
    const std::string inputName = _listenerInput->currentData().toString().toStdString();
    const st::Input* input = graph.input(inputName);
    if (inputName != listener.input && input)
        listener.change = input->kind == st::InputKind::Switch ? st::Change::TurnOn
            : input->kind == st::InputKind::Trigger            ? st::Change::Fire
                                                               : st::Change::Add;
    else if (_listenerChange->count())
        listener.change = static_cast<st::Change>(_listenerChange->currentData().toInt());
    listener.input = inputName;
    listener.value = _listenerValue->value();
    if (graph == _graph)
        return;
    commit(graph, st::Edit(), "Change Listener");
}

void StatesPanel::addState()
{
    const fla::Timeline* edited = timeline();
    if (!edited)
        return;
    // The labels no state has yet come first.
    QStringList unused;
    for (const auto& [label, frame] : st::labels(*edited))
        if (_graph.stateNamed(label) < 0)
            unused << QString::fromStdString(label);

    QDialog* dialog = new QDialog(this);
    dialog->setWindowTitle("Add State");
    QFormLayout* form = new QFormLayout(dialog);
    QLabel* about = new QLabel(unused.isEmpty()
            ? "A new frame label is made for the state, at the frame given, on the States layer."
            : "Pick a frame label for the state, or type a new name to make a label at the frame given.",
        dialog);
    about->setWordWrap(true);
    form->addRow(about);
    QComboBox* name = new QComboBox(dialog);
    name->setEditable(true);
    name->addItems(unused);
    if (unused.isEmpty())
    {
        int n = static_cast<int>(_graph.states.size()) + 1;
        while (_graph.stateNamed("state " + std::to_string(n)) >= 0)
            ++n;
        name->setEditText(QString("state %1").arg(n));
    }
    QSpinBox* frame = new QSpinBox(dialog);
    frame->setRange(1, 1 << 20);
    frame->setValue((_links.currentFrame ? _links.currentFrame() : 0) + 1);
    QComboBox* play = new QComboBox(dialog);
    for (st::Play each : {st::Play::Loop, st::Play::Once, st::Play::Hold})
        play->addItem(QString::fromStdString(st::words(each)), static_cast<int>(each));
    form->addRow("Label", name);
    form->addRow("At frame", frame);
    form->addRow("It", play);
    const auto labelled = [edited](const QString& text) {
        for (const auto& [label, at] : st::labels(*edited))
            if (QString::fromStdString(label) == text)
                return true;
        return false;
    };
    const auto update = [frame, name, labelled]() { frame->setEnabled(!labelled(name->currentText().trimmed())); };
    connect(name, &QComboBox::currentTextChanged, dialog, update);
    update();
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    modal::open(dialog, [this, edited, name, frame, play, labelled](int result) {
        if (result != QDialog::Accepted)
            return;
        // Another timeline may have come up while the dialog was.
        if (timeline() != edited)
            return;
        const std::string label = name->currentText().trimmed().toStdString();
        if (label.empty() || _graph.stateNamed(label) >= 0)
            return;
        st::Graph graph = _graph;
        st::State state;
        state.label = label;
        state.play = static_cast<st::Play>(play->currentData().toInt());
        const int n = static_cast<int>(graph.states.size());
        // Two to a row, every other row set in: no box in line with two others.
        state.x = 30.0 + 280.0 * (n % 2) + 140.0 * ((n / 2) % 2);
        state.y = 20.0 + 120.0 * (n / 2);
        graph.states.push_back(state);
        st::Edit edit;
        if (!labelled(QString::fromStdString(label)))
            edit.newLabels.emplace_back(label, frame->value() - 1);
        commit(graph, edit, "Add State");
        pick(Picked::State, n);
        _view->setPicked(StatesView::Pick::State, n);
    });
}

void StatesPanel::addTransition(int from, int to)
{
    if (_graph.states.empty() || to < 0 || to >= static_cast<int>(_graph.states.size()))
        return;
    st::Graph graph = _graph;
    st::Transition transition;
    transition.from = from;
    transition.to = to;
    transition.when = st::When::Click;
    if (const fla::Timeline* edited = timeline())
    {
        const std::vector<std::string> names = st::instanceNames(*edited);
        if (!names.empty())
            transition.target = names.front();
        else
            transition.when = st::When::Finished;
    }
    graph.transitions.push_back(transition);
    commit(graph, st::Edit(), "Add Transition");
    const int index = static_cast<int>(_graph.transitions.size()) - 1;
    pick(Picked::Transition, index);
    _view->setPicked(StatesView::Pick::Transition, index);
}

void StatesPanel::addInput()
{
    if (!timeline())
        return;
    st::Graph graph = _graph;
    int n = static_cast<int>(graph.inputs.size()) + 1;
    while (graph.input("input " + std::to_string(n)))
        ++n;
    st::Input input;
    input.name = "input " + std::to_string(n);
    graph.inputs.push_back(input);
    commit(graph, st::Edit(), "Add Input");
    pick(Picked::Input, static_cast<int>(_graph.inputs.size()) - 1);
    _inputs->setCurrentRow(_index);
}

void StatesPanel::addListener()
{
    const fla::Timeline* edited = timeline();
    if (!edited)
        return;
    st::Graph graph = _graph;
    if (graph.inputs.empty())
    {
        st::Input input;
        input.name = "input 1";
        graph.inputs.push_back(input);
    }
    st::Listener listener;
    listener.input = graph.inputs.front().name;
    const st::InputKind kind = graph.inputs.front().kind;
    listener.change = kind == st::InputKind::Switch ? st::Change::Toggle
        : kind == st::InputKind::Trigger            ? st::Change::Fire
                                                    : st::Change::Add;
    listener.value = 1.0;
    const std::vector<std::string> names = st::instanceNames(*edited);
    if (!names.empty())
        listener.target = names.front();
    else
    {
        listener.when = st::When::Key;
        listener.target = "Space";
    }
    graph.listeners.push_back(listener);
    commit(graph, st::Edit(), "Add Listener");
    pick(Picked::Listener, static_cast<int>(_graph.listeners.size()) - 1);
    _listeners->setCurrentRow(_index);
}

void StatesPanel::removePicked()
{
    st::Graph graph = _graph;
    QString name;
    switch (_kind)
    {
    case Picked::State:
        graph.removeState(_index);
        name = "Remove State";
        break;
    case Picked::Transition:
        graph.transitions.erase(graph.transitions.begin() + _index);
        name = "Remove Transition";
        break;
    case Picked::Input:
        graph.removeInput(graph.inputs[static_cast<size_t>(_index)].name);
        name = "Remove Input";
        break;
    case Picked::Listener:
        graph.listeners.erase(graph.listeners.begin() + _index);
        name = "Remove Listener";
        break;
    case Picked::None:
        return;
    }
    _kind = Picked::None;
    _index = -1;
    commit(graph, st::Edit(), name);
}

void StatesPanel::updateScripts()
{
    const fla::Timeline* edited = timeline();
    if (!edited || _graph.empty() || !st::stale(*edited, version(), frameRate()))
        return;
    commit(_graph, st::Edit(), "Update State Scripts");
}
