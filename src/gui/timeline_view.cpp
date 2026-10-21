#include "timeline_view.h"
#include "../data/layer.h"
#include "../data/timeline.h"
#include "layers_panel.h"
#include "timelines_panel.h"
#include "player.h"

#include <QScrollBar>
#include <QSettings>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <memory>

TimelineView::TimelineView(Player* player, QWidget *parent)
    : QWidget(parent)
    , _flaDocument(nullptr)
    , _player(player)
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    _splitter = new QSplitter(Qt::Horizontal, this);

    _layersPanel = new LayersPanel(this);
    _layersPanel->setMinimumWidth(150);

    _timelinesPanel = new TimelinesPanel(_player, this);
    // The transport row would otherwise hold the frames as wide as itself
    // and squeeze the layers list in a narrow dock.
    _timelinesPanel->setMinimumWidth(200);

    _splitter->addWidget(_layersPanel);
    _splitter->addWidget(_timelinesPanel);

    _splitter->setCollapsible(0, false);
    _splitter->setCollapsible(1, false);
    _splitter->setStretchFactor(0, 0);
    _splitter->setStretchFactor(1, 1);

    mainLayout->addWidget(_splitter);

    // The layers list keeps the width it was given as the timeline is
    // resized, and from run to run; the frames take the rest.
    _layersWidth = std::max(QSettings().value("timeline/layersWidth", 250).toInt(), 150);
    connect(_splitter, &QSplitter::splitterMoved, this, [this]() {
        _layersWidth = layersWidth();
        if (!_phoneView)
            QSettings().setValue("timeline/layersWidth", _layersWidth);
    });

    connect(_layersPanel, &LayersPanel::layerVisibilityChanged,
            this, &TimelineView::layerVisibilityChanged);
    connect(_layersPanel, &LayersPanel::layerLockChanged,
            this, &TimelineView::layerLockChanged);
    connect(_layersPanel, &LayersPanel::currentLayerChanged,
            this, &TimelineView::currentLayerChanged);
    connect(_layersPanel, &LayersPanel::layersChanged, this, &TimelineView::layersChanged);
    connect(_layersPanel, &LayersPanel::addLayerRequested, this, &TimelineView::addLayerRequested);
    connect(_layersPanel, &LayersPanel::deleteLayerRequested, this, &TimelineView::deleteLayerRequested);
    connect(_layersPanel, &LayersPanel::layerRenamed, this, &TimelineView::layerRenamed);
    connect(_layersPanel, &LayersPanel::layerEditRequested, this, &TimelineView::layerEditRequested);
    connect(_layersPanel, &LayersPanel::parentLayerRequested, this, &TimelineView::parentLayerRequested);
    connect(_layersPanel, &LayersPanel::effectSettingsRequested, this, &TimelineView::effectSettingsRequested);
    connect(_layersPanel, &LayersPanel::compositingRequested, this, &TimelineView::compositingRequested);
    connect(_layersPanel, &LayersPanel::layerClipboardRequested, this, &TimelineView::layerClipboardRequested);
    connect(_timelinesPanel, &TimelinesPanel::keyframeMoveRequested, this, &TimelineView::keyframeMoveRequested);
    connect(_timelinesPanel, &TimelinesPanel::framePicked, this, &TimelineView::framePicked);
    connect(_timelinesPanel, &TimelinesPanel::soundDropped, this, &TimelineView::soundDropped);
    connect(_timelinesPanel, &TimelinesPanel::framesMoveRequested, this, &TimelineView::framesMoveRequested);
    connect(_timelinesPanel, &TimelinesPanel::contextMenuRequested, this, &TimelineView::framesContextMenuRequested);
    connect(_timelinesPanel, &TimelinesPanel::spanResizeRequested, this, &TimelineView::spanResizeRequested);
    connect(_timelinesPanel, &TimelinesPanel::onionRangeChanged, this, &TimelineView::onionRangeChanged);
    connect(_timelinesPanel, &TimelinesPanel::onionMenuRequested, this, &TimelineView::onionMenuRequested);
    connect(_timelinesPanel, &TimelinesPanel::onionSkinToggled, this, &TimelineView::onionSkinToggled);
    // The frame view, from last time; the layers list keeps to its rows.
    {
        QSettings settings;
        TimelineGrid::View view;
        view.frameWidth = settings.value("timeline/frameWidth", 20).toInt();
        view.rowHeight = settings.value("timeline/rowHeight", 22).toInt();
        view.seconds = settings.value("timeline/seconds", false).toBool();
        view.preview = settings.value("timeline/preview", 0).toInt();
        view.spanSelection = settings.value("timeline/spanSelection", false).toBool();
        _timelinesPanel->grid()->setView(view);
        _layersPanel->setRowHeight(_timelinesPanel->grid()->view().rowHeight);
    }
    connect(_timelinesPanel, &TimelinesPanel::frameViewChanged, this, [this]() {
        const TimelineGrid::View& view = _timelinesPanel->grid()->view();
        _layersPanel->setRowHeight(view.rowHeight);
        if (_phoneView)
            return;
        QSettings settings;
        settings.setValue("timeline/frameWidth", view.frameWidth);
        settings.setValue("timeline/rowHeight", view.rowHeight);
        settings.setValue("timeline/seconds", view.seconds);
        settings.setValue("timeline/preview", view.preview);
        settings.setValue("timeline/spanSelection", view.spanSelection);
    });
    // The grid draws the rows the list shows, so folders fold their lanes.
    connect(_layersPanel, &LayersPanel::rowsChanged, this, [this]() {
        _timelinesPanel->setRows(_layersPanel->visibleRows());
    });
    // The names and their lanes scroll as one: the grid's scroll bar is the
    // one shown, and a wheel over the names moves the grid too. Each side
    // gets the space under its last row it needs to scroll as far as the
    // other, which their different heights would otherwise stop.
    QScrollBar* namesBar = _layersPanel->verticalScrollBar();
    QScrollBar* lanesBar = _timelinesPanel->verticalScrollBar();
    const auto follow = [this, namesBar, lanesBar](int value) {
        if (_syncingScroll)
            return;
        _syncingScroll = true;
        const int shared = std::min({value, namesBar->maximum(), lanesBar->maximum()});
        namesBar->setValue(shared);
        lanesBar->setValue(shared);
        _syncingScroll = false;
    };
    connect(namesBar, &QScrollBar::valueChanged, this, follow);
    connect(lanesBar, &QScrollBar::valueChanged, this, follow);
    const auto align = [this, lanesBar, follow]() {
        const int names = _layersPanel->rowsViewportHeight();
        const int lanes = _timelinesPanel->rowsViewportHeight();
        _timelinesPanel->setBottomPadding(std::max(0, lanes - names));
        _layersPanel->setBottomMargin(std::max(0, names - lanes));
        follow(lanesBar->value());
    };
    connect(namesBar, &QScrollBar::rangeChanged, this, align);
    connect(lanesBar, &QScrollBar::rangeChanged, this, align);

    // A frame pressed in the grid makes its layer current.
    connect(_timelinesPanel, &TimelinesPanel::layerPressed, this,
        [this](const fla::Timeline* timeline, int layerIndex) {
            _layersPanel->setCurrentLayer(timeline, layerIndex);
        });
}

void TimelineView::refresh()
{
    _layersPanel->refresh();
    _timelinesPanel->update();
}

void TimelineView::refreshIfStale()
{
    _layersPanel->refreshIfStale();
}

bool TimelineView::currentLayer(fla::Timeline*& timeline, int& layerIndex) const
{
    return _layersPanel->currentLayer(timeline, layerIndex);
}

FrameSelection TimelineView::frameSelection() const
{
    return _timelinesPanel->selection();
}

void TimelineView::selectAllFrames()
{
    _timelinesPanel->selectAll();
}

void TimelineView::setFrameSelection(const FrameSelection& selection)
{
    _timelinesPanel->setSelection(selection);
}

void TimelineView::setCurrentLayer(const fla::Layer* layer)
{
    const auto* timeline = layer ? dynamic_cast<const fla::Timeline*>(layer->parent) : nullptr;
    if (!timeline)
        return;
    const auto found = std::find(timeline->layers.begin(), timeline->layers.end(), layer);
    if (found == timeline->layers.end())
        return;
    fla::Timeline* current = nullptr;
    int index = -1;
    const int wanted = static_cast<int>(found - timeline->layers.begin());
    if (_layersPanel->currentLayer(current, index) && current == timeline && index == wanted)
        return;
    _layersPanel->setCurrentLayer(timeline, wanted);
}

void TimelineView::clearFrameSelection()
{
    _timelinesPanel->clearSelection();
}

void TimelineView::setOnionSkin(bool on, int before, int after)
{
    _timelinesPanel->setOnionSkin(on, before, after);
}

void TimelineView::setSoundProvider(std::function<std::shared_ptr<const PcmBuffer>(const fla::Frame*, size_t& in)> provider)
{
    _timelinesPanel->setSoundProvider(std::move(provider));
}

void TimelineView::refreshFrames()
{
    _timelinesPanel->refreshFrames();
}

void TimelineView::setTimeline(fla::Timeline* timeline)
{
    _layersPanel->setTimeline(timeline);
    _timelinesPanel->setTimeline(timeline);
}

void TimelineView::setDocument(const fla::FLADocument* document)
{
    _flaDocument = document;
    // The grid first: the list's rebuild hands it rows of the new document,
    // which it must size against the new document, not the old.
    _timelinesPanel->setDocument(document);
    _layersPanel->setDocument(document);
}

int TimelineView::layersWidth() const
{
    return _splitter->sizes().value(0);
}

void TimelineView::setLayersWidth(int width)
{
    _layersWidth = std::max(width, 150);
    const int total = _splitter->width();
    if (total > _layersWidth)
        _splitter->setSizes({_layersWidth, total - _layersWidth});
}

void TimelineView::setPhoneView(bool on)
{
    if (on == _phoneView)
        return;
    TimelineGrid* grid = _timelinesPanel->grid();
    if (on)
    {
        _beforePhone = grid->view();
        _layersWidthBeforePhone = _layersWidth;
        _phoneView = true;
        TimelineGrid::View view = _beforePhone;
        view.frameWidth = 26;
        view.rowHeight = 34;
        view.preview = 0;
        grid->setView(view);
        setLayersWidth(150);
    }
    else
    {
        _phoneView = false;
        grid->setView(_beforePhone);
        setLayersWidth(_layersWidthBeforePhone);
    }
    _layersPanel->setRowHeight(grid->view().rowHeight);
    _timelinesPanel->setCompactHeader(on);
}

void TimelineView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    setLayersWidth(_layersWidth);
}

std::vector<int> TimelineView::selectedLayers(fla::Timeline*& timeline) const
{
    return _layersPanel->selectedLayers(timeline);
}

void TimelineView::setThumbnailProvider(TimelineGrid::ThumbnailProvider provider)
{
    _timelinesPanel->grid()->setThumbnailProvider(std::move(provider));
}
