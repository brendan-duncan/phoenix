#include "render_queue.h"
#include "modal.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSettings>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QDir>

#include <algorithm>

QStringList RenderJob::arguments() const
{
    if (!data.isEmpty())
        return {"render", document, "--data", data, "--out", output, "--format", format.isEmpty() ? QString("mp4") : format};
    QStringList args{"render", document, "--out", output};
    if (!range.isEmpty())
        args << "--range" << range;
    if (!size.isEmpty())
        args << "--size" << size;
    if (scene == "*")
        args << "--all-scenes";
    else if (scene == "+")
        args << "--each-scene";
    else if (!scene.isEmpty())
        args << "--scene" << scene;
    return args + options;
}

namespace {

QString stateText(const RenderJob& job)
{
    switch (job.state)
    {
    case RenderJob::State::Waiting: return "Waiting";
    case RenderJob::State::Rendering: return "Rendering...";
    case RenderJob::State::Done: return "Done";
    case RenderJob::State::Failed: return "Failed";
    }
    return QString();
}

/// The options a job can turn on, with what they mean.
const std::pair<const char*, const char*> kOptions[] = {
    {"--transparent", "Transparent background"},
    {"--no-camera", "Without the camera"},
    {"--everything", "Everything, the pasteboard too"},
    {"--prores", "ProRes 422 HQ (a .mov)"},
    {"--prores4444", "ProRes 4444 with alpha (a .mov)"},
    {"--per-layer", "A PNG sequence per layer"},
    {"--passes", "Compositing passes"},
    {"--no-audio", "Without sound"},
    {"--safe-zones", "Safe zones drawn over the frames (a review copy)"},
};

} // namespace

RenderQueue::RenderQueue(std::function<QString()> current, QWidget* parent)
    : QDialog(parent)
    , _current(std::move(current))
{
    setWindowTitle("Render Queue");
    QVBoxLayout* layout = new QVBoxLayout(this);
    _list = new QTreeWidget(this);
    _list->setColumnCount(4);
    _list->setHeaderLabels({"Document", "Output", "Frames", "Status"});
    _list->setRootIsDecorated(false);
    _list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    _list->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    _list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(_list, 1);

    QHBoxLayout* buttons = new QHBoxLayout;
    QPushButton* addCurrent = new QPushButton("Add Current", this);
    addCurrent->setToolTip("Queue the document open now (saved first: the render reads the file)");
    QPushButton* addFiles = new QPushButton("Add Files...", this);
    QPushButton* edit = new QPushButton("Edit...", this);
    QPushButton* remove = new QPushButton("Remove", this);
    QPushButton* up = new QPushButton("Up", this);
    QPushButton* down = new QPushButton("Down", this);
    QPushButton* clearDone = new QPushButton("Clear Finished", this);
    QPushButton* addData = new QPushButton("Add Data...", this);
    addData->setToolTip("Render the current document once for each row of a CSV file, its template fields filled from the row");
    for (QPushButton* button : {addCurrent, addFiles, addData, edit, remove, up, down, clearDone})
        buttons->addWidget(button);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    QHBoxLayout* run = new QHBoxLayout;
    _status = new QLabel(this);
    run->addWidget(_status, 1);
    _start = new QPushButton("Render All", this);
    _stop = new QPushButton("Stop", this);
    _stop->setEnabled(false);
    run->addWidget(_start);
    run->addWidget(_stop);
    layout->addLayout(run);

    connect(addCurrent, &QPushButton::clicked, this, [this]() {
        const QString document = _current ? _current() : QString();
        if (document.isEmpty())
        {
            modal::information(this, "Render Queue", "Save the document first: the queue renders the saved file.");
            return;
        }
        addJob(document);
    });
    connect(addData, &QPushButton::clicked, this, [this]() {
        const QString document = _current ? _current() : QString();
        if (document.isEmpty())
        {
            modal::information(this, "Render Queue", "Save the document first: the queue renders the saved file.");
            return;
        }
        const QString data = QFileDialog::getOpenFileName(this, "Rows to Render", QFileInfo(document).absolutePath(),
            "CSV files (*.csv);;All Files (*)");
        if (!data.isEmpty())
            addJob(document, data);
    });
    connect(addFiles, &QPushButton::clicked, this, [this]() {
        const QStringList files = QFileDialog::getOpenFileNames(this, "Add to Render Queue", QString(),
            "Documents (*.fla *.phx *.xfl);;All Files (*)");
        for (const QString& file : files)
            addJob(file);
    });
    connect(edit, &QPushButton::clicked, this, [this]() {
        const int row = _list->indexOfTopLevelItem(_list->currentItem());
        if (row >= 0 && editJob(_jobs[static_cast<size_t>(row)]))
        {
            _jobs[static_cast<size_t>(row)].state = RenderJob::State::Waiting;
            save();
            fill();
        }
    });
    connect(_list, &QTreeWidget::itemDoubleClicked, edit, &QPushButton::click);
    connect(remove, &QPushButton::clicked, this, [this]() {
        std::vector<int> rows;
        for (QTreeWidgetItem* item : _list->selectedItems())
            rows.push_back(_list->indexOfTopLevelItem(item));
        std::sort(rows.rbegin(), rows.rend());
        for (int row : rows)
            if (row != _running && row >= 0)
            {
                _jobs.erase(_jobs.begin() + row);
                if (_running > row)
                    --_running;
            }
        save();
        fill();
    });
    const auto move = [this](int by) {
        const int row = _list->indexOfTopLevelItem(_list->currentItem());
        const int to = row + by;
        if (row < 0 || to < 0 || to >= static_cast<int>(_jobs.size()) || row == _running || to == _running)
            return;
        std::swap(_jobs[static_cast<size_t>(row)], _jobs[static_cast<size_t>(to)]);
        save();
        fill();
        _list->setCurrentItem(_list->topLevelItem(to));
    };
    connect(up, &QPushButton::clicked, this, [move]() { move(-1); });
    connect(down, &QPushButton::clicked, this, [move]() { move(1); });
    connect(clearDone, &QPushButton::clicked, this, [this]() {
        for (int i = static_cast<int>(_jobs.size()) - 1; i >= 0; --i)
            if (_jobs[static_cast<size_t>(i)].state == RenderJob::State::Done)
                _jobs.erase(_jobs.begin() + i);
        save();
        fill();
    });
    connect(_start, &QPushButton::clicked, this, &RenderQueue::start);
    connect(_stop, &QPushButton::clicked, this, &RenderQueue::stop);

    load();
    fill();
    resize(760, 380);
}

RenderQueue::~RenderQueue()
{
    if (_process && _process->state() != Process::NotRunning)
    {
        _stopping = true;
        _process->kill();
        _process->waitForFinished(2000);
    }
}

void RenderQueue::fill()
{
    _list->clear();
    for (const RenderJob& job : _jobs)
    {
        QString frames = !job.data.isEmpty() ? QString("A movie a row of %1").arg(QFileInfo(job.data).fileName())
            : job.range.isEmpty() ? QString("All") : job.range;
        if (job.scene == "*")
            frames += ", all scenes";
        else if (job.scene == "+")
            frames += ", each scene";
        else if (!job.scene.isEmpty())
            frames += ", " + job.scene;
        auto* item = new QTreeWidgetItem(_list, {QFileInfo(job.document).fileName(), QDir::toNativeSeparators(job.output),
                                                    frames, stateText(job)});
        item->setToolTip(0, QDir::toNativeSeparators(job.document));
        if (!job.message.isEmpty())
            item->setToolTip(3, job.message);
    }
    int waiting = 0;
    for (const RenderJob& job : _jobs)
        waiting += job.state == RenderJob::State::Waiting ? 1 : 0;
    if (_running < 0)
        _status->setText(QString("%1 waiting").arg(waiting));
}

void RenderQueue::save() const
{
    QJsonArray list;
    for (const RenderJob& job : _jobs)
    {
        QJsonObject entry;
        entry["document"] = job.document;
        entry["output"] = job.output;
        entry["range"] = job.range;
        entry["size"] = job.size;
        entry["scene"] = job.scene;
        entry["options"] = QJsonArray::fromStringList(job.options);
        if (!job.data.isEmpty())
        {
            entry["data"] = job.data;
            entry["format"] = job.format;
        }
        entry["done"] = job.state == RenderJob::State::Done;
        list.append(entry);
    }
    QSettings().setValue("renderQueue/jobs", QJsonDocument(list).toJson(QJsonDocument::Compact));
}

void RenderQueue::load()
{
    const QJsonArray list = QJsonDocument::fromJson(QSettings().value("renderQueue/jobs").toByteArray()).array();
    for (const QJsonValue& value : list)
    {
        const QJsonObject entry = value.toObject();
        RenderJob job;
        job.document = entry["document"].toString();
        job.output = entry["output"].toString();
        job.range = entry["range"].toString();
        job.size = entry["size"].toString();
        job.scene = entry["scene"].toString();
        for (const QJsonValue& option : entry["options"].toArray())
            job.options << option.toString();
        job.data = entry["data"].toString();
        job.format = entry["format"].toString();
        job.state = entry["done"].toBool() ? RenderJob::State::Done : RenderJob::State::Waiting;
        if (!job.document.isEmpty())
            _jobs.push_back(job);
    }
}

void RenderQueue::addJob(const QString& document, const QString& data)
{
    RenderJob job;
    job.document = QFileInfo(document).absoluteFilePath();
    const QFileInfo info(job.document);
    job.output = info.absoluteDir().filePath(info.completeBaseName() + ".mp4");
    if (!data.isEmpty())
    {
        job.data = QFileInfo(data).absoluteFilePath();
        job.output = info.absoluteDir().filePath(info.completeBaseName() + "_renders");
        job.format = "mp4";
    }
    if (!editJob(job))
        return;
    _jobs.push_back(job);
    save();
    fill();
}

bool RenderQueue::editJob(RenderJob& job)
{
    QDialog dialog(this);
    dialog.setWindowTitle(QFileInfo(job.document).fileName());
    QFormLayout* form = new QFormLayout(&dialog);
    if (!job.data.isEmpty())
        return editDataJob(job, dialog, form);
    QLineEdit* output = new QLineEdit(job.output, &dialog);
    QPushButton* browse = new QPushButton("Browse...", &dialog);
    QHBoxLayout* outputRow = new QHBoxLayout;
    outputRow->addWidget(output, 1);
    outputRow->addWidget(browse);
    form->addRow("Output", outputRow);
    form->addRow("", new QLabel("A folder for a PNG sequence, or a file: .mp4, .mov, .gif, .svg (a sequence) or .swf.", &dialog));
    connect(browse, &QPushButton::clicked, &dialog, [&dialog, output]() {
        const QString chosen = QFileDialog::getSaveFileName(&dialog, "Render To", output->text(),
            "Video (*.mp4 *.mov);;GIF (*.gif);;SVG Sequence (*.svg);;SWF (*.swf);;PNG Sequence (*.png)");
        if (!chosen.isEmpty())
            output->setText(chosen);
    });
    QLineEdit* range = new QLineEdit(job.range, &dialog);
    range->setPlaceholderText("All, or 1-240");
    form->addRow("Frames", range);
    QLineEdit* size = new QLineEdit(job.size, &dialog);
    size->setPlaceholderText("The stage's, or 1920x1080");
    form->addRow("Size", size);
    QComboBox* scenes = new QComboBox(&dialog);
    scenes->setEditable(true);
    scenes->addItem("The scene it opens on", QString());
    scenes->addItem("All scenes, one after another", QString("*"));
    scenes->addItem("Each scene to its own file", QString("+"));
    const int known = scenes->findData(job.scene);
    if (known >= 0)
        scenes->setCurrentIndex(known);
    else
        scenes->setEditText(job.scene);
    scenes->setToolTip("Or type a scene's name");
    form->addRow("Scenes", scenes);
    std::vector<QCheckBox*> boxes;
    for (const auto& [flag, label] : kOptions)
    {
        QCheckBox* box = new QCheckBox(label, &dialog);
        box->setChecked(job.options.contains(flag));
        form->addRow("", box);
        boxes.push_back(box);
    }
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    job.output = output->text().trimmed();
    job.range = range->text().trimmed();
    job.size = size->text().trimmed();
    const int index = scenes->findText(scenes->currentText());
    job.scene = index >= 0 ? scenes->itemData(index).toString() : scenes->currentText().trimmed();
    job.options.clear();
    for (size_t i = 0; i < boxes.size(); ++i)
        if (boxes[i]->isChecked())
            job.options << kOptions[i].first;
    return !job.output.isEmpty();
}

bool RenderQueue::editDataJob(RenderJob& job, QDialog& dialog, QFormLayout* form)
{
    // A movie for each row: the rows, the folder they go into, and as what.
    QLineEdit* data = new QLineEdit(job.data, &dialog);
    QPushButton* browseData = new QPushButton("Browse...", &dialog);
    QHBoxLayout* dataRow = new QHBoxLayout;
    dataRow->addWidget(data, 1);
    dataRow->addWidget(browseData);
    form->addRow("Rows", dataRow);
    connect(browseData, &QPushButton::clicked, &dialog, [&dialog, data]() {
        const QString chosen = QFileDialog::getOpenFileName(&dialog, "Rows to Render", data->text(), "CSV files (*.csv);;All Files (*)");
        if (!chosen.isEmpty())
            data->setText(chosen);
    });
    QLineEdit* output = new QLineEdit(job.output, &dialog);
    QPushButton* browse = new QPushButton("Browse...", &dialog);
    QHBoxLayout* outputRow = new QHBoxLayout;
    outputRow->addWidget(output, 1);
    outputRow->addWidget(browse);
    form->addRow("Into folder", outputRow);
    connect(browse, &QPushButton::clicked, &dialog, [&dialog, output]() {
        const QString chosen = QFileDialog::getExistingDirectory(&dialog, "Render Into", output->text());
        if (!chosen.isEmpty())
            output->setText(chosen);
    });
    QComboBox* format = new QComboBox(&dialog);
    format->addItems({"mp4", "mov", "gif", "png"});
    format->setCurrentText(job.format.isEmpty() ? QString("mp4") : job.format);
    form->addRow("Each row as", format);
    form->addRow("", new QLabel("The first row names the template fields; each row after it is a movie,\n"
                                "named by its file column when it has one.", &dialog));
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    job.data = data->text().trimmed();
    job.output = output->text().trimmed();
    job.format = format->currentText();
    return !job.data.isEmpty() && !job.output.isEmpty();
}

void RenderQueue::start()
{
    if (_running >= 0)
        return;
    _stopping = false;
    _start->setEnabled(false);
    _stop->setEnabled(true);
    runNext();
}

void RenderQueue::runNext()
{
    _running = -1;
    for (size_t i = 0; i < _jobs.size(); ++i)
        if (_jobs[i].state == RenderJob::State::Waiting)
        {
            _running = static_cast<int>(i);
            break;
        }
    if (_running < 0 || _stopping)
    {
        _running = -1;
        _start->setEnabled(true);
        _stop->setEnabled(false);
        fill();
        return;
    }
    RenderJob& job = _jobs[static_cast<size_t>(_running)];
    job.state = RenderJob::State::Rendering;
    job.message.clear();
    fill();
    _status->setText(QString("Rendering %1...").arg(QFileInfo(job.document).fileName()));
    // A Phoenix of its own, with no window.
    _process = new Process(this);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    // The offscreen platform where it was installed, the usual one where
    // not: a render makes no windows either way.
    environment.insert("QT_QPA_PLATFORM", "offscreen;windows");
    _process->setProcessEnvironment(environment);
    _process->setProcessChannelMode(Process::MergedChannels);
    connect(_process, &Process::finished, this, [this](int code, Process::ExitStatus status) {
        Process* finished = _process;
        _process = nullptr;
        const QString log = QString::fromLocal8Bit(finished->readAll()).trimmed();
        finished->deleteLater();
        if (_running >= 0 && _running < static_cast<int>(_jobs.size()))
        {
            RenderJob& done = _jobs[static_cast<size_t>(_running)];
            const bool ok = status == Process::NormalExit && code == 0;
            done.state = ok ? RenderJob::State::Done : (_stopping ? RenderJob::State::Waiting : RenderJob::State::Failed);
            done.message = log;
        }
        save();
        runNext();
    });
    _process->start(QCoreApplication::applicationFilePath(), job.arguments());
}

void RenderQueue::stop()
{
    _stopping = true;
    if (_process)
        _process->kill();
    else
        runNext();
}
