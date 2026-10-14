#include "actions_panel.h"

#include "code_editor.h"

#include "../data/action_script.h"
#include "../data/document.h"
#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"
#include "../edit/command_stack.h"
#include "../edit/frame_commands.h"
#include "../edit/script_edit.h"
#include "../play/movie_runtime.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QSplitter>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <memory>

namespace {

// How long typing pauses before it goes into the document.
constexpr int kCommitDelayMs = 400;

bool hasInstanceScript(const fla::Element* element)
{
    if (!element || element->elementType() != fla::Element::Type::SymbolInstance)
        return false;
    for (const std::string& child : element->extraChildren)
    {
        if (child.find("<Actionscript") != std::string::npos)
            return true;
    }
    return false;
}

bool canHoldScript(const fla::Element* element)
{
    if (!element || element->elementType() != fla::Element::Type::SymbolInstance)
        return false;
    const auto* instance = static_cast<const fla::SymbolInstance*>(element);
    return instance->symbolType == fla::SymbolType::Button || instance->symbolType == fla::SymbolType::MovieClip;
}

} // namespace

ActionsPanel::ActionsPanel(const ActionsLinks& links, QWidget* parent)
    : QWidget(parent)
    , _links(links)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(4, 2, 4, 2);
    bar->setSpacing(2);
    _navigatorButton = new QToolButton(this);
    _navigatorButton->setText(QString(QChar(0x2630)));
    _navigatorButton->setCheckable(true);
    _navigatorButton->setChecked(true);
    _navigatorButton->setAutoRaise(true);
    _navigatorButton->setToolTip("Show or hide the script navigator: every script in the document");
    bar->addWidget(_navigatorButton);
    _pin = new QToolButton(this);
    _pin->setText("Pin");
    _pin->setCheckable(true);
    _pin->setAutoRaise(true);
    _pin->setToolTip("Pin this script: keep it here while the selection and the playhead move on");
    bar->addWidget(_pin);
    _where = new QLabel(this);
    _where->setTextFormat(Qt::PlainText);
    // A long name is cut short rather than widening the panel.
    _where->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    bar->addWidget(_where, 1);
    auto* checkButton = new QToolButton(this);
    checkButton->setText("Check");
    checkButton->setAutoRaise(true);
    checkButton->setToolTip("Check the script's syntax");
    bar->addWidget(checkButton);
    auto* findButton = new QToolButton(this);
    findButton->setText("Find");
    findButton->setAutoRaise(true);
    findButton->setToolTip("Find and replace in the script (Ctrl+F, Ctrl+H)");
    bar->addWidget(findButton);
    layout->addLayout(bar);

    _splitter = new QSplitter(Qt::Horizontal, this);
    _navigator = new QTreeWidget(_splitter);
    _navigator->setHeaderHidden(true);
    _navigator->setRootIsDecorated(true);
    _navigator->setUniformRowHeights(true);
    _navigator->setToolTip("Every script in the document; click one to edit it");
    _view = new CodeView(_splitter);
    _editor = _view->editor();
    _editor->setLanguage(CodeEditor::Language::ActionScript);
    _splitter->addWidget(_navigator);
    _splitter->addWidget(_view);
    _splitter->setStretchFactor(0, 0);
    _splitter->setStretchFactor(1, 1);
    _splitter->setSizes({180, 520});
    layout->addWidget(_splitter, 1);

    _status = new QLabel(this);
    _status->setContentsMargins(6, 2, 6, 2);
    _status->setTextFormat(Qt::PlainText);
    layout->addWidget(_status);

    _commitTimer.setSingleShot(true);
    _commitTimer.setInterval(kCommitDelayMs);
    connect(&_commitTimer, &QTimer::timeout, this, &ActionsPanel::commit);
    connect(_editor, &QPlainTextEdit::textChanged, this, &ActionsPanel::textEdited);
    _editor->installEventFilter(this);
    connect(_navigatorButton, &QToolButton::toggled, _navigator, &QWidget::setVisible);
    connect(_pin, &QToolButton::toggled, this, [this](bool on) {
        setPinned(on);
    });
    connect(checkButton, &QToolButton::clicked, this, [this]() {
        commit();
        check();
        if (_editor->errorLine() > 0)
            _editor->goToLine(_editor->errorLine());
    });
    connect(findButton, &QToolButton::clicked, this, [this]() { _view->showFind(false); });
    connect(_navigator, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) { navigatorPicked(item); });
    setFocusProxy(_editor);

    refresh();
}

void ActionsPanel::focusEditor()
{
    _editor->setFocus(Qt::OtherFocusReason);
}

ActionsPanel::Target ActionsPanel::followed() const
{
    Target target;
    // A button or movie clip selected on the stage: its own script.
    const std::vector<fla::Element*> selected = _links.selection ? _links.selection() : std::vector<fla::Element*>{};
    if (selected.size() == 1 && canHoldScript(selected.front()))
    {
        target.kind = Target::Kind::Instance;
        target.element = selected.front();
        target.timeline = _links.timeline ? _links.timeline() : nullptr;
        if (target.timeline)
        {
            for (fla::Layer* layer : target.timeline->layers)
            {
                for (fla::Frame* frame : layer ? layer->frames : std::vector<fla::Frame*>{})
                {
                    for (fla::Element* element : frame->elements)
                    {
                        if (element == target.element)
                        {
                            target.layer = layer;
                            target.frame = frame;
                        }
                    }
                }
            }
        }
        return target;
    }
    if (!selected.empty())
    {
        target.why = selected.size() == 1 ? "Only a button or movie clip instance holds a script of its own."
                                          : "Select one button or movie clip instance, or a frame.";
        return target;
    }

    // Else the keyframe at the playhead on the current layer.
    fla::Timeline* timeline = _links.timeline ? _links.timeline() : nullptr;
    if (!timeline)
    {
        target.why = "Open a document to write its scripts.";
        return target;
    }
    fla::Layer* layer = nullptr;
    for (fla::Layer* candidate : timeline->layers)
    {
        if (candidate && candidate->selected && candidate->layerType != fla::Layer::Type::Folder)
        {
            layer = candidate;
            break;
        }
    }
    // With none current: the layer with a script here, else the first that
    // holds frames.
    const int shown = _links.currentFrame ? _links.currentFrame() : 0;
    for (int pass = 0; pass < 2 && !layer; ++pass)
    {
        for (fla::Layer* candidate : timeline->layers)
        {
            if (!candidate || candidate->layerType == fla::Layer::Type::Folder || candidate->frames.empty())
                continue;
            const fla::Frame* here = fla::InsertKeyframeCommand::spanAt(*candidate, shown);
            if (pass == 1 || (here && here->actionScript && !here->actionScript->code.empty()))
            {
                layer = candidate;
                break;
            }
        }
    }
    if (!layer)
    {
        target.why = "Choose a layer, and a frame on it, to write its script.";
        return target;
    }
    fla::Frame* frame = fla::InsertKeyframeCommand::spanAt(*layer, shown);
    if (!frame)
    {
        target.why = QString("Layer \"%1\" has no frame here: add a keyframe (F6) to give it a script.")
                         .arg(QString::fromStdString(layer->name));
        return target;
    }
    target.kind = Target::Kind::Frame;
    target.timeline = timeline;
    target.layer = layer;
    target.frame = frame;
    return target;
}

bool ActionsPanel::stillThere(const Target& target) const
{
    if (target.kind == Target::Kind::None)
        return true;
    fla::Document* document = _links.document ? _links.document() : nullptr;
    if (!document)
        return false;
    const auto reachable = document->reachable();
    if (target.kind == Target::Kind::Frame)
        return target.frame && reachable.count(target.frame) && (!target.layer || reachable.count(target.layer));
    return target.element && reachable.count(target.element);
}

QString ActionsPanel::describe(const Target& target) const
{
    if (target.kind == Target::Kind::None)
        return QString();
    fla::Document* document = _links.document ? _links.document() : nullptr;
    QString where;
    // The timeline, when it is not the scene being shown.
    if (target.timeline && document && target.timeline != document->currentScene())
        where = QString::fromStdString(target.timeline->name) + QString(" %1 ").arg(QChar(0x203A));
    if (target.kind == Target::Kind::Instance)
    {
        const auto* instance = static_cast<const fla::SymbolInstance*>(target.element);
        const QString kind = instance->symbolType == fla::SymbolType::Button ? "Button" : "Movie clip";
        const QString symbol = QString::fromStdString(instance->libraryItemName);
        if (instance->name.empty())
            return where + QString("%1 instance of %2").arg(kind, symbol);
        return where + QString("\"%1\", %2 instance of %3")
                           .arg(QString::fromStdString(instance->name), kind.toLower(), symbol);
    }
    return where + QString("%1 : frame %2")
                       .arg(target.layer ? QString::fromStdString(target.layer->name) : QString("?"))
                       .arg(target.frame->index + 1);
}

QString ActionsPanel::codeOf(const Target& target) const
{
    if (target.kind == Target::Kind::Frame)
        return target.frame->actionScript ? QString::fromStdString(target.frame->actionScript->code) : QString();
    if (target.kind == Target::Kind::Instance)
        return QString::fromStdString(fla::scripts::instanceScript(*target.element));
    return QString();
}

void ActionsPanel::show(const Target& target)
{
    const bool changed = !(target == _target);
    if (changed)
    {
        // What was typed into the last script goes there before moving on.
        commit();
        ++_session;
    }
    _target = target;
    bool reloaded = false;
    _where->setText(describe(target));
    _where->setToolTip(_where->text());
    _loading = true;
    if (target.kind == Target::Kind::None)
    {
        _editor->setReadOnly(true);
        _editor->setPlainText(QString());
        _editor->setPlaceholderText(target.why);
        _editor->setErrorLine(0);
        _status->clear();
    }
    else
    {
        _editor->setReadOnly(false);
        _editor->setPlaceholderText(target.kind == Target::Kind::Instance
                ? "on (release) {\n    gotoAndPlay(2);\n}"
                : "stop();");
        const QString code = codeOf(target);
        if (changed)
        {
            _editor->setPlainText(code);
            _editor->document()->clearUndoRedoStacks();
        }
        else if (!_dirty && code != _editor->toPlainText())
        {
            _editor->replaceAllText(code);
            reloaded = true;
        }
    }
    _loading = false;
    if ((changed || reloaded) && target.kind != Target::Kind::None)
        check();
}

void ActionsPanel::refresh()
{
    // Called back by the edit this panel is making: it knows already.
    if (_busy)
        return;
    // Typing that has not gone in yet goes in first, so an edit made
    // elsewhere is laid over it rather than lost.
    if (_dirty)
        commit();
    if (_pinned && !stillThere(_target))
        setPinned(false);
    show(_pinned ? _target : followed());
    rebuildNavigator();
}

void ActionsPanel::textEdited()
{
    if (_loading || _target.kind == Target::Kind::None)
        return;
    _dirty = true;
    _commitTimer.start();
}

void ActionsPanel::commit()
{
    _commitTimer.stop();
    if (!_dirty)
        return;
    _dirty = false;
    fla::CommandStack* stack = _links.commands ? _links.commands() : nullptr;
    if (!stack || _target.kind == Target::Kind::None || !stillThere(_target))
        return;
    const std::string code = _editor->toPlainText().toStdString();
    if (QString::fromStdString(code) == codeOf(_target))
        return;
    _busy = true;
    if (_target.kind == Target::Kind::Frame)
        stack->push(std::make_unique<fla::scripts::SetFrameScriptCommand>(_target.frame, code, _session));
    else
        stack->push(std::make_unique<fla::scripts::SetInstanceScriptCommand>(_target.element, code, _session));
    _busy = false;
    check();
    rebuildNavigator();
}

void ActionsPanel::check()
{
    const QString code = _editor->toPlainText();
    if (_target.kind == Target::Kind::None || code.trimmed().isEmpty())
    {
        _editor->setErrorLine(0);
        _status->clear();
        return;
    }
    int line = 0;
    std::string message;
    if (fla::play::checkSyntax(code.toStdString(), line, message))
    {
        _editor->setErrorLine(0);
        _status->setText("No syntax errors");
        return;
    }
    _editor->setErrorLine(line, QString::fromStdString(message));
    _status->setText(line > 0 ? QString("Line %1: %2").arg(line).arg(QString::fromStdString(message))
                              : QString::fromStdString(message));
}

void ActionsPanel::setPinned(bool pinned)
{
    if (_pinned == pinned)
        return;
    _pinned = pinned;
    {
        const QSignalBlocker block(_pin);
        _pin->setChecked(pinned);
    }
    if (!pinned)
        show(followed());
}

void ActionsPanel::rebuildNavigator()
{
    std::vector<Target> targets;
    fla::Document* document = _links.document ? _links.document() : nullptr;
    struct Group
    {
        QString title;
        std::vector<Target> targets;
    };
    std::vector<Group> groups;
    const auto collect = [&targets](fla::Timeline* timeline, Group& group) {
        for (fla::Layer* layer : timeline->layers)
        {
            if (!layer)
                continue;
            for (fla::Frame* frame : layer->frames)
            {
                if (frame->actionScript && !frame->actionScript->code.empty())
                {
                    Target target;
                    target.kind = Target::Kind::Frame;
                    target.timeline = timeline;
                    target.layer = layer;
                    target.frame = frame;
                    group.targets.push_back(target);
                }
                for (fla::Element* element : frame->elements)
                {
                    if (!hasInstanceScript(element))
                        continue;
                    Target target;
                    target.kind = Target::Kind::Instance;
                    target.timeline = timeline;
                    target.layer = layer;
                    target.frame = frame;
                    target.element = element;
                    group.targets.push_back(target);
                }
            }
        }
        targets.insert(targets.end(), group.targets.begin(), group.targets.end());
    };
    if (document)
    {
        for (fla::Timeline* scene : document->timelines)
        {
            Group group{QString::fromStdString(scene->name), {}};
            collect(scene, group);
            if (!group.targets.empty())
                groups.push_back(group);
        }
        if (document->symbolList)
        {
            for (fla::Symbol* symbol : document->symbolList->symbols)
            {
                for (fla::Timeline* timeline : symbol->timelines)
                {
                    Group group{QString::fromStdString(symbol->name), {}};
                    collect(timeline, group);
                    if (!group.targets.empty())
                        groups.push_back(group);
                }
            }
        }
    }

    // Only made again when the scripts there are have changed.
    bool same = targets.size() == _navigatorTargets.size();
    for (std::size_t i = 0; same && i < targets.size(); ++i)
        same = targets[i] == _navigatorTargets[i];
    if (!same)
    {
        _navigatorTargets = targets;
        const QSignalBlocker block(_navigator);
        _navigator->clear();
        int index = 0;
        for (const Group& group : groups)
        {
            auto* top = new QTreeWidgetItem(_navigator, {group.title});
            top->setFlags(Qt::ItemIsEnabled);
            for (const Target& target : group.targets)
            {
                QString label;
                if (target.kind == Target::Kind::Frame)
                    label = QString("%1 : frame %2")
                                .arg(QString::fromStdString(target.layer->name))
                                .arg(target.frame->index + 1);
                else
                {
                    const auto* instance = static_cast<const fla::SymbolInstance*>(target.element);
                    label = instance->name.empty() ? QString::fromStdString(instance->libraryItemName)
                                                   : QString::fromStdString(instance->name);
                    label += QString(" (frame %1)").arg(target.frame->index + 1);
                }
                auto* item = new QTreeWidgetItem(top, {label});
                item->setData(0, Qt::UserRole, index++);
                item->setToolTip(0, label);
            }
            top->setExpanded(true);
        }
        if (groups.empty())
        {
            auto* none = new QTreeWidgetItem(_navigator, {"No scripts yet"});
            none->setFlags(Qt::NoItemFlags);
        }
    }

    // The script being edited, marked.
    const QSignalBlocker block(_navigator);
    _navigator->clearSelection();
    for (QTreeWidgetItemIterator it(_navigator); *it; ++it)
    {
        const QVariant data = (*it)->data(0, Qt::UserRole);
        if (data.isValid() && _navigatorTargets[static_cast<std::size_t>(data.toInt())] == _target)
            (*it)->setSelected(true);
    }
}

void ActionsPanel::navigatorPicked(QTreeWidgetItem* item)
{
    const QVariant data = item ? item->data(0, Qt::UserRole) : QVariant();
    if (!data.isValid())
        return;
    const Target target = _navigatorTargets[static_cast<std::size_t>(data.toInt())];
    if (!stillThere(target))
        return;
    // A script picked here stays until the pin is let go.
    _pinned = true;
    {
        const QSignalBlocker block(_pin);
        _pin->setChecked(true);
    }
    show(target);
    rebuildNavigator();
    focusEditor();
}

bool ActionsPanel::eventFilter(QObject* watched, QEvent* event)
{
    // Leaving the editor puts what was typed in, and ends the step.
    if (watched == _editor && event->type() == QEvent::FocusOut)
    {
        commit();
        ++_session;
    }
    return QWidget::eventFilter(watched, event);
}
