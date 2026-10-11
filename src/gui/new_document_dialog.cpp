#include "new_document_dialog.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

constexpr int PresetRole = Qt::UserRole;

/// A tile's picture: the stage's shape, landscape, square or portrait, as
/// Animate draws its preset thumbnails.
QIcon aspectIcon(int width, int height)
{
    QPixmap pixmap(64, 48);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const double scale = std::min(56.0 / std::max(width, 1), 40.0 / std::max(height, 1));
    const QSizeF size(width * scale, height * scale);
    const QRectF frame(QPointF((64 - size.width()) / 2.0, (48 - size.height()) / 2.0), size);
    painter.setPen(QPen(QColor(200, 200, 200), 1.2));
    painter.setBrush(QColor(255, 255, 255, 30));
    painter.drawRoundedRect(frame.adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);
    return QIcon(pixmap);
}

QString rateText(double rate)
{
    // 24, 29.97, 23.976: as many decimals as the rate needs.
    QString text = QString::number(rate, 'f', 3);
    while (text.contains('.') && (text.endsWith('0') || text.endsWith('.')))
        text.chop(1);
    return text;
}

} // namespace

const std::vector<NewDocumentDialog::Preset>& NewDocumentDialog::presets()
{
    // Animate's presets by category, with the sizes and rates it uses.
    static const std::vector<Preset> all = {
        {"Character Animation", "Full HD", 1920, 1080, 24},
        {"Character Animation", "HD", 1280, 720, 24},
        {"Character Animation", "4K", 3840, 2160, 24},
        {"Character Animation", "Standard", 640, 480, 24},
        {"Character Animation", "Widescreen", 854, 480, 24},

        {"Social", "Instagram", 1080, 1080, 30},
        {"Social", "Instagram Story", 1080, 1920, 30},
        {"Social", "Twitter", 1280, 720, 30},
        {"Social", "Facebook", 1280, 720, 30},
        {"Social", "YouTube", 1920, 1080, 30},
        {"Social", "TikTok", 1080, 1920, 30},

        {"Game", "Full HD", 1920, 1080, 30},
        {"Game", "HD", 1280, 720, 30},
        {"Game", "Mobile Landscape", 1334, 750, 60},
        {"Game", "Mobile Portrait", 750, 1334, 60},
        {"Game", "Tablet", 2048, 1536, 60},

        {"Education", "Full HD", 1920, 1080, 30},
        {"Education", "HD", 1280, 720, 30},
        {"Education", "Standard", 1024, 768, 30},
        {"Education", "Tablet", 2048, 1536, 30},

        {"Ads", "Medium Rectangle", 300, 250, 24},
        {"Ads", "Leaderboard", 728, 90, 24},
        {"Ads", "Wide Skyscraper", 160, 600, 24},
        {"Ads", "Half Page", 300, 600, 24},
        {"Ads", "Mobile Banner", 320, 50, 24},
        {"Ads", "Large Rectangle", 336, 280, 24},
        {"Ads", "Billboard", 970, 250, 24},

        {"Web", "Full Screen", 1920, 1080, 30},
        {"Web", "Desktop", 1366, 768, 30},
        {"Web", "Laptop", 1280, 800, 30},
        {"Web", "Tablet", 768, 1024, 30},
        {"Web", "Mobile", 375, 667, 30},

        {"Advanced", "HD 720p", 1280, 720, 30},
        {"Advanced", "HD 1080p", 1920, 1080, 30},
        {"Advanced", "4K UHD", 3840, 2160, 30},
        {"Advanced", "DCI 4K", 4096, 2160, 24},
        {"Advanced", "PAL", 720, 576, 25},
        {"Advanced", "NTSC", 720, 480, 29.97},
        {"Advanced", "Film 24p", 1920, 1080, 23.976},
        {"Advanced", "Animate default", 550, 400, 24},
    };
    return all;
}

QStringList NewDocumentDialog::categories()
{
    QStringList names;
    for (const Preset& preset : presets())
    {
        const QString name = preset.category;
        if (!names.contains(name))
            names << name;
    }
    return names;
}

NewDocumentDialog::NewDocumentDialog(QWidget* parent)
    : QDialog(parent)
    , _categories(new QListWidget(this))
    , _tiles(new QListWidget(this))
    , _width(new QSpinBox(this))
    , _height(new QSpinBox(this))
    , _frameRate(new QComboBox(this))
    , _swatch(new QToolButton(this))
    , _summary(new QLabel(this))
{
    setWindowTitle("New Document");
    resize(860, 480);

    QVBoxLayout* outer = new QVBoxLayout(this);
    QHBoxLayout* columns = new QHBoxLayout();
    outer->addLayout(columns, 1);

    // Categories down the left, as Animate's dialog has them along its top.
    _categories->addItems(categories());
    _categories->setFixedWidth(170);
    _categories->setStyleSheet("QListWidget { border: none; } QListWidget::item { padding: 6px 8px; }");
    columns->addWidget(_categories);

    // The presets as tiles.
    QVBoxLayout* middle = new QVBoxLayout();
    QLabel* presetsLabel = new QLabel("Presets", this);
    QFont bold = presetsLabel->font();
    bold.setBold(true);
    presetsLabel->setFont(bold);
    middle->addWidget(presetsLabel);
    _tiles->setViewMode(QListView::IconMode);
    _tiles->setIconSize(QSize(64, 48));
    _tiles->setGridSize(QSize(132, 104));
    _tiles->setResizeMode(QListView::Adjust);
    _tiles->setMovement(QListView::Static);
    _tiles->setWordWrap(true);
    _tiles->setSpacing(4);
    middle->addWidget(_tiles, 1);
    columns->addLayout(middle, 1);

    // The details of the document to make.
    QWidget* details = new QWidget(this);
    details->setFixedWidth(230);
    QVBoxLayout* detailsLayout = new QVBoxLayout(details);
    detailsLayout->setContentsMargins(12, 0, 0, 0);
    QLabel* detailsLabel = new QLabel("Details", this);
    detailsLabel->setFont(bold);
    detailsLayout->addWidget(detailsLabel);
    QFormLayout* form = new QFormLayout();
    _width->setRange(1, 8192);
    _width->setSuffix(" px");
    _height->setRange(1, 8192);
    _height->setSuffix(" px");
    form->addRow("Width", _width);
    form->addRow("Height", _height);
    _frameRate->setEditable(true);
    for (double rate : {8.0, 10.0, 12.0, 15.0, 23.976, 24.0, 25.0, 29.97, 30.0, 48.0, 50.0, 59.94, 60.0, 120.0})
        _frameRate->addItem(rateText(rate));
    _frameRate->setValidator(new QDoubleValidator(0.01, 120.0, 3, _frameRate));
    form->addRow("Frame rate", _frameRate);
    _swatch->setFixedSize(36, 22);
    _swatch->setToolTip("Stage color");
    form->addRow("Stage color", _swatch);
    detailsLayout->addLayout(form);
    _summary->setWordWrap(true);
    _summary->setEnabled(false);
    detailsLayout->addWidget(_summary);
    detailsLayout->addStretch();
    columns->addWidget(details);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText("Create");
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);

    connect(_categories, &QListWidget::currentTextChanged, this, &NewDocumentDialog::showCategory);
    connect(_tiles, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (!item)
            return;
        const int index = item->data(PresetRole).toInt();
        if (index >= 0 && index < static_cast<int>(presets().size()))
            applyPreset(presets()[index]);
    });
    // A double-click on a tile creates the document straight away.
    connect(_tiles, &QListWidget::itemDoubleClicked, this, [this]() { accept(); });
    connect(_swatch, &QToolButton::clicked, this, [this]() {
        const QColor chosen = QColorDialog::getColor(_stageColor, this, "Stage Color");
        if (chosen.isValid())
        {
            _stageColor = chosen;
            updateSwatch();
        }
    });
    const auto edited = [this]() {
        if (!_applying)
            markMatchingPreset();
    };
    connect(_width, &QSpinBox::valueChanged, this, edited);
    connect(_height, &QSpinBox::valueChanged, this, edited);
    connect(_frameRate, &QComboBox::currentTextChanged, this, edited);

    // Start where the last document was made; the first time, at HD 1080p.
    QSettings settings;
    const QString category = settings.value("newDocument/category", "Advanced").toString();
    const int width = settings.value("newDocument/width", 1920).toInt();
    const int height = settings.value("newDocument/height", 1080).toInt();
    const double rate = settings.value("newDocument/frameRate", 30.0).toDouble();
    _stageColor = QColor(settings.value("newDocument/stageColor", "#FFFFFF").toString());
    if (!_stageColor.isValid())
        _stageColor = Qt::white;
    const QList<QListWidgetItem*> found = _categories->findItems(category, Qt::MatchExactly);
    _categories->setCurrentItem(found.isEmpty() ? _categories->item(0) : found.front());
    _applying = true;
    _width->setValue(width);
    _height->setValue(height);
    _frameRate->setCurrentText(rateText(rate));
    _applying = false;
    markMatchingPreset();
    updateSwatch();
}

void NewDocumentDialog::showCategory(const QString& category)
{
    _tiles->clear();
    const std::vector<Preset>& all = presets();
    for (size_t i = 0; i < all.size(); ++i)
    {
        const Preset& preset = all[i];
        if (category != preset.category)
            continue;
        QListWidgetItem* item = new QListWidgetItem(aspectIcon(preset.width, preset.height),
            QString("%1\n%2 x %3").arg(preset.name).arg(preset.width).arg(preset.height), _tiles);
        item->setData(PresetRole, static_cast<int>(i));
        item->setToolTip(QString("%1: %2 x %3 at %4 fps").arg(preset.name).arg(preset.width)
            .arg(preset.height).arg(rateText(preset.frameRate)));
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
    }
    markMatchingPreset();
}

void NewDocumentDialog::applyPreset(const Preset& preset)
{
    _applying = true;
    _width->setValue(preset.width);
    _height->setValue(preset.height);
    _frameRate->setCurrentText(rateText(preset.frameRate));
    _applying = false;
    _summary->setText(QString("%1, %2 x %3 pixels at %4 frames per second.")
        .arg(preset.name).arg(preset.width).arg(preset.height).arg(rateText(preset.frameRate)));
}

void NewDocumentDialog::markMatchingPreset()
{
    // The tile matching the details stays picked; edit them and none is.
    const QSignalBlocker blocker(_tiles);
    _tiles->setCurrentItem(nullptr);
    _tiles->clearSelection();
    for (int i = 0; i < _tiles->count(); ++i)
    {
        QListWidgetItem* item = _tiles->item(i);
        const Preset& preset = presets()[item->data(PresetRole).toInt()];
        if (preset.width == documentWidth() && preset.height == documentHeight() &&
            std::fabs(preset.frameRate - frameRate()) < 1.0e-3)
        {
            _tiles->setCurrentItem(item);
            _summary->setText(QString("%1, %2 x %3 pixels at %4 frames per second.")
                .arg(preset.name).arg(preset.width).arg(preset.height).arg(rateText(preset.frameRate)));
            return;
        }
    }
    _summary->setText(QString("Custom, %1 x %2 pixels at %3 frames per second.")
        .arg(documentWidth()).arg(documentHeight()).arg(rateText(frameRate())));
}

void NewDocumentDialog::updateSwatch()
{
    QPixmap pixmap(28, 14);
    pixmap.fill(_stageColor);
    QPainter painter(&pixmap);
    painter.setPen(QColor(30, 30, 30));
    painter.drawRect(0, 0, 27, 13);
    _swatch->setIcon(QIcon(pixmap));
    _swatch->setIconSize(pixmap.size());
}

int NewDocumentDialog::documentWidth() const
{
    return _width->value();
}

int NewDocumentDialog::documentHeight() const
{
    return _height->value();
}

double NewDocumentDialog::frameRate() const
{
    bool ok = false;
    const double rate = _frameRate->currentText().toDouble(&ok);
    return ok && rate > 0.0 ? rate : 24.0;
}

void NewDocumentDialog::accept()
{
    QSettings settings;
    if (_categories->currentItem())
        settings.setValue("newDocument/category", _categories->currentItem()->text());
    settings.setValue("newDocument/width", documentWidth());
    settings.setValue("newDocument/height", documentHeight());
    settings.setValue("newDocument/frameRate", frameRate());
    settings.setValue("newDocument/stageColor", _stageColor.name());
    QDialog::accept();
}
