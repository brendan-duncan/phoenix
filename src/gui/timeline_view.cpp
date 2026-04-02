#include "timeline_view.h"
#include "layers_panel.h"
#include "timelines_panel.h"
#include "player.h"

#include <QVBoxLayout>

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
}

void TimelineView::setDocument(const fla::FLADocument* document)
{
    _flaDocument = document;
    _layersPanel->setDocument(document);
    _timelinesPanel->setDocument(document);
}
