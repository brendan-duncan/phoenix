#include "frame_picker_panel.h"

#include "../data/symbol_instance.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QTimer>
#include <QVBoxLayout>

namespace {

const QSize kThumbnail(72, 72);

/// The looping choices, as the Properties panel lists them.
const fla::LoopType kLoops[4] = {fla::LoopType::Loop, fla::LoopType::PlayOnce, fla::LoopType::SingleFrame,
    fla::LoopType::PingPong};

} // namespace

FramePickerPanel::FramePickerPanel(QWidget* parent)
    : QWidget(parent)
{
    _title = new QLabel(this);
    _title->setContentsMargins(4, 4, 4, 0);

    _loop = new QComboBox(this);
    _loop->addItems({"Loop", "Play Once", "Single Frame", "Ping Pong"});
    _loop->setToolTip("How the instance plays from the frame picked");
    connect(_loop, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!_updating && index >= 0 && index < 4)
            emit loopChosen(static_cast<int>(kLoops[index]));
    });

    _createKeyframe = new QCheckBox("Create Keyframe", this);
    _createKeyframe->setToolTip("Set the frame picked on a new keyframe at the playhead");

    _frames = new QListWidget(this);
    _frames->setViewMode(QListView::IconMode);
    _frames->setIconSize(kThumbnail);
    _frames->setGridSize(kThumbnail + QSize(16, 24));
    _frames->setResizeMode(QListView::Adjust);
    _frames->setMovement(QListView::Static);
    _frames->setUniformItemSizes(true);
    connect(_frames, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (item)
            emit frameChosen(_frames->row(item), _createKeyframe->isChecked());
    });

    // Pictures are drawn a few at a time, so a long symbol does not hold up
    // the window.
    _renderTimer = new QTimer(this);
    _renderTimer->setInterval(0);
    connect(_renderTimer, &QTimer::timeout, this, &FramePickerPanel::renderSome);

    QHBoxLayout* options = new QHBoxLayout();
    options->addWidget(_loop);
    options->addWidget(_createKeyframe);
    options->addStretch();

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(_title);
    layout->addLayout(options);
    layout->addWidget(_frames, 1);

    setInstance(nullptr, 0);
}

void FramePickerPanel::setInstance(const fla::SymbolInstance* instance, int frameCount)
{
    const bool graphic = instance && instance->symbol && instance->symbolType == fla::SymbolType::Graphic;
    const fla::SymbolInstance* shown = graphic ? instance : nullptr;
    if (shown == _instance && (!shown || _frames->count() == frameCount))
    {
        refresh();
        return;
    }
    _instance = shown;
    _renderTimer->stop();
    _frames->clear();
    _loop->setEnabled(shown != nullptr);
    _createKeyframe->setEnabled(shown != nullptr);
    if (!shown)
    {
        _title->setText("Select an instance of a graphic symbol to pick its frames.");
        return;
    }
    _title->setText(QString::fromStdString(instance->libraryItemName));
    QPixmap blank(kThumbnail);
    blank.fill(Qt::transparent);
    for (int i = 0; i < frameCount; ++i)
        _frames->addItem(new QListWidgetItem(QIcon(blank), QString::number(i + 1)));
    _nextToRender = 0;
    _renderTimer->start();
    refresh();
}

void FramePickerPanel::refresh()
{
    if (!_instance)
        return;
    _updating = true;
    for (int i = 0; i < 4; ++i)
    {
        if (kLoops[i] == _instance->loopType)
            _loop->setCurrentIndex(i);
    }
    if (_instance->firstFrame >= 0 && _instance->firstFrame < _frames->count())
        _frames->setCurrentRow(_instance->firstFrame);
    _updating = false;
}

void FramePickerPanel::renderSome()
{
    if (!_instance || !_preview || _nextToRender >= _frames->count())
    {
        _renderTimer->stop();
        return;
    }
    const QString name = QString::fromStdString(_instance->libraryItemName);
    for (int n = 0; n < 4 && _nextToRender < _frames->count(); ++n, ++_nextToRender)
    {
        const QImage picture = _preview(name, _nextToRender, kThumbnail);
        if (!picture.isNull())
            _frames->item(_nextToRender)->setIcon(QIcon(QPixmap::fromImage(picture)));
    }
}
