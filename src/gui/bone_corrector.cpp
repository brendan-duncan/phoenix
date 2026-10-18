#include "bone_corrector.h"

#include "hot_text.h"
#include "phoenix_view.h"
#include "player.h"

#include "../data/document.h"
#include "../data/fla_document.h"
#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/shape.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/timeline.h"
#include "../edit/command_stack.h"
#include "../edit/element_commands.h"
#include "../edit/ik_pose.h"
#include "../edit/property_command.h"
#include "../edit/shape_merge.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <vector>

namespace corrections = fla::corrections;

using Attributes = std::vector<std::pair<std::string, std::string>>;

struct BoneCorrector::Session
{
    const fla::FLADocument* document = nullptr;
    fla::Frame* pose = nullptr;
    fla::SymbolInstance* container = nullptr;
    std::string xml;
    std::string armature;
    std::string node;
    QString bone;
    int into = 0;
    double from = 0.0;
    double to = 0.0;
    int depth = 0;
    int sceneFrame = 0;
    size_t entry = 0;
    /// Each shape as the other bones' corrections leave it on the frame, by
    /// its layer and its place on the frame.
    std::map<std::pair<int, int>, std::unique_ptr<fla::Shape>> bases;
};

namespace {

const QString kDegree = QString(QChar(0x00B0));

fla::Timeline* timelineOf(const fla::Frame* frame)
{
    const auto* layer = frame ? dynamic_cast<const fla::Layer*>(frame->parent) : nullptr;
    return layer ? dynamic_cast<fla::Timeline*>(layer->parent) : nullptr;
}

const fla::Frame* frameAt(const fla::Layer& layer, int index)
{
    for (const fla::Frame* frame : layer.frames)
        if (frame && frame->index <= index && index < frame->index + std::max(1, frame->duration))
            return frame;
    return nullptr;
}

/// The shape that is element \a element of the frame showing \a index on
/// layer \a layer of \a container's symbol.
fla::Shape* shapeIn(const fla::SymbolInstance& container, int layer, int index, int element)
{
    if (!container.symbol || container.symbol->timelines.empty() || !container.symbol->timelines.front())
        return nullptr;
    const std::vector<fla::Layer*>& layers = container.symbol->timelines.front()->layers;
    if (layer < 0 || static_cast<size_t>(layer) >= layers.size() || !layers[static_cast<size_t>(layer)])
        return nullptr;
    const fla::Frame* frame = frameAt(*layers[static_cast<size_t>(layer)], index);
    if (!frame || element < 0 || static_cast<size_t>(element) >= frame->elements.size())
        return nullptr;
    fla::Element* found = frame->elements[static_cast<size_t>(element)];
    return found && found->elementType() == fla::Element::Type::Shape ? static_cast<fla::Shape*>(found) : nullptr;
}

int symbolLength(const fla::Symbol& symbol)
{
    int length = 1;
    if (!symbol.timelines.empty() && symbol.timelines.front())
        for (const fla::Layer* layer : symbol.timelines.front()->layers)
            for (const fla::Frame* frame : layer ? layer->frames : std::vector<fla::Frame*>())
                if (frame)
                    length = std::max(length, frame->index + std::max(1, frame->duration));
    return length;
}

/// What a correction can be put on: an instance on the armature's timeline
/// at the playhead.
struct Target
{
    fla::SymbolInstance* instance = nullptr;
    QString label;
};

std::vector<Target> targetsAt(const fla::Timeline& timeline, int frame, bool graphicsOnly)
{
    std::vector<Target> targets;
    for (const fla::Layer* layer : timeline.layers)
    {
        if (!layer || layer->layerType == fla::Layer::Type::Folder)
            continue;
        const fla::Frame* shown = frameAt(*layer, frame);
        for (fla::Element* element : shown ? shown->elements : std::vector<fla::Element*>())
        {
            if (!element || element->elementType() != fla::Element::Type::SymbolInstance)
                continue;
            auto* instance = static_cast<fla::SymbolInstance*>(element);
            if (!instance->symbol || instance->symbol->otherSymbolType == "ik container")
                continue;
            if (graphicsOnly && instance->symbolType != fla::SymbolType::Graphic)
                continue;
            targets.push_back({instance, QString("%1 on %2").arg(QString::fromStdString(instance->symbol->name),
                                             QString::fromStdString(layer->name))});
        }
    }
    return targets;
}

QString degrees(double value)
{
    return QString::number(value, 'f', std::fabs(value - std::round(value)) < 0.05 ? 0 : 1) + kDegree;
}

/// One correction a bone drives, where it is kept.
struct Driven
{
    fla::SymbolInstance* instance = nullptr;
    size_t index = 0;
    corrections::Correction correction;
};

std::vector<Driven> drivenBy(const fla::Timeline& timeline, const std::string& armature, const std::string& node)
{
    std::vector<Driven> found;
    for (const fla::Layer* layer : timeline.layers)
        for (const fla::Frame* frame : layer ? layer->frames : std::vector<fla::Frame*>())
            for (fla::Element* element : frame ? frame->elements : std::vector<fla::Element*>())
            {
                if (!element || element->elementType() != fla::Element::Type::SymbolInstance)
                    continue;
                auto* instance = static_cast<fla::SymbolInstance*>(element);
                const std::vector<corrections::Correction> list = corrections::of(*instance);
                for (size_t i = 0; i < list.size(); ++i)
                    if (list[i].armature == armature && list[i].joint == node)
                        found.push_back({instance, i, list[i]});
            }
    return found;
}

QString describe(const Driven& driven)
{
    const corrections::Correction& c = driven.correction;
    const QString name = driven.instance->symbol ? QString::fromStdString(driven.instance->symbol->name) : QString("?");
    const QString range = QString("%1 to %2").arg(degrees(c.from), degrees(c.to));
    switch (c.property)
    {
    case corrections::Property::Frame:
        return QString("Drawing swap: %1, frame %2 to %3, as the bone turns %4")
            .arg(name)
            .arg(std::lround(c.valueFrom))
            .arg(std::lround(c.valueTo))
            .arg(range);
    case corrections::Property::Alpha:
        return QString("Fade: %1, %2% to %3%, as the bone turns %4")
            .arg(name)
            .arg(std::lround(c.valueFrom))
            .arg(std::lround(c.valueTo))
            .arg(range);
    default:
        return QString("Shape correction: %1 points of the boned shape, in full at %2 (from %3)")
            .arg(c.shifts.size())
            .arg(degrees(c.to), degrees(c.from));
    }
}

/// The joints a bone turns about, each named by the bone: by its number,
/// and in an armature of symbols, by the symbol it carries.
std::vector<std::pair<int, QString>> bonesOf(const fla::IKArmature& armature, const fla::Frame& pose)
{
    std::vector<std::pair<int, QString>> bones;
    const std::vector<fla::ArmatureJoint> joints = armature.symbolJoints();
    for (size_t j = 0; j < joints.size(); ++j)
    {
        int child = -1;
        for (size_t c = 0; c < joints.size() && child < 0; ++c)
            if (joints[c].parent == static_cast<int>(j))
                child = static_cast<int>(c);
        if (child < 0)
            continue;
        QString label = QString("Bone %1").arg(child);
        const std::string reference = armature.referenceOf(child);
        for (const fla::Element* element : reference.empty() ? std::vector<fla::Element*>() : pose.elements)
        {
            const auto* instance = dynamic_cast<const fla::SymbolInstance*>(element);
            if (!instance || !instance->symbol)
                continue;
            for (const auto& attribute : instance->extraAttributes)
                if (attribute.first == "referenceID" && attribute.second == reference)
                    label += QString(" (%1)").arg(QString::fromStdString(instance->symbol->name));
        }
        bones.emplace_back(static_cast<int>(j), label);
    }
    return bones;
}

/// Asks for a correction's angles and values. \a targets, when given, are
/// what it may go on (\a chosen the one picked); \a frames the frames of
/// each target's symbol for a drawing swap.
bool askCorrection(QWidget* parent, const QString& title, corrections::Correction& c, const std::vector<Target>* targets,
    int& chosen)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    QFormLayout* form = new QFormLayout(&dialog);
    QComboBox* target = nullptr;
    if (targets)
    {
        target = new QComboBox(&dialog);
        for (const Target& each : *targets)
            target->addItem(each.label);
        target->setCurrentIndex(std::max(0, chosen));
        form->addRow(c.property == corrections::Property::Frame ? "Graphic" : "Instance", target);
    }
    HotDoubleSpinBox* from = new HotDoubleSpinBox(&dialog);
    HotDoubleSpinBox* to = new HotDoubleSpinBox(&dialog);
    for (HotDoubleSpinBox* box : {from, to})
    {
        box->setRange(-180.0, 180.0);
        box->setDecimals(1);
        box->setSuffix(kDegree);
    }
    from->setValue(c.from);
    to->setValue(c.to);
    from->setToolTip("The bone's angle where the correction starts: from the bone above, or its own at the top of the armature");
    to->setToolTip("The bone's angle where the correction is in full");
    form->addRow("Bone from", from);
    form->addRow("Bone to", to);
    HotSpinBox* valueFrom = nullptr;
    HotSpinBox* valueTo = nullptr;
    if (c.property != corrections::Property::Shape)
    {
        valueFrom = new HotSpinBox(&dialog);
        valueTo = new HotSpinBox(&dialog);
        const bool frames = c.property == corrections::Property::Frame;
        const auto limit = [=]() {
            int most = 100;
            if (frames)
            {
                const int at = target ? target->currentIndex() : -1;
                most = targets && at >= 0 && static_cast<size_t>(at) < targets->size() && (*targets)[static_cast<size_t>(at)].instance->symbol
                    ? symbolLength(*(*targets)[static_cast<size_t>(at)].instance->symbol) : 9999;
            }
            valueFrom->setRange(frames ? 1 : 0, most);
            valueTo->setRange(frames ? 1 : 0, most);
        };
        limit();
        if (target)
            QObject::connect(target, &QComboBox::currentIndexChanged, &dialog, [=]() {
                limit();
                if (frames)
                    valueTo->setValue(valueTo->maximum());
            });
        valueFrom->setValue(static_cast<int>(std::lround(c.valueFrom)));
        valueTo->setValue(static_cast<int>(std::lround(c.valueTo)));
        if (!frames)
        {
            valueFrom->setSuffix("%");
            valueTo->setSuffix("%");
        }
        valueFrom->setToolTip(frames ? "The frame of the graphic shown at the first angle" : "The alpha at the first angle");
        valueTo->setToolTip(frames ? "The frame of the graphic shown at the second angle; those between show in turn"
                                   : "The alpha at the second angle");
        form->addRow(frames ? "Show frame" : "Alpha", valueFrom);
        form->addRow(frames ? "To frame" : "To alpha", valueTo);
    }
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    c.from = from->value();
    c.to = to->value();
    if (valueFrom && valueTo)
    {
        c.valueFrom = valueFrom->value();
        c.valueTo = valueTo->value();
    }
    if (target)
        chosen = target->currentIndex();
    return true;
}

} // namespace

BoneCorrector::BoneCorrector(QWidget* window, PhoenixView* view, fla::CommandStack* stack,
    std::function<void(const QString&, int)> status)
    : QObject(window)
    , _window(window)
    , _view(view)
    , _stack(stack)
    , _status(std::move(status))
{
}

BoneCorrector::~BoneCorrector() = default;

void BoneCorrector::changed()
{
    _view->clearCaches();
    _view->update();
}

void BoneCorrector::open(fla::Frame* pose, size_t child, int joint)
{
    if (_session)
    {
        _status("A shape correction is being drawn: leave the boned shape to keep it first.", 4000);
        return;
    }
    fla::IKArmature armature;
    fla::Timeline* timeline = timelineOf(pose);
    if (!pose || !timeline || child >= pose->extraChildren.size() || !armature.parse(pose->extraChildren[child]))
        return;
    const std::vector<std::pair<int, QString>> bones = bonesOf(armature, *pose);
    if (bones.empty())
        return;
    const std::string armatureName = armature.name();
    const std::vector<fla::ArmatureJoint> joints = armature.symbolJoints();
    fla::SymbolInstance* container = corrections::containerOn(*pose);

    QDialog dialog(_window);
    dialog.setWindowTitle("Bone Corrections");
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QLabel* intro = new QLabel("As a bone turns, it can correct the shape it bends, swap a graphic's drawing or fade an "
                               "instance. Turn the bone to where a correction is in full, then add it.",
        &dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    QFormLayout* form = new QFormLayout;
    QComboBox* bone = new QComboBox(&dialog);
    for (const auto& [index, label] : bones)
        bone->addItem(label, index);
    // Opened for the armature, the first bone that drives something.
    if (joint < 0)
    {
        const std::set<std::string> driving = corrections::drivingJoints(*timeline, armatureName);
        for (size_t j = 0; j < joints.size() && joint < 0; ++j)
            if (driving.count(joints[j].node) && bone->findData(static_cast<int>(j)) >= 0)
                joint = static_cast<int>(j);
    }
    const int start = bone->findData(joint);
    bone->setCurrentIndex(start >= 0 ? start : 0);
    form->addRow("Bone", bone);
    QLabel* angle = new QLabel(&dialog);
    form->addRow("Angle", angle);
    layout->addLayout(form);
    QListWidget* list = new QListWidget(&dialog);
    list->setMinimumSize(460, 140);
    layout->addWidget(list);
    QHBoxLayout* row = new QHBoxLayout;
    QPushButton* addShape = new QPushButton("Add Shape Correction", &dialog);
    addShape->setToolTip(container ? "Draw how the boned shape looks with the bone turned as it is here"
                                   : "Shape corrections are for boned shapes; this armature joins symbols");
    addShape->setEnabled(container != nullptr);
    QPushButton* addSwap = new QPushButton("Add Drawing Swap...", &dialog);
    addSwap->setToolTip("Show a graphic's frames in turn as the bone turns: a hand turning, a head turning");
    QPushButton* addFade = new QPushButton("Add Fade...", &dialog);
    addFade->setToolTip("Fade an instance in or out as the bone turns");
    QPushButton* edit = new QPushButton("Edit...", &dialog);
    QPushButton* remove = new QPushButton("Remove", &dialog);
    for (QPushButton* button : {addShape, addSwap, addFade, edit, remove})
    {
        button->setAutoDefault(false);
        row->addWidget(button);
    }
    layout->addLayout(row);
    QDialogButtonBox* close = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(close);

    const int frame = _view->player() ? _view->player()->currentFrame() : 0;
    const int into = std::clamp(frame - pose->index, 0, std::max(0, armature.frames() - 1));
    const auto chosenJoint = [=]() { return bone->currentData().toInt(); };
    const auto nodeOf = [=](int j) {
        return j >= 0 && static_cast<size_t>(j) < joints.size() ? joints[static_cast<size_t>(j)].node : std::string();
    };
    const auto angleAt = [&](int at) { return corrections::jointAngle(corrections::jointsAt(armature, at), chosenJoint()); };
    std::vector<Driven> shown;
    const auto refresh = [&]() {
        const int at = list->currentRow();
        angle->setText(QString("%1 here, %2 on the armature's first frame").arg(degrees(angleAt(into)), degrees(angleAt(0))));
        shown = drivenBy(*timeline, armatureName, nodeOf(chosenJoint()));
        list->clear();
        for (const Driven& each : shown)
            list->addItem(describe(each));
        list->setCurrentRow(std::min(at, list->count() - 1));
        edit->setEnabled(list->currentRow() >= 0);
        remove->setEnabled(list->currentRow() >= 0);
        _view->update();
    };
    connect(bone, &QComboBox::currentIndexChanged, &dialog, refresh);
    connect(list, &QListWidget::currentRowChanged, &dialog, [=](int at) {
        edit->setEnabled(at >= 0);
        remove->setEnabled(at >= 0);
    });

    const auto store = [this](fla::SymbolInstance* instance, const std::vector<corrections::Correction>& corrections,
                           const char* name) {
        const Attributes after = corrections::withCorrections(*instance, corrections);
        _stack->push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(instance->extraAttributes,
            instance->extraAttributes, after, name)));
        _stack->breakMergeChain();
        changed();
    };
    // A new correction's angles: from where the bone rests to where it is
    // turned now, or a quarter turn on when it is not turned.
    const auto fresh = [&](corrections::Property property) {
        corrections::Correction c;
        c.armature = armatureName;
        c.joint = nodeOf(chosenJoint());
        c.property = property;
        c.from = std::round(angleAt(0) * 10.0) / 10.0;
        const double now = std::round(angleAt(into) * 10.0) / 10.0;
        c.to = std::fabs(now - c.from) >= 5.0 ? now : std::clamp(c.from + 90.0, -180.0, 180.0);
        return c;
    };
    const auto addTo = [&](corrections::Property property, const char* title, const char* step) {
        const std::vector<Target> targets = targetsAt(*timeline, frame, property == corrections::Property::Frame);
        if (targets.empty())
        {
            _status(property == corrections::Property::Frame
                    ? "There is no graphic symbol instance at the playhead on this timeline to swap the drawing of."
                    : "There is no symbol instance at the playhead on this timeline to fade.",
                5000);
            return;
        }
        corrections::Correction c = fresh(property);
        int chosen = 0;
        const fla::SymbolInstance* first = targets.front().instance;
        c.valueFrom = property == corrections::Property::Frame ? first->firstFrame + 1 : 100.0;
        c.valueTo = property == corrections::Property::Frame && first->symbol ? symbolLength(*first->symbol) : 0.0;
        if (!askCorrection(&dialog, title, c, &targets, chosen) || chosen < 0 || static_cast<size_t>(chosen) >= targets.size())
            return;
        fla::SymbolInstance* instance = targets[static_cast<size_t>(chosen)].instance;
        std::vector<corrections::Correction> now = corrections::of(*instance);
        now.push_back(c);
        store(instance, now, step);
        refresh();
        list->setCurrentRow(list->count() - 1);
    };
    connect(addSwap, &QPushButton::clicked, &dialog,
        [&]() { addTo(corrections::Property::Frame, "Add Drawing Swap", "Add Drawing Swap"); });
    connect(addFade, &QPushButton::clicked, &dialog, [&]() { addTo(corrections::Property::Alpha, "Add Fade", "Add Fade"); });
    int drawShape = -1;
    connect(addShape, &QPushButton::clicked, &dialog, [&]() {
        drawShape = chosenJoint();
        dialog.accept();
    });
    connect(edit, &QPushButton::clicked, &dialog, [&]() {
        const int at = list->currentRow();
        if (at < 0 || static_cast<size_t>(at) >= shown.size())
            return;
        Driven& driven = shown[static_cast<size_t>(at)];
        corrections::Correction c = driven.correction;
        int chosen = -1;
        if (!askCorrection(&dialog, "Edit Bone Correction", c, nullptr, chosen))
            return;
        std::vector<corrections::Correction> now = corrections::of(*driven.instance);
        if (driven.index >= now.size())
            return;
        now[driven.index] = c;
        store(driven.instance, now, "Edit Bone Correction");
        refresh();
    });
    connect(remove, &QPushButton::clicked, &dialog, [&]() {
        const int at = list->currentRow();
        if (at < 0 || static_cast<size_t>(at) >= shown.size())
            return;
        const Driven& driven = shown[static_cast<size_t>(at)];
        std::vector<corrections::Correction> now = corrections::of(*driven.instance);
        if (driven.index >= now.size())
            return;
        now.erase(now.begin() + static_cast<std::ptrdiff_t>(driven.index));
        store(driven.instance, now, "Remove Bone Correction");
        refresh();
    });
    refresh();
    list->setCurrentRow(list->count() > 0 ? 0 : -1);
    dialog.exec();
    if (drawShape >= 0)
        startShape(pose, child, drawShape);
}

void BoneCorrector::startShape(fla::Frame* pose, size_t child, int joint)
{
    fla::IKArmature armature;
    fla::SymbolInstance* container = pose ? corrections::containerOn(*pose) : nullptr;
    if (!container || child >= pose->extraChildren.size() || !armature.parse(pose->extraChildren[child]) ||
        !armature.bendsShape())
        return;
    const std::vector<fla::ArmatureJoint> joints = armature.symbolJoints();
    if (joint < 0 || static_cast<size_t>(joint) >= joints.size())
        return;
    const fla::Document* document = _view->flaDocument() ? _view->flaDocument()->document : nullptr;
    const fla::Timeline* here = _view->scopeTimeline();
    if (!here && document)
        here = document->currentScene();
    if (here != timelineOf(pose))
    {
        _status("Go to the timeline the armature is on to draw a correction.", 4000);
        return;
    }
    const int frame = _view->player() ? _view->player()->currentFrame() : 0;
    const int into = frame - pose->index;
    if (into < 0 || into >= armature.frames())
    {
        _status("Move the playhead into the armature's span, to a frame where the bone is turned.", 4000);
        return;
    }
    const std::string node = joints[static_cast<size_t>(joint)].node;
    const double now = corrections::jointAngle(corrections::jointsAt(armature, into), joint);
    std::vector<corrections::Correction> list = corrections::of(*container);
    double from = corrections::jointAngle(corrections::jointsAt(armature, 0), joint);
    for (const corrections::Correction& each : list)
        if (each.property == corrections::Property::Shape && each.joint == node)
            from = each.from;
    if (std::fabs(now - from) < 2.0)
    {
        _status(QString("The bone is at %1, where the correction starts. Turn it with the Selection tool to where the "
                        "correction is in full, then add the correction.")
                    .arg(degrees(now)),
            7000);
        return;
    }
    // Every frame of the container shows a keyframe of its own.
    const std::vector<fla::Layer*>& layers = container->symbol->timelines.front()->layers;
    for (const fla::Layer* layer : layers)
    {
        const fla::Frame* shown = layer ? frameAt(*layer, container->firstFrame + into) : nullptr;
        if (shown && shown->index != container->firstFrame + into)
        {
            _status("The boned shape holds one drawing over several frames here, so it cannot be corrected on this one.",
                5000);
            return;
        }
    }

    _session = std::make_unique<Session>();
    Session& s = *_session;
    s.document = _view->flaDocument();
    s.pose = pose;
    s.container = container;
    s.xml = pose->extraChildren[child];
    s.armature = armature.name();
    s.node = node;
    s.into = into;
    s.from = from;
    s.to = std::round(now * 10.0) / 10.0;
    s.depth = _view->scopeDepth();
    s.sceneFrame = frame;
    s.entry = _stack->index();
    _view->pauseCorrections(container);
    // Each shape drawn as the corrections make it on this frame, to be
    // moved further; the others' part kept to tell this one's from.
    for (size_t li = 0; li < layers.size(); ++li)
    {
        for (int ei = 0;; ++ei)
        {
            const fla::Frame* shown = layers[li] ? frameAt(*layers[li], container->firstFrame + into) : nullptr;
            if (!shown || static_cast<size_t>(ei) >= shown->elements.size())
                break;
            fla::Shape* stored = shapeIn(*container, static_cast<int>(li), container->firstFrame + into, ei);
            const fla::Shape* rest = shapeIn(*container, static_cast<int>(li), container->firstFrame, ei);
            if (!stored || !rest)
                continue;
            std::unique_ptr<fla::Shape> base(fla::cloneShape(*stored, nullptr));
            base->transform = stored->transform;
            corrections::correctShape(*base, *rest, *container, armature, into, static_cast<int>(li), ei, list, node);
            std::unique_ptr<fla::Shape> start(fla::cloneShape(*stored, nullptr));
            start->transform = stored->transform;
            if (corrections::correctShape(*start, *rest, *container, armature, into, static_cast<int>(li), ei, list))
                _stack->push(fla::CommandPtr(new fla::SetShapeContentsCommand(stored, *start, "Draw Bone Correction")));
            s.bases[{static_cast<int>(li), ei}] = std::move(base);
        }
    }
    s.bone = QString("Bone %1").arg(joint);
    for (size_t c = 0; c < joints.size(); ++c)
        if (joints[c].parent == joint)
        {
            s.bone = QString("Bone %1").arg(c);
            break;
        }
    _view->enterSymbol(container);
    if (_view->player())
        _view->player()->setCurrentFrame(container->firstFrame + into);
    changed();
    _status(QString("Drawing the correction for %1 at %2: move the shape's points with the Subselection tool, then "
                    "leave the symbol to keep it.")
                .arg(s.bone, degrees(s.to)),
        0);
}

void BoneCorrector::scopeChanged()
{
    if (_session && _view->scopeDepth() <= _session->depth)
        finish();
}

void BoneCorrector::finish()
{
    std::unique_ptr<Session> session = std::move(_session);
    if (!session)
        return;
    Session& s = *session;
    _view->pauseCorrections(nullptr);
    if (_view->flaDocument() != s.document)
        return;
    fla::IKArmature armature;
    if (!armature.parse(s.xml))
        return;
    // What was drawn, before the edits go back into the one step.
    std::vector<corrections::PointShift> shifts;
    bool fits = true;
    for (const auto& [where, base] : s.bases)
    {
        const fla::Shape* drawn = shapeIn(*s.container, where.first, s.container->firstFrame + s.into, where.second);
        const fla::Shape* rest = shapeIn(*s.container, where.first, s.container->firstFrame, where.second);
        if (drawn && rest)
            fits = corrections::recordShifts(*base, *drawn, *rest, *s.container, armature, s.into, where.first,
                       where.second, shifts) && fits;
    }
    while (_stack->index() > s.entry && _stack->canUndo())
        _stack->undo();
    if (_view->player())
        _view->player()->setCurrentFrame(s.sceneFrame);
    if (!fits)
    {
        changed();
        _status("Points were added or taken away while drawing, so the correction was not kept: move the shape's "
                "points only.",
            7000);
        return;
    }
    std::vector<corrections::Correction> list = corrections::of(*s.container);
    auto old = std::find_if(list.begin(), list.end(), [&](const corrections::Correction& each) {
        return each.property == corrections::Property::Shape && each.joint == s.node && each.armature == s.armature;
    });
    const bool had = old != list.end();
    if (shifts.empty() && !had)
    {
        changed();
        _status("Nothing was moved, so no correction was added.", 4000);
        return;
    }
    corrections::Correction c;
    c.armature = s.armature;
    c.joint = s.node;
    c.property = corrections::Property::Shape;
    c.from = std::round(s.from * 10.0) / 10.0;
    c.to = s.to;
    c.shifts = shifts;
    if (had && shifts.empty())
        list.erase(old);
    else if (had)
        *old = c;
    else
        list.push_back(c);
    const Attributes after = corrections::withCorrections(*s.container, list);
    _stack->push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(s.container->extraAttributes,
        s.container->extraAttributes, after, shifts.empty() ? "Remove Bone Correction" : "Bone Correction")));
    _stack->breakMergeChain();
    changed();
    _status(shifts.empty() ? QString("The shape correction for %1 is taken away.").arg(s.bone)
                           : QString("The shape correction blends in as %1 turns from %2 to %3.")
                                 .arg(s.bone, degrees(c.from), degrees(c.to)),
        6000);
}
