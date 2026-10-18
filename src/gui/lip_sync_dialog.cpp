#include "lip_sync_dialog.h"

#include "audio_engine.h"
#include "media_import.h"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QComboBox>
#include <QDataStream>
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

/// Interleaved stereo float samples as a 16-bit WAV file.
bool writeWav(QIODevice& device, const std::vector<float>& samples, int rate)
{
    QDataStream out(&device);
    out.setByteOrder(QDataStream::LittleEndian);
    const size_t count = std::min(samples.size(), static_cast<size_t>((0xFFFFFFFFull - 36) / 2));
    const quint32 dataBytes = static_cast<quint32>(count * 2);
    out.writeRawData("RIFF", 4);
    out << quint32(36 + dataBytes);
    out.writeRawData("WAVE", 4);
    out.writeRawData("fmt ", 4);
    out << quint32(16) << quint16(1) << quint16(2) << quint32(rate) << quint32(rate * 4) << quint16(4) << quint16(16);
    out.writeRawData("data", 4);
    out << dataBytes;
    for (size_t i = 0; i < count; ++i)
        out << qint16(std::lround(std::clamp(samples[i], -1.0f, 1.0f) * 32767.0f));
    return out.status() == QDataStream::Ok;
}

QHBoxLayout* fileRow(QLineEdit* edit, QPushButton* browse)
{
    QHBoxLayout* row = new QHBoxLayout();
    row->addWidget(edit, 1);
    row->addWidget(browse);
    return row;
}

} // namespace

LipSyncDialog::LipSyncDialog(const QString& mouthName, const std::map<std::string, int>& savedChart,
    const std::map<std::string, int>& labels, const QStringList& sounds, const QString& sound, SoundSource soundOf,
    int startFrame, double frameRate, QWidget* parent)
    : QDialog(parent)
    , _frameRate(frameRate)
    , _savedChart(savedChart)
    , _labels(labels)
    , _soundOf(std::move(soundOf))
{
    setWindowTitle("Lip Sync");
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QString("Mouth: <b>%1</b>, keyed on its layer from the frame below.").arg(mouthName.toHtmlEscaped()), this));

    // From a recording: a library sound, or a file on disk.
    _fromAudio = new QRadioButton("Work out the mouth shapes from a voice recording", this);
    _fromAudio->setChecked(true);
    layout->addWidget(_fromAudio);
    QFormLayout* audioForm = new QFormLayout();
    _sound = new QComboBox(this);
    if (_soundOf)
        for (const QString& name : sounds)
            _sound->addItem(name, name);
    _sound->addItem("A recording on disk", QString());
    _sound->setCurrentIndex(std::max(0, _sound->findData(sound)));
    audioForm->addRow("Sound:", _sound);
    _audio = new QLineEdit(this);
    _audio->setPlaceholderText("A WAV or Ogg recording of the line");
    QPushButton* audioBrowse = new QPushButton("Browse...", this);
    audioForm->addRow("Recording:", fileRow(_audio, audioBrowse));
    const auto onDisk = [this, audioBrowse]() {
        const bool file = librarySound().isEmpty();
        _audio->setEnabled(file);
        audioBrowse->setEnabled(file);
    };
    onDisk();
    connect(_sound, &QComboBox::currentIndexChanged, this, [this, onDisk]() {
        onDisk();
        _fromAudio->setChecked(true);
    });
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
    // Rows as tall as a line of text, and room for the nine shapes of the
    // usual mouth chart (A to H and X) without scrolling.
    const int row = fontMetrics().height() + 10;
    _chart->verticalHeader()->setDefaultSectionSize(row);
    _chart->setMinimumHeight(_chart->horizontalHeader()->sizeHint().height() + 9 * row + 2 * _chart->frameWidth());
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
    resize(560, 700);
}

bool LipSyncDialog::runRhubarb(std::string& text)
{
    const QString rhubarb = _rhubarb->text().trimmed();
    QString audio = _audio->text().trimmed();
    const QString library = librarySound();
    std::shared_ptr<const PcmBuffer> pcm;
    if (!library.isEmpty())
    {
        pcm = _soundOf(library);
        if (!pcm || pcm->samples.empty())
        {
            QMessageBox::information(this, "Lip Sync", QString("Phoenix cannot read the sound %1.").arg(library));
            return false;
        }
    }
    else if (audio.isEmpty() || !QFileInfo::exists(audio))
    {
        QMessageBox::information(this, "Lip Sync", "Choose the recording of the line.");
        return false;
    }
    if ((rhubarb.isEmpty() || !QFileInfo::exists(rhubarb)) && pcm)
    {
        // No Rhubarb: Phoenix listens to the library sound itself.
        std::vector<int16_t> samples(pcm->samples.size());
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = static_cast<int16_t>(std::lround(std::clamp(pcm->samples[i], -1.0f, 1.0f) * 32767.0f));
        text = fla::lipsync::tsvOf(fla::lipsync::listen(samples, 2, PcmBuffer::kRate, _frameRate));
        return true;
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
    if (pcm)
    {
        // Rhubarb reads a file: the library sound, written out as a WAV.
        QFile wav(folder.filePath("voice.wav"));
        if (!wav.open(QIODevice::WriteOnly) || !writeWav(wav, pcm->samples, PcmBuffer::kRate))
        {
            QMessageBox::warning(this, "Lip Sync", "Phoenix could not hand the sound to Rhubarb.");
            return false;
        }
        wav.close();
        audio = wav.fileName();
    }
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
    std::map<std::string, int> chart = fla::lipsync::defaultChart(shapes, _labels);
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

QString LipSyncDialog::librarySound() const
{
    return _sound->currentData().toString();
}

int LipSyncDialog::startFrame() const
{
    return _start->value() - 1;
}
