#include "sprite_sheet_dialog.h"
#include "web_files.h"
#include "file_pick.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include "hot_text.h"
#include <QVBoxLayout>

#include <cstring>
#include <algorithm>

namespace {

HotSpinBox* spin(int low, int high, int value, QWidget* parent, const QString& suffix = QString())
{
    HotSpinBox* box = new HotSpinBox(parent);
    box->setRange(low, high);
    box->setValue(value);
    box->setSuffix(suffix);
    return box;
}

/// A checkerboard behind the preview, so clear pixels show as clear.
QPixmap onCheckers(const QImage& image)
{
    QPixmap pixmap(image.size());
    QPainter painter(&pixmap);
    const int cell = 8;
    for (int y = 0; y < image.height(); y += cell)
        for (int x = 0; x < image.width(); x += cell)
            painter.fillRect(x, y, cell, cell, ((x / cell + y / cell) % 2) ? QColor(204, 204, 204) : QColor(255, 255, 255));
    painter.drawImage(0, 0, image);
    return pixmap;
}

} // namespace

SpriteSheetDialog::SpriteSheetDialog(const QString& source, int frameCount, double frameRate, Renderer render,
    const QString& suggestedPath, QWidget* parent)
    : QDialog(parent)
    , _source(source)
    , _frameRate(frameRate)
    , _render(std::move(render))
{
    setWindowTitle(QString("Sprite Sheet: %1").arg(source));
    QSettings settings;

    _path = new QLineEdit(suggestedPath, this);
    QPushButton* browseButton = new QPushButton("Browse...", this);
    connect(browseButton, &QPushButton::clicked, this, &SpriteSheetDialog::browse);
    QHBoxLayout* pathRow = new QHBoxLayout;
    pathRow->addWidget(_path, 1);
    pathRow->addWidget(browseButton);

    const int count = std::max(1, frameCount);
    _first = spin(1, count, 1, this);
    _last = spin(1, count, count, this);
    QHBoxLayout* framesRow = new QHBoxLayout;
    framesRow->addWidget(_first);
    framesRow->addWidget(new QLabel("to", this));
    framesRow->addWidget(_last);
    framesRow->addStretch(1);

    _scale = spin(1, 800, settings.value("spriteSheet/scale", 100).toInt(), this, "%");

    _layout = new QComboBox(this);
    _layout->addItems({"Grid", "One row", "Packed"});
    _layout->setCurrentIndex(std::clamp(settings.value("spriteSheet/layout", 0).toInt(), 0, 2));
    _layout->setToolTip("Grid: cells the size of the largest frame. Packed: frames fitted closely, tallest first");
    _columns = spin(0, 256, settings.value("spriteSheet/columns", 0).toInt(), this);
    _columns->setSpecialValueText("Auto");
    _columns->setToolTip("Columns in the grid; Auto for as square a sheet as fits");
    _maxWidth = spin(64, 16384, settings.value("spriteSheet/maxWidth", 2048).toInt(), this, " px");
    _maxWidth->setToolTip("The widest a packed sheet may be");
    QHBoxLayout* layoutRow = new QHBoxLayout;
    layoutRow->addWidget(_layout, 1);
    layoutRow->addWidget(new QLabel("Columns", this));
    layoutRow->addWidget(_columns);
    layoutRow->addWidget(new QLabel("Max width", this));
    layoutRow->addWidget(_maxWidth);

    _padding = spin(0, 64, settings.value("spriteSheet/padding", 2).toInt(), this, " px");
    _padding->setToolTip("Clear pixels between frames, so filtering never bleeds one into the next");
    _border = spin(0, 64, settings.value("spriteSheet/border", 0).toInt(), this, " px");
    QHBoxLayout* spacingRow = new QHBoxLayout;
    spacingRow->addWidget(new QLabel("Between", this));
    spacingRow->addWidget(_padding);
    spacingRow->addWidget(new QLabel("Around", this));
    spacingRow->addWidget(_border);
    spacingRow->addStretch(1);

    _trim = new QCheckBox("Trim clear edges", this);
    _trim->setToolTip("Keep only each frame's drawn pixels; the description says where they sit in the frame");
    _trim->setChecked(settings.value("spriteSheet/trim", false).toBool());
    _dedupe = new QCheckBox("Share repeated frames", this);
    _dedupe->setToolTip("A frame the same as an earlier one points at its pixels rather than taking room");
    _dedupe->setChecked(settings.value("spriteSheet/dedupe", true).toBool());
    _powerOfTwo = new QCheckBox("Power-of-two size", this);
    _powerOfTwo->setChecked(settings.value("spriteSheet/powerOfTwo", false).toBool());
    _background = new QCheckBox("Stage color behind", this);
    _background->setToolTip("Off, the sheet is clear where nothing is drawn");

    _data = new QComboBox(this);
    _data->addItems({"JSON (hash)", "JSON (array)", "None"});
    _data->setCurrentIndex(std::clamp(settings.value("spriteSheet/data", 0).toInt(), 0, 2));
    _data->setToolTip("A description of where each frame is, written beside the image with the same name");

    QFormLayout* form = new QFormLayout;
    form->addRow("File", pathRow);
    form->addRow("Frames", framesRow);
    form->addRow("Scale", _scale);
    form->addRow("Layout", layoutRow);
    form->addRow("Padding", spacingRow);
    form->addRow("", _trim);
    form->addRow("", _dedupe);
    form->addRow("", _powerOfTwo);
    form->addRow("", _background);
    form->addRow("Data", _data);

    _preview = new QLabel(this);
    _preview->setAlignment(Qt::AlignCenter);
    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidget(_preview);
    scroll->setWidgetResizable(true);
    scroll->setMinimumSize(420, 300);
    _summary = new QLabel(this);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText("Export");
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QHBoxLayout* columns = new QHBoxLayout;
    columns->addLayout(form);
    QVBoxLayout* previewColumn = new QVBoxLayout;
    previewColumn->addWidget(scroll, 1);
    previewColumn->addWidget(_summary);
    columns->addLayout(previewColumn, 1);
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(columns, 1);
    layout->addWidget(buttons);

    // Drawing the frames again only when what is drawn changes.
    for (HotSpinBox* box : {_first, _last, _scale})
        connect(box, &HotSpinBox::valueChanged, this, &SpriteSheetDialog::renderFrames);
    connect(_background, &QCheckBox::toggled, this, &SpriteSheetDialog::renderFrames);
    for (HotSpinBox* box : {_columns, _maxWidth, _padding, _border})
        connect(box, &HotSpinBox::valueChanged, this, &SpriteSheetDialog::relayout);
    for (QCheckBox* box : {_trim, _dedupe, _powerOfTwo})
        connect(box, &QCheckBox::toggled, this, &SpriteSheetDialog::relayout);
    connect(_layout, &QComboBox::currentIndexChanged, this, &SpriteSheetDialog::relayout);
    renderFrames();
}

sprites::Options SpriteSheetDialog::options() const
{
    sprites::Options options;
    options.layout = static_cast<sprites::Layout>(_layout->currentIndex());
    options.columns = _columns->value();
    options.maxWidth = _maxWidth->value();
    options.padding = _padding->value();
    options.border = _border->value();
    options.trim = _trim->isChecked();
    options.dedupe = _dedupe->isChecked();
    options.powerOfTwo = _powerOfTwo->isChecked();
    return options;
}

void SpriteSheetDialog::renderFrames()
{
    if (_last->value() < _first->value())
        _last->setValue(_first->value());
    const int first = _first->value() - 1;
    const int last = _last->value() - 1;
    const double scale = _scale->value() / 100.0;
    _frames.clear();
    QProgressDialog progress("Drawing frames...", "Cancel", 0, last - first + 1, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(500);
    for (int frame = first; frame <= last; ++frame)
    {
        progress.setValue(frame - first);
        if (progress.wasCanceled())
            break;
        const QImage image = _render(frame, scale, _background->isChecked()).convertToFormat(QImage::Format_RGBA8888);
        sprites::Frame pixels;
        pixels.width = image.width();
        pixels.height = image.height();
        pixels.rgba.resize(static_cast<size_t>(image.width()) * image.height() * 4);
        for (int y = 0; y < image.height(); ++y)
            std::memcpy(pixels.rgba.data() + static_cast<size_t>(y) * image.width() * 4, image.constScanLine(y),
                static_cast<size_t>(image.width()) * 4);
        // Kept once, as pixels: the sheet is drawn from these.
        _frames.push_back(std::move(pixels));
    }
    relayout();
}

void SpriteSheetDialog::relayout()
{
    _columns->setEnabled(_layout->currentIndex() == 0);
    _maxWidth->setEnabled(_layout->currentIndex() == 2);
    _sheet = sprites::layout(_frames, options());
    // An image past what Qt can hold (sides over 32767, or 2 GB) cannot be
    // made: said, rather than an empty sheet saved.
    const qint64 bytes = static_cast<qint64>(std::max(1, _sheet.width)) * std::max(1, _sheet.height) * 4;
    if (_sheet.width > 32767 || _sheet.height > 32767 || bytes > (qint64(1) << 31))
        _sheetImage = QImage();
    else
        _sheetImage = QImage(std::max(1, _sheet.width), std::max(1, _sheet.height), QImage::Format_ARGB32);
    if (_sheetImage.isNull())
    {
        _preview->setPixmap(QPixmap());
        _summary->setText(QString("%1 × %2 px is too large for one image: lower the scale, use fewer frames, "
                                  "or a Max width for a packed layout.")
                              .arg(_sheet.width)
                              .arg(_sheet.height));
        return;
    }
    _sheetImage.fill(Qt::transparent);
    QPainter painter(&_sheetImage);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    int unique = 0;
    for (size_t i = 0; i < _sheet.frames.size() && i < _frames.size(); ++i)
    {
        const sprites::Placed& placed = _sheet.frames[i];
        if (placed.sameAs >= 0)
            continue;
        ++unique;
        const sprites::Frame& pixels = _frames[i];
        const QImage image(pixels.rgba.data(), pixels.width, pixels.height, pixels.width * 4,
            QImage::Format_RGBA8888);
        painter.drawImage(QPoint(placed.x, placed.y), image,
            QRect(placed.offsetX, placed.offsetY, placed.width, placed.height));
    }
    painter.end();
    // Shown no larger than fits comfortably.
    QImage shown = _sheetImage;
    if (shown.width() > 1200 || shown.height() > 1200)
        shown = shown.scaled(1200, 1200, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    _preview->setPixmap(onCheckers(shown));
    _summary->setText(QString("%1 × %2 px, %3 frames, %4 drawn")
                          .arg(_sheet.width)
                          .arg(_sheet.height)
                          .arg(_sheet.frames.size())
                          .arg(unique));
}

void SpriteSheetDialog::browse()
{
    file_pick::save(this, "Export Sprite Sheet", _path->text(), "PNG Image (*.png)",
        [this](const QString& chosen) { _path->setText(chosen); });
}

bool SpriteSheetDialog::write(QString& error)
{
    QString path = _path->text();
    if (path.isEmpty())
    {
        error = "Choose where to save the sprite sheet.";
        return false;
    }
    if (QFileInfo(path).suffix().toLower() != "png")
        path += ".png";
    if (_sheetImage.isNull())
    {
        error = "The sprite sheet is too large for one image.";
        return false;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (!_sheetImage.save(path, "PNG"))
    {
        error = QString("Could not write %1.").arg(QDir::toNativeSeparators(path));
        return false;
    }
    const auto format = static_cast<sprites::DataFormat>(_data->currentIndex());
    if (format != sprites::DataFormat::None)
    {
        std::vector<std::string> names;
        for (int frame = _first->value(); frame < _first->value() + static_cast<int>(_sheet.frames.size()); ++frame)
            names.push_back(QString("%1 %2").arg(_source).arg(frame, 4, 10, QChar('0')).toStdString());
        const std::string text = sprites::describe(_sheet, names, QFileInfo(path).fileName().toStdString(), format, _frameRate);
        const QString dataPath = QFileInfo(path).absoluteDir().filePath(QFileInfo(path).completeBaseName() + ".json");
        QFile file(dataPath);
        if (!file.open(QIODevice::WriteOnly) || file.write(text.data(), static_cast<qint64>(text.size())) != static_cast<qint64>(text.size()))
        {
            error = QString("Could not write %1.").arg(QDir::toNativeSeparators(dataPath));
            return false;
        }
#ifdef PHOENIX_WEB
        web_files::download(dataPath);
#endif
    }
#ifdef PHOENIX_WEB
    web_files::download(path);
#endif
    QSettings settings;
    settings.setValue("spriteSheet/scale", _scale->value());
    settings.setValue("spriteSheet/layout", _layout->currentIndex());
    settings.setValue("spriteSheet/columns", _columns->value());
    settings.setValue("spriteSheet/maxWidth", _maxWidth->value());
    settings.setValue("spriteSheet/padding", _padding->value());
    settings.setValue("spriteSheet/border", _border->value());
    settings.setValue("spriteSheet/trim", _trim->isChecked());
    settings.setValue("spriteSheet/dedupe", _dedupe->isChecked());
    settings.setValue("spriteSheet/powerOfTwo", _powerOfTwo->isChecked());
    settings.setValue("spriteSheet/data", _data->currentIndex());
    return true;
}
