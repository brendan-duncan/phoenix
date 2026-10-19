#include "node_graph_panel.h"

#include "hot_text.h"
#include "node_graph_style.h"
#include "theme.h"

#include "../data/timeline.h"
#include "../edit/command_stack.h"
#include "../edit/property_command.h"
#include "swatch_popup.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace {

using Attributes = std::vector<std::pair<std::string, std::string>>;

/// Steps of a node graph's own (moves, links) never merge; a setting
/// dragged merges into one step.
constexpr int kMergeSetting = 7411;

/// A label's unit, made a suffix: "Amount (percent)" is "Amount" in %.
QString unitOf(QString& label)
{
    static const std::pair<const char*, const char*> units[] = {{" (percent)", "%"}, {" (degrees)", "\xc2\xb0"},
        {" (pixels)", " px"}, {" (seconds)", " s"}, {" (frames)", " f"}};
    for (const auto& [words, suffix] : units)
        if (label.endsWith(words))
        {
            label.chop(static_cast<int>(std::strlen(words)));
            return QString::fromUtf8(suffix);
        }
    return QString();
}

QIcon swatchIcon(const QColor& color)
{
    QPixmap pixmap(34, 18);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(theme::color("swatchBorder"), 1.0));
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(0.5, 0.5, 33, 17), 4, 4);
    painter.end();
    return QIcon(pixmap);
}

QToolButton* barButton(QWidget* parent, const QString& text, const QString& tip)
{
    auto* button = new QToolButton(parent);
    button->setText(text);
    button->setToolTip(tip);
    button->setAutoRaise(true);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    return button;
}

} // namespace

NodeGraphPanel::NodeGraphPanel(NodeGraphLinks links, QWidget* parent)
    : QWidget(parent)
    , _links(std::move(links))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // The bar: what is shown, and what to do with it.
    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(8, 3, 4, 3);
    bar->setSpacing(2);
    _title = new QLabel(this);
    QFont bold = _title->font();
    bold.setBold(true);
    _title->setFont(bold);
    bar->addWidget(_title);
    bar->addSpacing(8);
    _enabled = barButton(this, "On", "Composite the timeline through its graph; off, its layers stack as usual");
    _enabled->setCheckable(true);
    bar->addWidget(_enabled);
    bar->addStretch(1);
    _add = barButton(this, "+ Add Node", "Add a node (Tab)");
    _frame = barButton(this, "Frame", "Frame the selection, or the whole graph (F)");
    _tidy = barButton(this, "Line Up", "Line up the selected nodes, or all of them, by how they link (L)");
    _pictures = barButton(this, "Pictures", "Show what each node gives (H hides a node's own)");
    _pictures->setCheckable(true);
    _pictures->setChecked(true);
    _minimap = barButton(this, "Map", "Show a map of the whole graph in the corner");
    _minimap->setCheckable(true);
    _minimap->setChecked(true);
    _more = barButton(this, QString::fromUtf8("\xe2\x8b\xaf"), "More");
    _more->setPopupMode(QToolButton::InstantPopup);
    auto* moreMenu = new QMenu(_more);
    _more->setMenu(moreMenu);
    for (QToolButton* button : {_add, _frame, _tidy, _pictures, _minimap, _more})
        bar->addWidget(button);
    _enabled->setObjectName("nodeGraphOn");
    _add->setObjectName("nodeGraphAdd");
    layout->addLayout(bar);
    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Plain);
    line->setStyleSheet(QString("color: %1;").arg(theme::color("nodes.border").name()));
    layout->addWidget(line);

    _split = new QSplitter(Qt::Horizontal, this);
    _split->setChildrenCollapsible(true);
    _view = new NodeGraphView(_split);
    _view->setObjectName("nodeGraphView");
    _inspectorScroll = new QScrollArea(_split);
    _inspectorScroll->setWidgetResizable(true);
    _inspectorScroll->setFrameShape(QFrame::NoFrame);
    _inspectorScroll->setMinimumWidth(220);
    _inspectorScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _split->addWidget(_view);
    _split->addWidget(_inspectorScroll);
    _split->setStretchFactor(0, 1);
    _split->setStretchFactor(1, 0);
    _split->setSizes({640, 260});
    layout->addWidget(_split, 1);

    _pictureTimer = new QTimer(this);
    _pictureTimer->setSingleShot(true);
    _pictureTimer->setInterval(50);
    connect(_pictureTimer, &QTimer::timeout, this, [this]() {
        if (!_view->hasGraph() || !_links.pictures || !isVisible())
            return;
        const double aspect = _links.stageAspect ? _links.stageAspect() : 16.0 / 9.0;
        _view->setStageAspect(aspect);
        // Twice the size a node shows them, for sharp pictures when zoomed.
        const int w = 336;
        const int h = std::max(32, static_cast<int>(std::lround(std::clamp(w / aspect, 96.0, 336.0))));
        QMap<int, QString> problems;
        const QMap<int, QImage> pictures = _links.pictures(_view->graph(), QSize(w, h), &problems);
        _view->setPictures(pictures, problems);
    });

    connect(_view, &NodeGraphView::edited, this,
        [this](const fla::nodegraph::Graph& graph, const QString& name, bool merge) { apply(&graph, name, merge); });
    connect(_view, &NodeGraphView::selectionChanged, this, [this]() { fillInspector(); });
    connect(_view, &NodeGraphView::viewRequested, this, [this](int node) {
        _view->setViewed(node);
        if (_links.view)
            _links.view(node);
    });
    connect(_view, &NodeGraphView::layerRequested, this, [this](const QString& layer) {
        if (_links.pickLayer && !layer.isEmpty())
            _links.pickLayer(layer);
    });
    connect(_view, &NodeGraphView::createRequested, this, [this]() {
        const fla::nodegraph::Graph graph = fla::nodegraph::starter();
        apply(&graph, "Create Node Graph", false);
        refresh();
        _view->frameAll(false);
    });
    connect(_enabled, &QToolButton::toggled, this, [this](bool on) {
        if (!_view->hasGraph() || _view->graph().enabled == on)
            return;
        fla::nodegraph::Graph graph = _view->graph();
        graph.enabled = on;
        apply(&graph, on ? "Turn Node Graph On" : "Turn Node Graph Off", false);
        refresh();
    });
    connect(_add, &QToolButton::clicked, this, [this]() { _view->addNodeMenu(); });
    connect(_frame, &QToolButton::clicked, this, [this]() { _view->frame(); });
    connect(_tidy, &QToolButton::clicked, this, [this]() { _view->tidy(); });
    connect(_pictures, &QToolButton::toggled, this, [this](bool on) { _view->setShowPictures(on); });
    connect(_minimap, &QToolButton::toggled, this, [this](bool on) { _view->setShowMinimap(on); });
    moreMenu->addAction("Add Backdrop", this, [this]() { _view->backdropSelection(); });
    moreMenu->addAction("Frame All", this, [this]() { _view->frameAll(); });
    moreMenu->addSeparator();
    moreMenu->addAction("Remove Node Graph...", this, [this]() {
        if (!_view->hasGraph())
            return;
        apply(nullptr, "Remove Node Graph", false);
        refresh();
    });
    connect(theme::notifier(), &theme::Notifier::changed, this, [this]() {
        fillInspector();
        update();
    });
    refresh();
}

void NodeGraphPanel::refresh()
{
    if (_changing)
        return;
    fla::Timeline* timeline = _links.timeline ? _links.timeline() : nullptr;
    if (timeline != _shown)
    {
        if (_shown)
            _views[_shown] = _view->view();
        _shown = timeline;
        _inspected = -1;
        _view->setViewed(0);
        if (_links.view)
            _links.view(0);
    }
    const std::shared_ptr<const fla::nodegraph::Graph> graph = timeline ? fla::nodegraph::of(*timeline) : nullptr;
    const bool had = _view->hasGraph();
    _view->set(timeline ? graph.get() : nullptr);
    if (graph && _views.contains(timeline) && !had)
        _view->setView(_views.value(timeline));
    _inspected = -1;
    fillInspector();
    updateBar();
    refreshPictures();
}

void NodeGraphPanel::refreshPictures()
{
    if (_view->hasGraph())
        _pictureTimer->start();
}

void NodeGraphPanel::updateBar()
{
    const bool has = _view->hasGraph();
    const QString name = _links.timelineName ? _links.timelineName() : QString();
    _title->setText(name.isEmpty() ? QString("Node Graph") : name);
    _enabled->setVisible(has);
    {
        const QSignalBlocker block(_enabled);
        _enabled->setChecked(has && _view->graph().enabled);
    }
    _enabled->setText(has && _view->graph().enabled ? "On" : "Off");
    const QColor accent = theme::color("accent");
    _enabled->setStyleSheet(_enabled->isChecked()
                                ? QString("QToolButton { background: %1; color: white; border-radius: 9px; padding: 1px 10px; }")
                                      .arg(accent.name())
                                : QString("QToolButton { border: 1px solid %1; border-radius: 9px; padding: 0px 9px; }")
                                      .arg(theme::color("nodes.border").name()));
    for (QToolButton* button : {_add, _frame, _tidy, _pictures, _minimap, _more})
        button->setEnabled(has);
    // With no graph, the view's own invitation is all there is to show.
    _inspectorScroll->setVisible(has);
}

void NodeGraphPanel::apply(const fla::nodegraph::Graph* graph, const QString& name, bool merge)
{
    fla::Timeline* timeline = _links.timeline ? _links.timeline() : nullptr;
    fla::CommandStack* stack = _links.commands ? _links.commands() : nullptr;
    if (!timeline || !stack)
        return;
    const Attributes after = fla::nodegraph::withGraph(*timeline, graph);
    if (after == timeline->extraAttributes)
        return;
    _changing = true;
    stack->push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(timeline->extraAttributes,
        timeline->extraAttributes, after, name.toStdString(), merge ? kMergeSetting : -1)));
    _changing = false;
    if (graph)
        _view->set(graph);
    updateBar();
    if (_links.changed)
        _links.changed();
    refreshPictures();
}

void NodeGraphPanel::setSetting(int id, const std::string& name, const std::string& value, bool merge)
{
    fla::nodegraph::Graph graph = _view->graph();
    fla::nodegraph::Node* node = graph.node(id);
    if (!node)
        return;
    const int frame = _links.currentFrame ? _links.currentFrame() : 0;
    if (node->applied.keyed(name))
        node->applied.setKey(name, frame, value);
    else
        node->applied.params[name] = value;
    apply(&graph, "Set " + QString::fromStdString(name), merge);
}

void NodeGraphPanel::toggleKey(int id, const std::string& name)
{
    fla::nodegraph::Graph graph = _view->graph();
    fla::nodegraph::Node* node = graph.node(id);
    if (!node)
        return;
    const int frame = _links.currentFrame ? _links.currentFrame() : 0;
    const std::string now = node->applied.valueAt(name, frame, std::string());
    bool here = false;
    for (const auto& key : node->applied.keys(name))
        here = here || key.first == frame;
    if (here)
        node->applied.removeKey(name, frame, now);
    else
        node->applied.setKey(name, frame, now);
    apply(&graph, here ? "Remove Key" : "Add Key", false);
    _inspected = -1;
    fillInspector();
}

void NodeGraphPanel::fillInspector()
{
    const int id = _view->currentNode();
    if (id == _inspected && _inspector)
        return;
    _inspected = id;
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    const QColor dim = theme::color("nodes.dimText");
    const auto dimLabel = [&](const QString& text) {
        auto* label = new QLabel(text, page);
        label->setWordWrap(true);
        label->setStyleSheet(QString("color: %1;").arg(dim.name()));
        return label;
    };
    const fla::nodegraph::Node* node = _view->graph().node(id);
    if (!_view->hasGraph())
    {
        layout->addWidget(dimLabel("This timeline has no node graph. Create one to composite its layers through "
                                   "nodes: effects, merges, mattes and transforms."));
    }
    else if (!node)
    {
        const int count = _view->selectedNodes().size();
        auto* heading = new QLabel(count > 1 ? QString("%1 nodes selected").arg(count) : QString("Node Graph"), page);
        QFont font = heading->font();
        font.setBold(true);
        font.setPointSizeF(font.pointSizeF() * 1.1);
        heading->setFont(font);
        layout->addWidget(heading);
        layout->addWidget(dimLabel(count > 1 ? "Select one node to set it up."
                                             : "Select a node to set it up here. Layer nodes take a layer out of the "
                                               "stack; Other Layers brings in the rest; the Output is what the "
                                               "timeline shows."));
        layout->addSpacing(6);
        const char* keys[][2] = {{"Tab", "Add a node"}, {"Drag a pin", "Link nodes"}, {"Alt+click a link", "Unlink"},
            {"D", "Bypass"}, {"V", "Show on the stage"}, {"H", "Hide the picture"}, {"F", "Frame"},
            {"L", "Line up"}, {"B", "Backdrop"}, {"Ctrl+D", "Duplicate"}, {"Space+drag", "Pan"}};
        auto* grid = new QGridLayout;
        grid->setHorizontalSpacing(12);
        grid->setVerticalSpacing(4);
        int row = 0;
        for (const auto& key : keys)
        {
            auto* chip = new QLabel(key[0], page);
            chip->setStyleSheet(QString("color: %1; border: 1px solid %2; border-radius: 4px; padding: 1px 5px;")
                                    .arg(theme::color("nodes.text").name(), theme::color("nodes.border").name()));
            grid->addWidget(chip, row, 0, Qt::AlignLeft);
            grid->addWidget(dimLabel(key[1]), row, 1);
            ++row;
        }
        layout->addLayout(grid);
    }
    else
    {
        const fla::nodegraph::Type* type = fla::nodegraph::type(node->type());
        const std::string category = type ? type->category : std::string("Utility");
        // The heading: its icon on its color, its name, its kind.
        auto* head = new QHBoxLayout;
        auto* chip = new QLabel(page);
        QPixmap icon(28, 28);
        icon.fill(Qt::transparent);
        {
            QPainter painter(&icon);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(Qt::NoPen);
            painter.setBrush(nodestyle::categoryColor(category));
            painter.drawRoundedRect(QRectF(0, 0, 28, 28), 7, 7);
            nodestyle::drawIcon(painter, QRectF(6, 6, 16, 16), category, Qt::white);
        }
        chip->setPixmap(icon);
        head->addWidget(chip);
        auto* names = new QVBoxLayout;
        names->setSpacing(0);
        auto* name = new QLineEdit(QString::fromStdString(fla::nodegraph::title(*node)), page);
        name->setFrame(false);
        QFont big = name->font();
        big.setBold(true);
        big.setPointSizeF(big.pointSizeF() * 1.12);
        name->setFont(big);
        name->setStyleSheet("background: transparent;");
        connect(name, &QLineEdit::editingFinished, this, [this, id, name]() {
            fla::nodegraph::Graph graph = _view->graph();
            fla::nodegraph::Node* named = graph.node(id);
            if (!named)
                return;
            fla::nodegraph::Node plain = *named;
            plain.label.clear();
            const std::string text = name->text().trimmed().toStdString();
            const std::string label = text.empty() || text == fla::nodegraph::title(plain) ? std::string() : text;
            if (label != named->label)
            {
                named->label = label;
                apply(&graph, "Rename Node", false);
            }
        });
        names->addWidget(name);
        names->addWidget(dimLabel(QString::fromStdString(type ? type->name + "  \xc2\xb7  " + category : node->type())));
        head->addLayout(names, 1);
        layout->addLayout(head);
        if (type && !type->description.empty())
            layout->addWidget(dimLabel(QString::fromStdString(type->description)));
        if (node->type() != "output" && node->type() != "dot")
        {
            auto* bypass = new QCheckBox("Bypass", page);
            bypass->setToolTip("Pass the first input through unchanged (D)");
            bypass->setChecked(node->bypassed());
            connect(bypass, &QCheckBox::toggled, this, [this, id](bool on) {
                fla::nodegraph::Graph graph = _view->graph();
                if (fla::nodegraph::Node* changed = graph.node(id); changed && changed->bypassed() != on)
                {
                    changed->applied.enabled = !on;
                    apply(&graph, on ? "Bypass Node" : "Turn On Node", false);
                }
            });
            layout->addWidget(bypass);
        }
        auto* line = new QFrame(page);
        line->setFrameShape(QFrame::HLine);
        line->setStyleSheet(QString("color: %1;").arg(theme::color("nodes.border").name()));
        layout->addWidget(line);

        // A row for each setting: its name over its control, and the key.
        const int frame = _links.currentFrame ? _links.currentFrame() : 0;
        auto* grid = new QGridLayout;
        grid->setHorizontalSpacing(6);
        grid->setVerticalSpacing(3);
        grid->setColumnStretch(0, 1);
        int row = 0;
        for (const fla::behaviors::ParamSpec& spec : type ? type->params : std::vector<fla::behaviors::ParamSpec>())
        {
            QString label = QString::fromStdString(spec.label);
            const QString suffix = unitOf(label);
            const std::string value = node->applied.valueAt(spec.name, frame, spec.text);
            const std::string name = spec.name;
            grid->addWidget(dimLabel(label), row, 0);
            // The key: here, elsewhere, or none.
            if (spec.type != "layer" && spec.type != "text")
            {
                bool here = false;
                const bool keyed = node->applied.keyed(name);
                for (const auto& key : node->applied.keys(name))
                    here = here || key.first == frame;
                auto* key = new QToolButton(page);
                key->setAutoRaise(true);
                // A filled diamond keyed here, a hollow one keyed elsewhere
                // or not at all, in the accent color once keyed.
                key->setText(here ? QString::fromUtf8("\xe2\x97\x86") : QString::fromUtf8("\xe2\x97\x87"));
                key->setToolTip(here ? "Keyed on this frame: click to take the key away"
                                : keyed ? "Keyed on other frames: click to key it here"
                                        : "Click to key it on this frame, so it can change over time");
                key->setStyleSheet(QString("QToolButton { color: %1; font-size: 13px; padding: 0 2px; }")
                                       .arg((here || keyed ? theme::color("accent") : dim).name()));
                connect(key, &QToolButton::clicked, this, [this, id, name]() { toggleKey(id, name); });
                grid->addWidget(key, row, 1, Qt::AlignRight);
            }
            ++row;
            QWidget* control = nullptr;
            if (spec.type == "choice")
            {
                auto* box = new QComboBox(page);
                for (const std::string& option : spec.choices)
                    box->addItem(QString::fromStdString(option));
                box->setCurrentText(QString::fromStdString(value));
                connect(box, &QComboBox::currentTextChanged, this,
                    [this, id, name](const QString& text) { setSetting(id, name, text.toStdString(), false); });
                control = box;
            }
            else if (spec.type == "checkbox")
            {
                auto* box = new QCheckBox(page);
                box->setChecked(value == "true" || value == "1");
                connect(box, &QCheckBox::toggled, this,
                    [this, id, name](bool on) { setSetting(id, name, on ? "true" : "false", false); });
                control = box;
            }
            else if (spec.type == "color")
            {
                auto* button = new QToolButton(page);
                const QColor color(QString::fromStdString(value));
                button->setIcon(swatchIcon(color.isValid() ? color : QColor(Qt::black)));
                button->setIconSize(QSize(34, 18));
                button->setText(QString::fromStdString(value).toUpper());
                button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
                button->setAutoRaise(true);
                connect(button, &QToolButton::clicked, this, [this, id, name, value, button]() {
                    const QColor chosen = SwatchPopup::getColor(button, nullptr, QColor(QString::fromStdString(value)), "Color", false);
                    if (chosen.isValid())
                        setSetting(id, name, chosen.name().toUpper().toStdString(), false);
                });
                control = button;
            }
            else if (spec.type == "layer")
            {
                auto* box = new QComboBox(page);
                box->setEditable(true);
                const QStringList layers = _links.layerNames ? _links.layerNames() : QStringList();
                box->addItems(layers);
                box->setCurrentText(QString::fromStdString(value));
                box->setPlaceholderText("Choose a layer");
                // A layer picked, or a name typed and finished.
                connect(box, &QComboBox::activated, this,
                    [this, id, name, box](int) { setSetting(id, name, box->currentText().toStdString(), false); });
                connect(box->lineEdit(), &QLineEdit::editingFinished, this,
                    [this, id, name, box]() { setSetting(id, name, box->currentText().toStdString(), false); });
                control = box;
            }
            else if (spec.type == "text")
            {
                auto* field = new QLineEdit(QString::fromStdString(value), page);
                connect(field, &QLineEdit::editingFinished, this,
                    [this, id, name, field]() { setSetting(id, name, field->text().toStdString(), false); });
                control = field;
            }
            else
            {
                // Numbers: hot text to drag or type, and a slider for a
                // range.
                auto* holder = new QWidget(page);
                auto* line = new QHBoxLayout(holder);
                line->setContentsMargins(0, 0, 0, 0);
                line->setSpacing(8);
                auto* hot = new HotText(holder);
                const bool whole = spec.step >= 1.0 || spec.type == "seed";
                hot->setDecimals(whole ? 0 : spec.step >= 0.1 ? 1 : spec.step >= 0.01 ? 2 : 3);
                hot->setRange(spec.min, spec.max);
                hot->setSingleStep(spec.step > 0 ? spec.step : 1.0);
                hot->setSuffix(suffix);
                hot->setValue(std::strtod(value.c_str(), nullptr));
                line->addWidget(hot);
                QSlider* slider = nullptr;
                if (spec.max > spec.min && spec.max - spec.min <= 2000.0 && spec.type != "seed")
                {
                    slider = new QSlider(Qt::Horizontal, holder);
                    slider->setRange(0, 1000);
                    const auto toSlider = [spec](double v) {
                        return static_cast<int>(std::lround((v - spec.min) / (spec.max - spec.min) * 1000.0));
                    };
                    slider->setValue(toSlider(hot->value()));
                    line->addWidget(slider, 1);
                    connect(slider, &QSlider::valueChanged, this, [this, id, name, spec, hot](int position) {
                        double v = spec.min + (spec.max - spec.min) * position / 1000.0;
                        if (spec.step > 0)
                            v = std::round(v / spec.step) * spec.step;
                        hot->setValue(v);
                        char text[32];
                        std::snprintf(text, sizeof(text), "%.10g", v);
                        setSetting(id, name, text, true);
                    });
                    connect(slider, &QSlider::sliderReleased, this, [this]() {
                        if (fla::CommandStack* stack = _links.commands ? _links.commands() : nullptr)
                            stack->breakMergeChain();
                    });
                    connect(hot, &HotText::valueChanged, slider, [slider, toSlider](double v) {
                        const QSignalBlocker block(slider);
                        slider->setValue(toSlider(v));
                    });
                }
                else
                    line->addStretch(1);
                connect(hot, &HotText::valueChanged, this, [this, id, name](double v, bool live) {
                    char text[32];
                    std::snprintf(text, sizeof(text), "%.10g", v);
                    setSetting(id, name, text, live);
                });
                connect(hot, &HotText::editFinished, this, [this]() {
                    if (fla::CommandStack* stack = _links.commands ? _links.commands() : nullptr)
                        stack->breakMergeChain();
                });
                control = holder;
            }
            grid->addWidget(control, row, 0, 1, 2);
            ++row;
            grid->setRowMinimumHeight(row, 6);
            ++row;
        }
        if (type && type->params.empty() && node->type() != "output")
            grid->addWidget(dimLabel("Nothing to set: link pictures into it."), row++, 0, 1, 2);
        layout->addLayout(grid);
        if (node->type() == "layer")
        {
            auto* pick = new QPushButton("Select the Layer", page);
            connect(pick, &QPushButton::clicked, this, [this, id]() {
                if (const fla::nodegraph::Node* layer = _view->graph().node(id); layer && _links.pickLayer)
                    _links.pickLayer(QString::fromStdString(layer->applied.valueAt("layer", 0, std::string())));
            });
            layout->addWidget(pick);
        }
    }
    layout->addStretch(1);
    // Fitted to the side's width; long words wrap rather than widen it.
    page->setMinimumWidth(0);
    page->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    _inspectorScroll->setWidget(page);
    _inspector = page;
}
