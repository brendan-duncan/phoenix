#include "timeline_view.h"
#include "layers_panel.h"
#include "timelines_panel.h"
#include "player.h"

#include <QScrollBar>
#include <QVBoxLayout>

#include <algorithm>

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

    _splitter->addWidget(_layersPanel);
    _splitter->addWidget(_timelinesPanel);

    _splitter->setCollapsible(0, false);
    _splitter->setCollapsible(1, false);
    _splitter->setStretchFactor(0, 0);
    _splitter->setStretchFactor(1, 1);

    mainLayout->addWidget(_splitter);

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
    connect(_timelinesPanel, &TimelinesPanel::keyframeMoveRequested, this, &TimelineView::keyframeMoveRequested);
    connect(_timelinesPanel, &TimelinesPanel::framesMoveRequested, this, &TimelineView::framesMoveRequested);
    connect(_timelinesPanel, &TimelinesPanel::contextMenuRequested, this, &TimelineView::framesContextMenuRequested);
    connect(_timelinesPanel, &TimelinesPanel::spanResizeRequested, this, &TimelineView::spanResizeRequested);
    connect(_timelinesPanel, &TimelinesPanel::onionSkinToggled, this, &TimelineView::onionSkinToggled);
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
