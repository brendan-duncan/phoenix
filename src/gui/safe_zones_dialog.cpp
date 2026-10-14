#include "safe_zones_dialog.h"

#include "hot_text.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <cstdint>

namespace sz = fla::safezones;

namespace {

const char* const kPresetsKey = "safeZones/presets";
const char* const kDefaultKey = "safeZones/default";

QIcon swatch(const uint8_t color[4])
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setPen(QColor(0, 0, 0, 120));
    painter.setBrush(QColor(color[0], color[1], color[2], color[3]));
    painter.drawRect(0, 0, 15, 15);
    return QIcon(pixmap);
}

QVariantMap userPresets()
{
    return QSettings().value(kPresetsKey).toMap();
}

enum Column
{
    Name,
    Color,
    Anchor,
    Shade,
    ColumnCount
};

} // namespace

sz::Settings SafeZonesDialog::newDocumentDefault()
{
    const QString text = QSettings().value(kDefaultKey).toString();
    return text.isEmpty() ? sz::defaults() : sz::fromText(text.toStdString());
}

SafeZonesDialog::SafeZonesDialog(const sz::Settings& settings, std::function<void(const sz::Settings&)> preview,
    QWidget* parent)
    : QDialog(parent)
    , _settings(settings)
    , _preview(std::move(preview))
{
    setWindowTitle("Safe Zone Settings");
    QVBoxLayout* layout = new QVBoxLayout(this);

    // Presets.
    QHBoxLayout* presetRow = new QHBoxLayout;
    presetRow->addWidget(new QLabel("Preset:", this));
    _presets = new QComboBox(this);
    _presets->setMinimumContentsLength(16);
    presetRow->addWidget(_presets, 1);
    QPushButton* apply = new QPushButton("Apply", this);
    apply->setToolTip("Set everything below from the chosen preset");
    QPushButton* save = new QPushButton("Save As...", this);
    save->setToolTip("Keep these settings as a preset of your own");
    QPushButton* remove = new QPushButton("Delete", this);
    remove->setToolTip("Delete the chosen preset of your own");
    presetRow->addWidget(apply);
    presetRow->addWidget(save);
    presetRow->addWidget(remove);
    layout->addLayout(presetRow);

    // Safe zones.
    QGroupBox* zones = new QGroupBox("Safe zones", this);
    QFormLayout* zoneForm = new QFormLayout(zones);
    const auto percentRow = [&](QCheckBox*& check, HotDoubleSpinBox*& percent, const QString& name) {
        check = new QCheckBox(name, zones);
        percent = new HotDoubleSpinBox(zones);
        percent->setRange(1.0, 100.0);
        percent->setDecimals(1);
        percent->setSingleStep(0.5);
        percent->setSuffix("%");
        percent->setToolTip("How much of the frame it covers, about the center");
        QHBoxLayout* row = new QHBoxLayout;
        row->addWidget(check);
        row->addWidget(percent);
        row->addStretch(1);
        zoneForm->addRow(row);
    };
    percentRow(_title, _titlePercent, "Title safe");
    percentRow(_action, _actionPercent, "Action safe");
    _cross = new QCheckBox("Center cross", zones);
    _thirds = new QCheckBox("Thirds", zones);
    QHBoxLayout* marks = new QHBoxLayout;
    marks->addWidget(_cross);
    marks->addWidget(_thirds);
    marks->addStretch(1);
    zoneForm->addRow(marks);
    _follow = new QComboBox(zones);
    _follow->setToolTip("Measure the safe zones, cross and thirds in the stage, or in one of the frames below");
    zoneForm->addRow("Measured in:", _follow);
    layout->addWidget(zones);

    // Aspect-ratio frames.
    QGroupBox* framesBox = new QGroupBox("Aspect-ratio frames", this);
    QVBoxLayout* framesLayout = new QVBoxLayout(framesBox);
    _frames = new QTreeWidget(framesBox);
    _frames->setColumnCount(ColumnCount);
    _frames->setHeaderLabels({"Frame", "Color", "Placed", "Shade outside"});
    _frames->setRootIsDecorated(false);
    _frames->header()->setSectionResizeMode(Name, QHeaderView::Stretch);
    _frames->header()->setSectionResizeMode(Color, QHeaderView::ResizeToContents);
    _frames->header()->setSectionResizeMode(Anchor, QHeaderView::ResizeToContents);
    _frames->header()->setSectionResizeMode(Shade, QHeaderView::ResizeToContents);
    _frames->setMinimumHeight(200);
    framesLayout->addWidget(_frames);
    QHBoxLayout* frameButtons = new QHBoxLayout;
    QPushButton* add = new QPushButton("Add...", framesBox);
    add->setToolTip("A frame of another shape: 3:2, 1.66:1, 21:9...");
    QPushButton* removeFrame = new QPushButton("Remove", framesBox);
    frameButtons->addWidget(add);
    frameButtons->addWidget(removeFrame);
    frameButtons->addStretch(1);
    framesLayout->addLayout(frameButtons);
    layout->addWidget(framesBox, 1);

    // New documents, and the dialog's own buttons.
    QHBoxLayout* bottom = new QHBoxLayout;
    QPushButton* makeDefault = new QPushButton("Use for New Documents", this);
    makeDefault->setToolTip("New documents start with these settings");
    bottom->addWidget(makeDefault);
    bottom->addStretch(1);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    bottom->addWidget(buttons);
    layout->addLayout(bottom);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    for (QCheckBox* check : {_title, _action, _cross, _thirds})
        connect(check, &QCheckBox::toggled, this, [this]() { changed(); });
    for (HotDoubleSpinBox* percent : {_titlePercent, _actionPercent})
        connect(percent, &HotText::valueChanged, this, [this]() { changed(); });
    connect(_follow, &QComboBox::currentIndexChanged, this, [this]() { changed(); });

    connect(_frames, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (_loading || column != Name)
            return;
        const int row = _frames->indexOfTopLevelItem(item);
        if (row < 0 || row >= static_cast<int>(_settings.frames.size()))
            return;
        _settings.frames[static_cast<size_t>(row)].on = item->checkState(Name) == Qt::Checked;
        fillFollow();
        changed();
    });

    connect(add, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const QString text = QInputDialog::getText(this, "Add Frame", "Aspect ratio (width:height):",
            QLineEdit::Normal, "3:2", &ok);
        if (!ok)
            return;
        sz::AspectFrame frame;
        if (!sz::parseRatio(text.toStdString(), frame.width, frame.height))
        {
            QMessageBox::warning(this, "Add Frame", QString("\"%1\" is not an aspect ratio, such as 3:2 or 1.66:1.").arg(text));
            return;
        }
        frame.on = true;
        _settings.frames.push_back(frame);
        fillFrames();
        fillFollow();
        changed();
    });
    connect(removeFrame, &QPushButton::clicked, this, [this]() {
        const int row = _frames->indexOfTopLevelItem(_frames->currentItem());
        if (row < 0 || row >= static_cast<int>(_settings.frames.size()))
            return;
        _settings.frames.erase(_settings.frames.begin() + row);
        if (_settings.follow == row)
            _settings.follow = -1;
        else if (_settings.follow > row)
            --_settings.follow;
        fillFrames();
        fillFollow();
        changed();
    });

    connect(apply, &QPushButton::clicked, this, [this]() {
        const QString name = _presets->currentData().toString();
        for (const auto& [builtIn, settings] : sz::presets())
        {
            if (name == QString::fromStdString(builtIn))
            {
                _settings = settings;
                load();
                changed();
                return;
            }
        }
        const QVariantMap mine = userPresets();
        if (mine.contains(name))
        {
            _settings = sz::fromText(mine.value(name).toString().toStdString());
            load();
            changed();
        }
    });
    connect(save, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const QString name = QInputDialog::getText(this, "Save Preset", "Name:", QLineEdit::Normal, QString(), &ok).trimmed();
        if (!ok || name.isEmpty())
            return;
        for (const auto& [builtIn, settings] : sz::presets())
        {
            if (name.compare(QString::fromStdString(builtIn), Qt::CaseInsensitive) == 0)
            {
                QMessageBox::warning(this, "Save Preset", QString("\"%1\" is a built-in preset; choose another name.").arg(name));
                return;
            }
        }
        QVariantMap mine = userPresets();
        mine.insert(name, QString::fromStdString(sz::toText(_settings)));
        QSettings().setValue(kPresetsKey, mine);
        fillPresets();
        _presets->setCurrentIndex(_presets->findData(name));
    });
    connect(remove, &QPushButton::clicked, this, [this]() {
        const QString name = _presets->currentData().toString();
        QVariantMap mine = userPresets();
        if (!mine.contains(name))
        {
            QMessageBox::information(this, "Delete Preset", "Only presets of your own can be deleted.");
            return;
        }
        mine.remove(name);
        QSettings().setValue(kPresetsKey, mine);
        fillPresets();
    });
    connect(makeDefault, &QPushButton::clicked, this, [this]() {
        QSettings().setValue(kDefaultKey, QString::fromStdString(sz::toText(_settings)));
        QMessageBox::information(this, "Safe Zone Settings", "New documents will start with these settings.");
    });

    fillPresets();
    load();
    resize(560, 620);
}

void SafeZonesDialog::fillPresets()
{
    _presets->clear();
    for (const auto& [name, settings] : sz::presets())
        _presets->addItem(QString::fromStdString(name), QString::fromStdString(name));
    const QVariantMap mine = userPresets();
    if (!mine.isEmpty())
        _presets->insertSeparator(_presets->count());
    for (auto it = mine.constBegin(); it != mine.constEnd(); ++it)
        _presets->addItem(it.key(), it.key());
}

void SafeZonesDialog::load()
{
    _loading = true;
    _title->setChecked(_settings.titleSafe);
    _titlePercent->setValue(_settings.titlePercent);
    _action->setChecked(_settings.actionSafe);
    _actionPercent->setValue(_settings.actionPercent);
    _cross->setChecked(_settings.centerCross);
    _thirds->setChecked(_settings.thirds);
    _loading = false;
    fillFrames();
    fillFollow();
}

void SafeZonesDialog::fillFollow()
{
    const bool was = _loading;
    _loading = true;
    _follow->clear();
    _follow->addItem("The stage", -1);
    for (size_t i = 0; i < _settings.frames.size(); ++i)
    {
        const sz::AspectFrame& frame = _settings.frames[i];
        _follow->addItem(QString("The %1 frame%2").arg(QString::fromStdString(frame.name())).arg(frame.on ? "" : " (hidden)"),
            static_cast<int>(i));
    }
    const int index = _follow->findData(_settings.follow);
    _follow->setCurrentIndex(index >= 0 ? index : 0);
    _loading = was;
}

void SafeZonesDialog::fillFrames()
{
    _loading = true;
    _frames->clear();
    for (size_t i = 0; i < _settings.frames.size(); ++i)
    {
        sz::AspectFrame& frame = _settings.frames[i];
        QTreeWidgetItem* item = new QTreeWidgetItem(_frames);
        item->setText(Name, QString::fromStdString(frame.name()));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Name, frame.on ? Qt::Checked : Qt::Unchecked);

        QToolButton* color = new QToolButton(_frames);
        color->setAutoRaise(true);
        color->setIcon(swatch(frame.color));
        color->setToolTip("The frame's color");
        connect(color, &QToolButton::clicked, this, [this, i, color]() {
            if (i >= _settings.frames.size())
                return;
            uint8_t* c = _settings.frames[i].color;
            const QColor chosen = QColorDialog::getColor(QColor(c[0], c[1], c[2], c[3]), this, "Frame Color",
                QColorDialog::ShowAlphaChannel);
            if (!chosen.isValid())
                return;
            c[0] = static_cast<uint8_t>(chosen.red());
            c[1] = static_cast<uint8_t>(chosen.green());
            c[2] = static_cast<uint8_t>(chosen.blue());
            c[3] = static_cast<uint8_t>(chosen.alpha());
            color->setIcon(swatch(c));
            changed();
        });
        _frames->setItemWidget(item, Color, color);

        QComboBox* anchor = new QComboBox(_frames);
        anchor->addItems({"Centered", "Left", "Right", "Top", "Bottom"});
        anchor->setCurrentIndex(static_cast<int>(frame.anchor));
        anchor->setToolTip("Where the frame sits when it is narrower or shorter than the stage");
        connect(anchor, &QComboBox::currentIndexChanged, this, [this, i](int index) {
            if (_loading || i >= _settings.frames.size())
                return;
            _settings.frames[i].anchor = static_cast<sz::Anchor>(index);
            changed();
        });
        _frames->setItemWidget(item, Anchor, anchor);

        QCheckBox* shade = new QCheckBox(_frames);
        shade->setChecked(frame.shade);
        shade->setToolTip("Darken the stage outside the frame, to preview the crop");
        connect(shade, &QCheckBox::toggled, this, [this, i](bool on) {
            if (_loading || i >= _settings.frames.size())
                return;
            _settings.frames[i].shade = on;
            changed();
        });
        _frames->setItemWidget(item, Shade, shade);
    }
    _loading = false;
}

void SafeZonesDialog::changed()
{
    if (_loading)
        return;
    _settings.titleSafe = _title->isChecked();
    _settings.titlePercent = _titlePercent->value();
    _settings.actionSafe = _action->isChecked();
    _settings.actionPercent = _actionPercent->value();
    _settings.centerCross = _cross->isChecked();
    _settings.thirds = _thirds->isChecked();
    _settings.follow = _follow->currentData().isValid() ? _follow->currentData().toInt() : -1;
    if (_preview)
        _preview(_settings);
}
