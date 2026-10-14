#include "lip_sync_dialog.h"

#include "media_import.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include "hot_text.h"
#include <QStandardPaths>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QVBoxLayout>

namespace {

/// Rhubarb as it was set, or found on the path or beside Phoenix.
QString findRhubarb()
{
    const QString saved = QSettings().value("lipsync/rhubarb").toString();
    if (!saved.isEmpty() && QFileInfo::exists(saved))
        return saved;
    const QString onPath = QStandardPaths::findExecutable("rhubarb");
    if (!onPath.isEmpty())
        return onPath;
    const QString beside = QDir(QCoreApplication::applicationDirPath()).filePath("rhubarb/rhubarb.exe");
    return QFileInfo::exists(beside) ? beside : QString();
}

QHBoxLayout* fileRow(QLineEdit* edit, QPushButton* browse)
{
    QHBoxLayout* row = new QHBoxLayout();
    row->addWidget(edit, 1);
    row->addWidget(browse);
    return row;
}

} // namespace

LipSyncDialog::LipSyncDialog(const QString& mouthName, const std::map<std::string, int>& savedChart, int startFrame,
    double frameRate, QWidget* parent)
    : QDialog(parent)
    , _frameRate(frameRate)
    , _savedChart(savedChart)
{
    setWindowTitle("Lip Sync");
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QString("Mouth: <b>%1</b>, keyed on its layer from the frame below.").arg(mouthName.toHtmlEscaped()), this));

    // From a recording, through Rhubarb.
    _fromAudio = new QRadioButton("Work out the mouth shapes from a voice recording", this);
    _fromAudio->setChecked(true);
    layout->addWidget(_fromAudio);
    QFormLayout* audioForm = new QFormLayout();
    _audio = new QLineEdit(this);
    _audio->setPlaceholderText("A WAV or Ogg recording of the line");
    QPushButton* audioBrowse = new QPushButton("Browse...", this);
    audioForm->addRow("Recording:", fileRow(_audio, audioBrowse));
    _rhubarb = new QLineEdit(findRhubarb(), this);
    _rhubarb->setPlaceholderText("rhubarb.exe (github.com/DanielSWolf/rhubarb-lip-sync)");
    QPushButton* rhubarbBrowse = new QPushButton("Browse...", this);
    _rhubarb->setPlaceholderText("Optional: Rhubarb Lip Sync gives better shapes than Phoenix's own listening");
    audioForm->addRow("Rhubarb:", fileRow(_rhubarb, rhubarbBrowse));
    _script = new QPlainTextEdit(this);
    _script->setPlaceholderText("The words spoken (optional): helps Rhubarb get them right");
    _script->setMaximumHeight(60);
    audioForm->addRow("Words:", _script);
    layout->addLayout(audioForm);

    // From cues worked out elsewhere.
    _fromCues = new QRadioButton("Load mouth shapes worked out before: Rhubarb TSV, or Papagayo / Moho .dat", this);
    layout->addWidget(_fromCues);
    QFormLayout* cueForm = new QFormLayout();
    _cueFile = new QLineEdit(this);
    QPushButton* cueBrowse = new QPushButton("Browse...", this);
    cueForm->addRow("Cue file:", fileRow(_cueFile, cueBrowse));
    layout->addLayout(cueForm);

    QPushButton* readButton = new QPushButton("Read Mouth Shapes", this);
    layout->addWidget(readButton);
    _status = new QLabel(this);
    layout->addWidget(_status);

    layout->addWidget(new QLabel("Mouth chart: the frame of the mouth graphic each shape is drawn on.", this));
    _chart = new QTableWidget(0, 2, this);
    _chart->setHorizontalHeaderLabels({"Shape", "Frame"});
    _chart->horizontalHeader()->setStretchLastSection(true);
    _chart->verticalHeader()->setVisible(false);
    _chart->setMinimumHeight(160);
    layout->addWidget(_chart, 1);

    QFormLayout* startForm = new QFormLayout();
    _start = new HotSpinBox(this);
    _start->setRange(1, 100000);
    _start->setValue(startFrame + 1);
    startForm->addRow("Starts on frame:", _start);
    layout->addLayout(startForm);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText("Key the Mouth");
    layout->addWidget(buttons);

    connect(audioBrowse, &QPushButton::clicked, this, [this]() {
        const QString file = QFileDialog::getOpenFileName(this, "Recording", _audio->text(), "Audio (*.wav *.ogg);;All files (*)");
        if (!file.isEmpty())
        {
            _audio->setText(file);
            _fromAudio->setChecked(true);
        }
    });
    connect(rhubarbBrowse, &QPushButton::clicked, this, [this]() {
        const QString file = QFileDialog::getOpenFileName(this, "Rhubarb Lip Sync", _rhubarb->text(), "rhubarb (rhubarb*);;All files (*)");
        if (!file.isEmpty())
        {
            _rhubarb->setText(file);
            QSettings().setValue("lipsync/rhubarb", file);
        }
    });
    connect(cueBrowse, &QPushButton::clicked, this, [this]() {
        const QString file = QFileDialog::getOpenFileName(this, "Cue File", _cueFile->text(),
            "Mouth cues (*.tsv *.txt *.dat);;All files (*)");
        if (!file.isEmpty())
        {
            _cueFile->setText(file);
            _fromCues->setChecked(true);
        }
    });
    connect(readButton, &QPushButton::clicked, this, [this]() { read(); });
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (_cues.empty() && !read())
            return;
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    resize(560, 620);
}

bool LipSyncDialog::runRhubarb(std::string& text)
{
    const QString rhubarb = _rhubarb->text().trimmed();
    const QString audio = _audio->text().trimmed();
    if (audio.isEmpty() || !QFileInfo::exists(audio))
    {
        QMessageBox::information(this, "Lip Sync", "Choose the recording of the line.");
        return false;
    }
    if (rhubarb.isEmpty() || !QFileInfo::exists(rhubarb))
    {
        // No Rhubarb: Phoenix listens itself, by loudness.
        fla::media::Sound sound;
        QString error;
        if (!MediaImport::loadSound(audio, sound, error))
        {
            QMessageBox::warning(this, "Lip Sync", error);
            return false;
        }
        text = fla::lipsync::tsvOf(fla::lipsync::listen(sound.samples, sound.channels, sound.rate, _frameRate));
        return true;
    }
    QSettings().setValue("lipsync/rhubarb", rhubarb);
    QTemporaryDir folder;
    QStringList arguments{"-f", "tsv", "-o", folder.filePath("cues.tsv")};
    if (!_script->toPlainText().trimmed().isEmpty())
    {
        QFile words(folder.filePath("words.txt"));
        if (words.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            words.write(_script->toPlainText().toUtf8());
            words.close();
            arguments << "-d" << words.fileName();
        }
    }
    arguments << audio;
    QProcess process;
    process.start(rhubarb, arguments);
    QProgressDialog progress("Listening to the recording...", "Cancel", 0, 0, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.show();
    while (!process.waitForFinished(100))
    {
        QApplication::processEvents();
        if (progress.wasCanceled())
        {
            process.kill();
            process.waitForFinished();
            return false;
        }
        if (process.state() == QProcess::NotRunning)
            break;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        QMessageBox::warning(this, "Lip Sync", QString("Rhubarb could not read the recording:\n\n%1")
                                                   .arg(QString::fromLocal8Bit(process.readAllStandardError()).right(800)));
        return false;
    }
    QFile cues(folder.filePath("cues.tsv"));
    if (!cues.open(QIODevice::ReadOnly))
        return false;
    text = cues.readAll().toStdString();
    return true;
}

bool LipSyncDialog::read()
{
    std::string text;
    if (_fromAudio->isChecked())
    {
        if (!runRhubarb(text))
            return false;
    }
    else
    {
        QFile file(_cueFile->text());
        if (!file.open(QIODevice::ReadOnly))
        {
            QMessageBox::information(this, "Lip Sync", "Choose the cue file.");
            return false;
        }
        text = file.readAll().toStdString();
    }
    _cues = fla::lipsync::parse(text, _frameRate);
    if (_cues.empty())
    {
        QMessageBox::information(this, "Lip Sync", "No mouth shapes were found in that.");
        return false;
    }
    const double seconds = _cues.back().time;
    _status->setText(QString("%1 mouth shapes over %2 seconds.").arg(_cues.size()).arg(seconds, 0, 'f', 1));
    fillChart();
    return true;
}

void LipSyncDialog::fillChart()
{
    const std::vector<std::string> shapes = fla::lipsync::shapesOf(_cues);
    std::map<std::string, int> chart = fla::lipsync::defaultChart(shapes);
    // A chart kept on the mouth from before wins.
    for (auto& [shape, frame] : chart)
        if (_savedChart.count(shape))
            frame = _savedChart.at(shape);
    _chart->setRowCount(static_cast<int>(shapes.size()));
    for (int row = 0; row < static_cast<int>(shapes.size()); ++row)
    {
        const std::string& shape = shapes[static_cast<size_t>(row)];
        QTableWidgetItem* name = new QTableWidgetItem(QString::fromStdString(shape));
        name->setFlags(Qt::ItemIsEnabled);
        _chart->setItem(row, 0, name);
        HotSpinBox* frame = new HotSpinBox(_chart);
        frame->setRange(1, 10000);
        frame->setValue(chart[shape]);
        _chart->setCellWidget(row, 1, frame);
    }
}

std::map<std::string, int> LipSyncDialog::chart() const
{
    std::map<std::string, int> chart;
    for (int row = 0; row < _chart->rowCount(); ++row)
    {
        const auto* frame = qobject_cast<const HotSpinBox*>(_chart->cellWidget(row, 1));
        if (_chart->item(row, 0) && frame)
            chart[_chart->item(row, 0)->text().toStdString()] = frame->value();
    }
    return chart;
}

int LipSyncDialog::startFrame() const
{
    return _start->value() - 1;
}
