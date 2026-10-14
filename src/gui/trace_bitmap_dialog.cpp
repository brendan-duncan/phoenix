#include "trace_bitmap_dialog.h"

#include "hot_text.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

TraceBitmapDialog::TraceBitmapDialog(int count, Preview trace, QWidget* parent)
    : QDialog(parent)
    , _preview(std::move(trace))
{
    setWindowTitle("Trace Bitmap");
    QVBoxLayout* layout = new QVBoxLayout(this);
    QFormLayout* form = new QFormLayout;
    layout->addLayout(form);
    QSettings settings;
    _threshold = new HotSpinBox(this);
    _threshold->setRange(1, 500);
    _threshold->setValue(settings.value("trace/threshold", 100).toInt());
    _threshold->setToolTip("How different two colors may be and still make one area; higher makes fewer areas");
    form->addRow("Color threshold", _threshold);
    _area = new HotSpinBox(this);
    _area->setRange(1, 1000);
    _area->setSuffix(" px");
    _area->setValue(settings.value("trace/minimumArea", 8).toInt());
    _area->setToolTip("Areas smaller than this join their neighbors");
    form->addRow("Minimum area", _area);
    _fit = new QComboBox(this);
    for (const fla::trace::CurveFit& fit : fla::trace::kCurveFits)
        _fit->addItem(fit.label, fit.tolerance);
    _fit->setCurrentIndex(std::clamp(settings.value("trace/curveFit", 3).toInt(), 0, 5));
    _fit->setToolTip("How closely the outlines follow the pixels; smoother outlines have fewer points");
    form->addRow("Curve fit", _fit);

    _auto = new QCheckBox("Auto Preview", this);
    _auto->setToolTip("Trace the stage again whenever the settings change");
    _auto->setChecked(settings.value("trace/autoPreview", false).toBool());
    layout->addWidget(_auto);
    _status = new QLabel(count > 1 ? QString("Preview traces all %1 bitmaps selected on the stage.").arg(count)
                                   : QString("Preview traces the bitmap on the stage."),
        this);
    _status->setWordWrap(true);
    layout->addWidget(_status);
    _advice = new QLabel(this);
    _advice->setWordWrap(true);
    _advice->setForegroundRole(QPalette::PlaceholderText);
    _advice->hide();
    layout->addWidget(_advice);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText("Trace");
    QPushButton* previewButton = buttons->addButton("Preview", QDialogButtonBox::ActionRole);
    previewButton->setToolTip("Trace the stage with these settings; Cancel puts the bitmaps back");
    connect(previewButton, &QPushButton::clicked, this, &TraceBitmapDialog::preview);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        QSettings settings;
        settings.setValue("trace/threshold", _threshold->value());
        settings.setValue("trace/minimumArea", _area->value());
        settings.setValue("trace/curveFit", _fit->currentIndex());
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // Auto Preview waits for the settings to settle: a moment after a typed
    // value or a pick, longer while a value is scrubbed, so a drag does not
    // trace the stage at every step.
    _wait = new QTimer(this);
    _wait->setSingleShot(true);
    connect(_wait, &QTimer::timeout, this, &TraceBitmapDialog::preview);
    connect(_threshold, &HotSpinBox::valueChanged, this, [this](double, bool live) { settingsChanged(live); });
    connect(_area, &HotSpinBox::valueChanged, this, [this](double, bool live) { settingsChanged(live); });
    connect(_fit, &QComboBox::currentIndexChanged, this, [this] { settingsChanged(false); });
    connect(_auto, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue("trace/autoPreview", on);
        if (on)
            preview();
        else
            _wait->stop();
    });
    if (_auto->isChecked())
        QTimer::singleShot(0, this, &TraceBitmapDialog::preview);
}

fla::trace::Options TraceBitmapDialog::options() const
{
    fla::trace::Options options;
    options.colorThreshold = _threshold->value();
    options.minimumArea = _area->value();
    options.curveFit = _fit->currentData().toDouble();
    return options;
}

void TraceBitmapDialog::settingsChanged(bool live)
{
    if (!_auto->isChecked())
        return;
    _wait->start(live ? 500 : 150);
}

void TraceBitmapDialog::preview()
{
    _wait->stop();
    if (!_preview)
        return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const Counts counts = _preview(options());
    QApplication::restoreOverrideCursor();
    const QLocale locale;
    _status->setText(QString("%1 %2, %3 points")
                         .arg(locale.toString(counts.areas), counts.areas == 1 ? "area" : "areas",
                             locale.toString(counts.points)));
    // Some advice from the count against the pictures' size.
    const double pixelsPerArea = counts.areas > 0 ? static_cast<double>(counts.pixels) / counts.areas : 0.0;
    QString advice;
    if (counts.areas == 0)
        advice = "Nothing to trace: the bitmap is clear.";
    else if (counts.areas > 4000 || pixelsPerArea < 60.0)
        advice = "Many small areas, which is slow to draw and edit. Raise Color threshold to merge shades, or "
                 "Minimum area to fold specks into their neighbors.";
    else if (counts.areas < 4 && counts.pixels > 2500)
        advice = "Very few areas: colors that should stay apart may have run together. Lower Color threshold.";
    _advice->setText(advice);
    _advice->setVisible(!advice.isEmpty());
}
