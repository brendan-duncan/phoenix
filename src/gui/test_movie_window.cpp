#include "test_movie_window.h"

#include "phoenix_view.h"
#include "player.h"

#include "../data/document.h"
#include "../data/fla_document.h"
#include "../data/timeline.h"
#include "../play/movie_runtime.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

TestMovieWindow::TestMovieWindow(const fla::FLADocument& document, const QString& name,
    std::function<void(const QString&)> print, std::function<void(const QString&)> printError, QWidget* parent)
    : QDialog(parent)
{
    const fla::Document& data = *document.document;
    setWindowTitle(QString("Test Movie") + QString::fromUtf8(" \xE2\x80\x94 ") + name);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    _player = new Player(this);
    _view = new PhoenixView(_player, this);
    _view->setDocument(&document);
    layout->addWidget(_view, 1);

    QHBoxLayout* bar = new QHBoxLayout;
    bar->setContentsMargins(8, 4, 8, 4);
    QPushButton* restartButton = new QPushButton("Restart", this);
    restartButton->setToolTip("Play the movie again from its first frame");
    restartButton->setAutoDefault(false);
    connect(restartButton, &QPushButton::clicked, this, &TestMovieWindow::restart);
    bar->addWidget(restartButton);
    _status = new QLabel(this);
    _status->setForegroundRole(QPalette::PlaceholderText);
    bar->addWidget(_status, 1);
    layout->addLayout(bar);

    // Every scene, from the first, as the published movie plays them.
    std::vector<const fla::Timeline*> scenes(data.timelines.begin(), data.timelines.end());
    if (!scenes.empty())
    {
        _movie = std::make_unique<fla::play::MovieRuntime>(scenes, data.frameRate);
        _movie->trace = [print](const std::string& line) {
            if (print)
                print(QString::fromStdString(line));
        };
        _movie->error = [this, printError](const std::string& line) {
            ++_errors;
            if (printError)
                printError(QString::fromStdString(line));
            showFrame();
        };
        _movie->getURL = [](const std::string& url, const std::string&) {
            // Only web links leave the player, in the browser.
            const QUrl link(QString::fromStdString(url));
            if (link.scheme() == "http" || link.scheme() == "https")
                QDesktopServices::openUrl(link);
        };
        _movie->changed = [this]() { _view->update(); };
        _view->setMovie(_movie.get());
    }

    _timer = new QTimer(this);
    _timer->setTimerType(Qt::PreciseTimer);
    _timer->setInterval(std::max(1, static_cast<int>(std::lround(1000.0 / (data.frameRate > 0.0 ? data.frameRate : 24.0)))));
    connect(_timer, &QTimer::timeout, this, &TestMovieWindow::tick);

    // The stage at its own size, as far as the screen allows.
    resize(std::clamp(data.width + 40, 320, 1600), std::clamp(data.height + 80, 240, 1000));
    restart();
}

TestMovieWindow::~TestMovieWindow()
{
    if (_view)
        _view->setMovie(nullptr);
}

void TestMovieWindow::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    // The whole stage, whatever the window's size, once the view has its
    // own.
    QTimer::singleShot(0, this, [this]() { _view->zoomToFit(); });
}

void TestMovieWindow::restart()
{
    if (!_movie)
    {
        _status->setText("The document has no scene to play.");
        return;
    }
    _errors = 0;
    _movie->start();
    showFrame();
    _timer->start();
}

void TestMovieWindow::tick()
{
    if (!_movie)
        return;
    _movie->step();
    showFrame();
}

void TestMovieWindow::showFrame()
{
    if (!_movie)
        return;
    const fla::play::Clip& root = _movie->root();
    if (_player->currentFrame() != root.frame)
        _player->setCurrentFrame(root.frame);
    QString status = QString("Frame %1 of %2").arg(root.frame + 1).arg(root.length);
    if (_movie->sceneCount() > 1 && root.timeline)
        status = QString("%1, %2").arg(QString::fromStdString(root.timeline->name), status.toLower());
    if (!root.playing)
        status += ", stopped";
    if (_errors > 0)
        status += QString(", %1 script %2 (see the Output panel)").arg(_errors).arg(_errors == 1 ? "error" : "errors");
    _status->setText(status);
    _view->update();
}
