#include "test_movie_window.h"

#include "audio_engine.h"
#include "phoenix_view.h"
#include "player.h"
#include "timeline_audio.h"

#include "../data/document.h"
#include "../data/fla_document.h"
#include "../data/symbol.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"
#include "../edit/frame_sound.h"
#include "../play/movie_runtime.h"
#include "../third_party/tinyxml2/tinyxml2.h"

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

namespace {

/// A key as Flash numbers it (Key.LEFT is 37, "A" 65), or 0 for one it
/// does not.
int flashKeyCode(int key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return 65 + (key - Qt::Key_A);
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return 48 + (key - Qt::Key_0);
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12)
        return 112 + (key - Qt::Key_F1);
    switch (key)
    {
    case Qt::Key_Backspace: return 8;
    case Qt::Key_Tab: return 9;
    case Qt::Key_Return:
    case Qt::Key_Enter: return 13;
    case Qt::Key_Shift: return 16;
    case Qt::Key_Control: return 17;
    case Qt::Key_Alt: return 18;
    case Qt::Key_Escape: return 27;
    case Qt::Key_Space: return 32;
    case Qt::Key_PageUp: return 33;
    case Qt::Key_PageDown: return 34;
    case Qt::Key_End: return 35;
    case Qt::Key_Home: return 36;
    case Qt::Key_Left: return 37;
    case Qt::Key_Up: return 38;
    case Qt::Key_Right: return 39;
    case Qt::Key_Down: return 40;
    case Qt::Key_Insert: return 45;
    case Qt::Key_Delete: return 46;
    default: return 0;
    }
}

} // namespace

TestMovieWindow::TestMovieWindow(const fla::FLADocument& document, const QString& name,
    std::function<void(const QString&)> print, std::function<void(const QString&, const fla::DOMElement*, int)> printError,
    QWidget* parent)
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
    _pause = new QPushButton("Pause", this);
    _pause->setToolTip("Hold the movie where it is, or play it on");
    _pause->setAutoDefault(false);
    connect(_pause, &QPushButton::clicked, this, [this]() { setPaused(!_paused); });
    bar->addWidget(_pause);
    _step = new QPushButton("Step", this);
    _step->setToolTip("Play one frame of the paused movie");
    _step->setAutoDefault(false);
    _step->setEnabled(false);
    connect(_step, &QPushButton::clicked, this, &TestMovieWindow::tick);
    bar->addWidget(_step);
    // The buttons never keep the keys from the movie.
    for (QPushButton* button : {restartButton, _pause, _step})
        button->setFocusPolicy(Qt::NoFocus);
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
        _movie->scriptError = [this, printError](const fla::play::MovieRuntime::ScriptError& error) {
            ++_errors;
            if (printError)
                printError(QString::fromStdString(error.message), error.source, error.line);
            showFrame();
        };
        _movie->getURL = [](const std::string& url, const std::string&) {
            // Only web links leave the player, in the browser.
            const QUrl link(QString::fromStdString(url));
            if (link.scheme() == "http" || link.scheme() == "https")
                QDesktopServices::openUrl(link);
        };
        _movie->changed = [this]() { _view->update(); };

        // What scripts reach by linkage: movie clips (attachMovie, new
        // Hat()), and sounds, played from the library.
        std::vector<const fla::Symbol*> symbols;
        if (data.symbolList)
            symbols.assign(data.symbolList->symbols.begin(), data.symbolList->symbols.end());
        std::vector<std::string> soundIdentifiers;
        std::vector<std::string> soundClasses;
        for (const std::string& item : data.extraMedia)
        {
            if (item.find("<DOMSoundItem") == std::string::npos)
                continue;
            tinyxml2::XMLDocument xml;
            if (xml.Parse(item.c_str(), item.size()) != tinyxml2::XML_SUCCESS || !xml.FirstChildElement())
                continue;
            const tinyxml2::XMLElement* sound = xml.FirstChildElement();
            const char* name = sound->Attribute("name");
            if (!name)
                continue;
            _soundNames[name] = name;
            if (const char* identifier = sound->Attribute("linkageIdentifier"))
            {
                soundIdentifiers.push_back(identifier);
                _soundNames[identifier] = name;
            }
            if (const char* className = sound->Attribute("linkageClassName"))
            {
                soundClasses.push_back(className);
                _soundNames[className] = name;
            }
        }
        _movie->setLibrary(symbols, soundIdentifiers, soundClasses);
        _audio = new TimelineAudio(_player, this);
        _audio->setDocument(&document);
        _movie->playSound = [this](const std::string& linkage, double offset, int loops, double volume) {
            const auto name = _soundNames.find(linkage);
            if (name == _soundNames.end())
                return -1;
            std::shared_ptr<const PcmBuffer> buffer = _audio->librarySound(name->second);
            if (!buffer)
                return -1;
            const size_t start = static_cast<size_t>(std::max(0.0, offset) * 44100.0);
            const int voice = AudioEngine::instance().play(buffer, start, SIZE_MAX, std::max(1, loops),
                static_cast<float>(std::clamp(volume, 0.0, 4.0)));
            if (voice >= 0)
                _voices.push_back(voice);
            return voice;
        };
        _movie->stopSound = [](int channel) { AudioEngine::instance().stop(channel); };
        // The sounds on keyframes, as the frames are entered: an event
        // sound each time, a start sound unless it is still playing, a stop
        // sound silencing it; a stream from the keyframe on.
        _movie->frameSound = [this](const fla::Frame& frame) {
            const fla::frame_sound::Settings sound = fla::frame_sound::read(frame);
            if (sound.name.empty())
                return;
            std::vector<int>& voices = _frameVoices[sound.name];
            voices.erase(std::remove_if(voices.begin(), voices.end(),
                             [](int voice) { return !AudioEngine::instance().isPlaying(voice); }),
                voices.end());
            if (sound.sync == "stop")
            {
                for (const int voice : voices)
                    AudioEngine::instance().stop(voice);
                voices.clear();
                return;
            }
            if (sound.sync == "start" && !voices.empty())
                return;
            std::shared_ptr<const PcmBuffer> buffer = _audio->librarySound(sound.name);
            if (!buffer)
                return;
            const int voice = AudioEngine::instance().play(buffer, 0, SIZE_MAX, sound.loop ? 0 : std::max(1, sound.loops));
            if (voice >= 0)
            {
                voices.push_back(voice);
                _voices.push_back(voice);
            }
        };
        _movie->stopAllSounds = [this]() { stopSounds(); };
        _view->setMovie(_movie.get());
    }

    _timer = new QTimer(this);
    _timer->setTimerType(Qt::PreciseTimer);
    _timer->setInterval(std::max(1, static_cast<int>(std::lround(1000.0 / (data.frameRate > 0.0 ? data.frameRate : 24.0)))));
    connect(_timer, &QTimer::timeout, this, &TestMovieWindow::tick);

    // The stage at its own size, as far as the screen allows.
    resize(std::clamp(data.width + 40, 320, 1600), std::clamp(data.height + 80, 240, 1000));
    _view->installEventFilter(this);
    installEventFilter(this);
    _view->setFocus();
    restart();
}

bool TestMovieWindow::eventFilter(QObject* watched, QEvent* event)
{
    // Keys go to the movie; Esc still closes the window.
    if (_movie && (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease))
    {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape)
            return QDialog::eventFilter(watched, event);
        const int code = flashKeyCode(key->key());
        const QString text = key->text();
        const int character = text.size() == 1 && text.at(0).unicode() >= 32 ? text.at(0).unicode() : 0;
        if (code != 0 || character != 0)
        {
            if (event->type() == QEvent::KeyPress)
                _movie->keyDown(code, character);
            else if (!key->isAutoRepeat())
                _movie->keyUp(code, character);
            showFrame();
        }
        return true;
    }
    return QDialog::eventFilter(watched, event);
}

void TestMovieWindow::setPaused(bool paused)
{
    _paused = paused;
    _pause->setText(paused ? "Play" : "Pause");
    _step->setEnabled(paused);
    if (paused)
        _timer->stop();
    else if (_movie)
        _timer->start();
    showFrame();
}

TestMovieWindow::~TestMovieWindow()
{
    stopSounds();
    if (_view)
        _view->setMovie(nullptr);
}

void TestMovieWindow::stopSounds()
{
    for (const int voice : _voices)
        AudioEngine::instance().stop(voice);
    _voices.clear();
    _frameVoices.clear();
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
    if (!_paused)
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
    if (_paused)
        status += ", paused";
    if (_errors > 0)
        status += QString(", %1 script %2 (see the Output panel)").arg(_errors).arg(_errors == 1 ? "error" : "errors");
    _status->setText(status);
    _view->update();
}
