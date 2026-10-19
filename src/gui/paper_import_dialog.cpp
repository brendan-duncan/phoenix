#include "paper_import_dialog.h"

#include "hot_text.h"
#include "media_import.h"
#include "phoenix_view.h"
#include "player.h"
#include "timeline_view.h"

#include "../data/document.h"
#include "../data/fla_document.h"
#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/shape.h"
#include "../data/timeline.h"
#include "../edit/command_stack.h"
#include "../edit/element_commands.h"
#include "../edit/layer_commands.h"
#include "../edit/selection.h"
#include "../edit/timeline_edit.h"
#include "../edit/trace_bitmap.h"
#include "swatch_popup.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include <algorithm>
#include <cstring>
#include <memory>

namespace {

/// The longest side drawings are cleaned up and traced at.
constexpr int kWorkSize = 1600;

/// The preview's size on screen.
constexpr int kPreviewWidth = 420;
constexpr int kPreviewHeight = 315;

/// Straight RGBA bytes, row by row, of an RGBA8888 image.
std::vector<uint8_t> bytesOf(const QImage& image)
{
    std::vector<uint8_t> pixels(static_cast<size_t>(image.width()) * image.height() * 4);
    for (int y = 0; y < image.height(); ++y)
        std::memcpy(pixels.data() + static_cast<size_t>(y) * image.width() * 4, image.constScanLine(y),
            static_cast<size_t>(image.width()) * 4);
    return pixels;
}

QIcon swatch(const QColor& color)
{
    QPixmap pixmap(28, 14);
    pixmap.fill(color);
    QPainter painter(&pixmap);
    painter.setPen(QColor(128, 128, 128));
    painter.drawRect(pixmap.rect().adjusted(0, 0, -1, -1));
    return QIcon(pixmap);
}

} // namespace

QImage PaperImportDialog::workImage(const QImage& image)
{
    QImage work = image.convertToFormat(QImage::Format_RGBA8888);
    if (std::max(work.width(), work.height()) > kWorkSize)
        work = work.scaled(kWorkSize, kWorkSize, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                   .convertToFormat(QImage::Format_RGBA8888);
    return work;
}

PaperImportDialog::PaperImportDialog(int currentFrame, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Import Paper Drawings");
    QSettings settings;
    QHBoxLayout* columns = new QHBoxLayout(this);
    QVBoxLayout* steps = new QVBoxLayout;
    columns->addLayout(steps);

    // 1. Which drawings.
    QGroupBox* pick = new QGroupBox("1. Your drawings", this);
    QFormLayout* pickForm = new QFormLayout(pick);
    _path = new QLineEdit(this);
    _path->setPlaceholderText("A scan or photo of a drawing");
    _path->setToolTip("One picture, or the first of a numbered sequence (walk_001.png, walk_002.png...)");
    pickForm->addRow("Drawing", _path);
    QPushButton* browse = new QPushButton("Browse...", this);
    connect(browse, &QPushButton::clicked, this, &PaperImportDialog::browse);
    pickForm->addRow(QString(), browse);
    _sequence = new QCheckBox("Bring in the whole numbered sequence", this);
    _sequence->setChecked(true);
    _sequence->setToolTip("Every picture numbered like this one, each on a keyframe of its own");
    pickForm->addRow(QString(), _sequence);
    _found = new QLabel(this);
    _found->setWordWrap(true);
    _found->setForegroundRole(QPalette::PlaceholderText);
    pickForm->addRow(QString(), _found);
    steps->addWidget(pick);

    // 2. Cleanup.
    QGroupBox* clean = new QGroupBox("2. Clean up", this);
    QFormLayout* cleanForm = new QFormLayout(clean);
    _even = new QCheckBox("Even out the paper", this);
    _even->setChecked(settings.value("paper/evenPaper", true).toBool());
    _even->setToolTip("Take out shadows, uneven light and the paper's tint, so all of the page reads as white");
    cleanForm->addRow(QString(), _even);
    _levels = new QCheckBox("Auto levels", this);
    _levels->setChecked(settings.value("paper/autoLevel", true).toBool());
    _levels->setToolTip("Stretch the darkest lines to black and the paper to white");
    cleanForm->addRow(QString(), _levels);
    _threshold = new HotSpinBox(this);
    _threshold->setRange(1, 255);
    _threshold->setValue(settings.value("paper/threshold", 170).toInt());
    _threshold->setToolTip("How dark a mark must be to count as a line: higher picks up fainter pencil, lower "
                           "leaves out smudges and construction lines");
    cleanForm->addRow("Line darkness", _threshold);
    QPushButton* autoThreshold = new QPushButton("Find Line Darkness", this);
    autoThreshold->setToolTip("Set Line darkness from this drawing: where its marks and its paper divide");
    connect(autoThreshold, &QPushButton::clicked, this, &PaperImportDialog::findThreshold);
    cleanForm->addRow(QString(), autoThreshold);
    _specks = new HotSpinBox(this);
    _specks->setRange(0, 500);
    _specks->setSuffix(" px");
    _specks->setValue(settings.value("paper/despeckle", 6).toInt());
    _specks->setToolTip("Dust and dots smaller than this go, and tiny holes in lines are filled; 0 keeps everything");
    cleanForm->addRow("Remove specks under", _specks);
    _gaps = new HotSpinBox(this);
    _gaps->setRange(0, 20);
    _gaps->setSuffix(" px");
    _gaps->setValue(settings.value("paper/closeGaps", 0).toInt());
    _gaps->setToolTip("Join lines that nearly meet, so the Paint Bucket can fill the shapes they make");
    cleanForm->addRow("Close gaps up to", _gaps);
    _smooth = new HotSpinBox(this);
    _smooth->setRange(0, 3);
    _smooth->setValue(settings.value("paper/smoothing", 1).toInt());
    _smooth->setToolTip("Smooth the pencil's rough edges before tracing: 0 keeps every wobble, 3 is smoothest");
    cleanForm->addRow("Smooth lines", _smooth);
    _fit = new QComboBox(this);
    for (const fla::trace::CurveFit& fit : fla::trace::kCurveFits)
        _fit->addItem(fit.label, fit.tolerance);
    _fit->setCurrentIndex(std::clamp(settings.value("paper/curveFit", 3).toInt(), 0, 5));
    _fit->setToolTip("How closely the traced lines follow the pixels; smoother lines have fewer points");
    cleanForm->addRow("Curve fit", _fit);
    _lineColor = QColor(settings.value("paper/lineColor", QColor(Qt::black).name()).toString());
    _color = new QPushButton(this);
    _color->setIcon(swatch(_lineColor));
    _color->setToolTip("The color the traced lines are drawn in");
    connect(_color, &QPushButton::clicked, this, [this] {
        const QColor chosen = SwatchPopup::getColor(_color, nullptr, _lineColor, "Line Color", false);
        if (!chosen.isValid())
            return;
        _lineColor = chosen;
        _color->setIcon(swatch(_lineColor));
        updatePreview();
    });
    cleanForm->addRow("Line color", _color);
    steps->addWidget(clean);

    // 3. Where they go.
    QGroupBox* place = new QGroupBox("3. Place", this);
    QFormLayout* placeForm = new QFormLayout(place);
    _layer = new QComboBox(this);
    _layer->addItems({"New layer", "Current layer"});
    _layer->setToolTip("A new layer named Paper Drawings, or the layer selected in the timeline");
    placeForm->addRow("Layer", _layer);
    _start = new HotSpinBox(this);
    _start->setRange(1, 99999);
    _start->setValue(currentFrame + 1);
    _start->setToolTip("The frame the first drawing goes on; the rest follow");
    placeForm->addRow("Start at frame", _start);
    _hold = new HotSpinBox(this);
    _hold->setRange(1, 24);
    _hold->setValue(settings.value("paper/hold", 1).toInt());
    _hold->setToolTip("How many frames each drawing stays: 1 for ones, 2 for twos");
    placeForm->addRow("Frames per drawing", _hold);
    _size = new QComboBox(this);
    _size->addItems({"Fit the stage", "Pixel for pixel"});
    _size->setCurrentIndex(settings.value("paper/actualSize", false).toBool() ? 1 : 0);
    _size->setToolTip("Scale each page to fit the stage, or keep a pixel of the scan to a pixel of the stage");
    placeForm->addRow("Size", _size);
    _lineUp = new QComboBox(this);
    _lineUp->addItems({"Keep page position", "Center each drawing"});
    _lineUp->setCurrentIndex(settings.value("paper/centerLines", false).toBool() ? 1 : 0);
    _lineUp->setToolTip("Keep page position for pages drawn on pegs, so they stay lined up; Center each drawing "
                        "puts each drawing's lines in the middle of the stage");
    placeForm->addRow("Line up", _lineUp);
    steps->addWidget(place);
    steps->addStretch(1);

    // The preview, beside the steps.
    QVBoxLayout* look = new QVBoxLayout;
    columns->addLayout(look);
    _preview = new QLabel(this);
    _preview->setFixedSize(kPreviewWidth, kPreviewHeight);
    _preview->setAlignment(Qt::AlignCenter);
    _preview->setFrameShape(QFrame::StyledPanel);
    look->addWidget(_preview);
    QFormLayout* lookForm = new QFormLayout;
    _which = new HotSpinBox(this);
    _which->setRange(1, 1);
    _which->setValue(1);
    _which->setToolTip("Which of the drawings the preview shows");
    lookForm->addRow("Preview drawing", _which);
    _showScan = new QCheckBox("Show the scan", this);
    _showScan->setToolTip("Show the drawing as it was scanned, to compare");
    lookForm->addRow(QString(), _showScan);
    look->addLayout(lookForm);
    _measure = new QLabel(this);
    _measure->setWordWrap(true);
    _measure->setFixedWidth(kPreviewWidth);
    look->addWidget(_measure);
    look->addStretch(1);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    _import = buttons->button(QDialogButtonBox::Ok);
    _import->setText("Import");
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (_files.isEmpty())
            return;
        QSettings settings;
        settings.setValue("paper/evenPaper", _even->isChecked());
        settings.setValue("paper/autoLevel", _levels->isChecked());
        settings.setValue("paper/threshold", _threshold->value());
        settings.setValue("paper/despeckle", _specks->value());
        settings.setValue("paper/closeGaps", _gaps->value());
        settings.setValue("paper/smoothing", _smooth->value());
        settings.setValue("paper/curveFit", _fit->currentIndex());
        settings.setValue("paper/lineColor", _lineColor.name());
        settings.setValue("paper/hold", _hold->value());
        settings.setValue("paper/actualSize", _size->currentIndex() == 1);
        settings.setValue("paper/centerLines", _lineUp->currentIndex() == 1);
        settings.setValue("importFolder", QFileInfo(_files.front()).absolutePath());
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    look->addWidget(buttons);

    connect(_path, &QLineEdit::textChanged, this, &PaperImportDialog::filesChanged);
    connect(_sequence, &QCheckBox::toggled, this, &PaperImportDialog::filesChanged);
    connect(_which, &HotSpinBox::valueChanged, this, [this] { loadPreview(); });
    for (QCheckBox* box : {_even, _levels, _showScan})
        connect(box, &QCheckBox::toggled, this, &PaperImportDialog::updatePreview);
    for (HotSpinBox* spin : {_threshold, _specks, _gaps, _smooth})
        connect(spin, &HotSpinBox::valueChanged, this, &PaperImportDialog::updatePreview);
    filesChanged();
}

void PaperImportDialog::browse()
{
    QSettings settings;
    const QStringList paths = QFileDialog::getOpenFileNames(this, "Import Paper Drawings",
        settings.value("importFolder").toString(), "Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.gif *.webp)");
    if (paths.isEmpty())
        return;
    // Several picked: those, in order, rather than a sequence.
    _sequence->setChecked(paths.size() == 1);
    _path->setText(paths.size() == 1 ? paths.front() : paths.join(';'));
}

void PaperImportDialog::filesChanged()
{
    _files.clear();
    const QStringList typed = _path->text().split(';', Qt::SkipEmptyParts);
    for (const QString& each : typed)
        if (QFileInfo(each.trimmed()).isFile())
            _files << each.trimmed();
    // One numbered picture brings its sequence along.
    QStringList sequence;
    if (_files.size() == 1)
        sequence = MediaImport::sequenceOf(_files.front());
    _sequence->setEnabled(!sequence.isEmpty());
    if (!sequence.isEmpty() && _sequence->isChecked())
        _files = sequence;

    if (_files.isEmpty())
        _found->setText(typed.isEmpty() ? "Pick a scan or a photo of a drawing." : "That picture cannot be found.");
    else if (_files.size() == 1)
        _found->setText(QString("1 drawing: %1").arg(QFileInfo(_files.front()).fileName()));
    else
        _found->setText(QString("%1 drawings: %2 to %3")
                            .arg(_files.size())
                            .arg(QFileInfo(_files.front()).fileName(), QFileInfo(_files.back()).fileName()));
    _which->setRange(1, std::max<int>(1, static_cast<int>(_files.size())));
    _import->setEnabled(!_files.isEmpty());
    loadPreview();
}

void PaperImportDialog::loadPreview()
{
    _work = QImage();
    const int index = _which->value() - 1;
    if (index >= 0 && index < _files.size())
    {
        const QImage image(_files.at(index));
        if (!image.isNull())
            _work = workImage(image);
    }
    updatePreview();
}

fla::paper::Settings PaperImportDialog::cleanupSettings() const
{
    fla::paper::Settings settings;
    settings.evenPaper = _even->isChecked();
    settings.autoLevel = _levels->isChecked();
    settings.threshold = _threshold->value();
    settings.despeckle = _specks->value();
    settings.closeGaps = _gaps->value();
    settings.smoothing = _smooth->value();
    settings.lineColor[0] = static_cast<uint8_t>(_lineColor.red());
    settings.lineColor[1] = static_cast<uint8_t>(_lineColor.green());
    settings.lineColor[2] = static_cast<uint8_t>(_lineColor.blue());
    settings.lineColor[3] = 255;
    return settings;
}

void PaperImportDialog::updatePreview()
{
    if (_work.isNull())
    {
        _preview->setPixmap(QPixmap());
        _preview->setText("The cleaned-up drawing shows here.");
        _measure->clear();
        return;
    }
    QImage shown;
    if (_showScan->isChecked())
        shown = _work;
    else
    {
        // The lines as they will be traced, on white paper.
        int lines = 0;
        const std::vector<uint8_t> cleaned =
            fla::paper::cleanup(bytesOf(_work).data(), _work.width(), _work.height(), cleanupSettings(), &lines);
        const QImage art(cleaned.data(), _work.width(), _work.height(), _work.width() * 4, QImage::Format_RGBA8888);
        shown = QImage(_work.size(), QImage::Format_RGB32);
        shown.fill(Qt::white);
        QPainter painter(&shown);
        painter.drawImage(0, 0, art);
        painter.end();
        const double share = 100.0 * lines / std::max(1, _work.width() * _work.height());
        QString advice;
        if (lines == 0)
            advice = " No lines found: raise Line darkness.";
        else if (share > 25.0)
            advice = " That is a lot of the page: lower Line darkness, or turn on Even out the paper.";
        _measure->setText(QString("Lines cover %1% of the page.%2").arg(share, 0, 'f', 1).arg(advice));
    }
    _preview->setPixmap(QPixmap::fromImage(
        shown.scaled(kPreviewWidth - 4, kPreviewHeight - 4, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

void PaperImportDialog::findThreshold()
{
    if (_work.isNull())
        return;
    const std::vector<uint8_t> leveled = fla::paper::level(
        fla::paper::lightness(bytesOf(_work).data(), _work.width(), _work.height()), _work.width(), _work.height(),
        _even->isChecked(), _levels->isChecked());
    _threshold->setValue(fla::paper::suggestThreshold(leveled));
}

PaperImportDialog::Choices PaperImportDialog::choices() const
{
    Choices choices;
    choices.files = _files;
    choices.cleanup = cleanupSettings();
    choices.curveFit = _fit->currentData().toDouble();
    choices.newLayer = _layer->currentIndex() == 0;
    choices.startFrame = _start->value() - 1;
    choices.hold = _hold->value();
    choices.fitStage = _size->currentIndex() == 0;
    choices.centerLines = _lineUp->currentIndex() == 1;
    return choices;
}

namespace PaperImport {

QString run(const PaperImportDialog::Choices& choices, fla::FLADocument& document, TimelineView& timelineView,
    fla::CommandStack& stack, fla::Selection& selection, QStringList& problems)
{
    fla::Document* doc = document.document;
    fla::Timeline* timeline = nullptr;
    int index = 0;
    if (!doc || !timelineView.currentLayer(timeline, index) || !timeline)
        return "There is no timeline to import onto.";

    // Every drawing cleaned up and traced first, so a picture that cannot
    // be read leaves no gap.
    std::vector<std::unique_ptr<fla::Shape>> shapes;
    for (const QString& path : choices.files)
    {
        const QImage image(path);
        if (image.isNull())
        {
            problems << QString("%1 could not be read.").arg(QFileInfo(path).fileName());
            continue;
        }
        const QImage work = PaperImportDialog::workImage(image);
        const std::vector<uint8_t> cleaned =
            fla::paper::cleanup(bytesOf(work).data(), work.width(), work.height(), choices.cleanup);
        std::unique_ptr<fla::Shape> shape(fla::paper::vectorize(cleaned, work.width(), work.height(), choices.curveFit));
        if (!shape)
        {
            problems << QString("No lines were found in %1.").arg(QFileInfo(path).fileName());
            continue;
        }

        // Onto the stage: the page fitted to it, or a scan pixel to a stage
        // pixel; the page centered, or the drawing's lines.
        const double stageWidth = doc->width;
        const double stageHeight = doc->height;
        const double scale = choices.fitStage
            ? std::min(stageWidth / work.width(), stageHeight / work.height())
            : static_cast<double>(image.width()) / work.width();
        double x = (stageWidth - work.width() * scale) / 2.0;
        double y = (stageHeight - work.height() * scale) / 2.0;
        int left = 0, top = 0, right = 0, bottom = 0;
        if (choices.centerLines && fla::paper::lineBounds(cleaned, work.width(), work.height(), left, top, right, bottom))
        {
            x = stageWidth / 2.0 - (left + right + 1) / 2.0 * scale;
            y = stageHeight / 2.0 - (top + bottom + 1) / 2.0 * scale;
        }
        shape->transform = fla::Transform(scale, 0.0, 0.0, scale, x, y);
        shapes.push_back(std::move(shape));
    }
    if (shapes.empty())
        return "Nothing was imported.";

    const char* name = "Import Paper Drawings";
    const int start = std::max(0, choices.startFrame);
    const int hold = std::max(1, choices.hold);
    const int count = static_cast<int>(shapes.size());
    fla::Layer* layer = index >= 0 && index < static_cast<int>(timeline->layers.size()) ? timeline->layers[index] : nullptr;
    const bool fresh = choices.newLayer || !layer || layer->isLocked() || layer->layerType == fla::Layer::Type::Folder;
    selection.clear();
    stack.beginMacro(name);
    if (fresh)
    {
        layer = fla::newLayerFor(*timeline, false);
        layer->name = "Paper Drawings";
        stack.push(fla::CommandPtr(new fla::AddLayerCommand(timeline, std::max(0, index), layer, name)));
    }
    auto frames = std::make_shared<std::vector<fla::Frame*>>();
    stack.push(fla::CommandPtr(new fla::EditFramesCommand({layer}, name,
        [layer, start, count, hold, frames](std::vector<fla::Frame*>&) {
            frames->clear();
            for (int i = 0; i < count; ++i)
                frames->push_back(fla::timeline::insertKeyframe(*layer, start + i * hold, true));
            // The last drawing held as long as the others.
            if (!frames->empty() && frames->back() && frames->back()->duration < hold)
                frames->back()->duration = hold;
        })));
    for (int i = 0; i < count; ++i)
    {
        fla::Frame* frame = i < static_cast<int>(frames->size()) ? (*frames)[static_cast<size_t>(i)] : nullptr;
        if (!frame)
            continue;
        fla::Shape* shape = shapes[static_cast<size_t>(i)].release();
        shape->parent = frame;
        stack.push(fla::CommandPtr(new fla::AddElementCommand(frame, shape, name)));
    }
    stack.endMacro();
    stack.breakMergeChain();

    const int last = start + (count - 1) * hold;
    return count == 1 ? QString("Imported 1 drawing onto %1, frame %2.")
                            .arg(QString::fromStdString(layer->name))
                            .arg(start + 1)
                      : QString("Imported %1 drawings onto %2, frames %3 to %4.")
                            .arg(count)
                            .arg(QString::fromStdString(layer->name))
                            .arg(start + 1)
                            .arg(last + 1);
}

void open(QWidget* parent, fla::FLADocument* document, PhoenixView* view, TimelineView* timelineView,
    Player* player, fla::CommandStack& stack, fla::Selection& selection, const std::function<void(const QString&)>& status)
{
    if (!document || !document->document || !view || !timelineView)
        return;
    PaperImportDialog dialog(player ? player->currentFrame() : 0, parent);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const PaperImportDialog::Choices choices = dialog.choices();
    view->flushPendingMerge();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QStringList problems;
    const QString message = run(choices, *document, *timelineView, stack, selection, problems);
    QApplication::restoreOverrideCursor();
    timelineView->refreshFrames();
    if (player)
        player->setCurrentFrame(std::max(0, choices.startFrame));
    view->invalidateBounds();
    view->clearCaches();
    view->update();
    if (status)
        status(message);
    if (!problems.isEmpty())
        QMessageBox::warning(parent, "Import Paper Drawings", problems.join("\n"));
}

} // namespace PaperImport
