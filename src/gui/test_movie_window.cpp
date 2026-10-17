#include "test_movie_window.h"

#include "audio_engine.h"
#include "movie_hooks.h"
#include "phoenix_view.h"
#include "player.h"
#include "timeline_audio.h"

#include "../data/bitmap.h"
#include "../data/document.h"
#include "../data/fla_document.h"
#include "../data/symbol.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"
#include "../edit/frame_sound.h"
#include "../play/movie_runtime.h"
#include "../play/display/backend.h"
#include "../third_party/tinyxml2/tinyxml2.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

/// A key as Flash numbers it (Key.LEFT is 37, "A" 65), or 0 for one it
/// does not. \a keypad: on the number pad (Keyboard.NUMPAD_1 is 97).
int flashKeyCode(int key, bool keypad = false)
{
    if (keypad)
    {
        if (key >= Qt::Key_0 && key <= Qt::Key_9)
            return 96 + (key - Qt::Key_0);
        switch (key)
        {
        case Qt::Key_Asterisk: return 106;
        case Qt::Key_Plus: return 107;
        case Qt::Key_Minus: return 109;
        case Qt::Key_Period: return 110;
        case Qt::Key_Slash: return 111;
        default: break;
        }
    }
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return 65 + (key - Qt::Key_A);
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return 48 + (key - Qt::Key_0);
    if (key >= Qt::Key_F1 && key <= Qt::Key_F15)
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
    case Qt::Key_CapsLock: return 20;
    case Qt::Key_Semicolon: return 186;
    case Qt::Key_Equal: return 187;
    case Qt::Key_Comma: return 188;
    case Qt::Key_Minus: return 189;
    case Qt::Key_Period: return 190;
    case Qt::Key_Slash: return 191;
    case Qt::Key_QuoteLeft: return 192;
    case Qt::Key_BracketLeft: return 219;
    case Qt::Key_Backslash: return 220;
    case Qt::Key_BracketRight: return 221;
    case Qt::Key_Apostrophe: return 222;
    default: return 0;
    }
}

/// The player's buttons, drawn in the text color: restart (a bar and a
/// triangle back), play, pause, and step (a triangle and a bar on).
enum class Glyph
{
    Restart,
    Play,
    Pause,
    Step
};

/// A click on the time slider goes straight to where it lands.
class JumpingSliderStyle : public QProxyStyle
{
public:
    using QProxyStyle::QProxyStyle;
    int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
        QStyleHintReturn* returnData) const override
    {
        if (hint == QStyle::SH_Slider_AbsoluteSetButtons)
            return Qt::LeftButton;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

QIcon glyph(Glyph which, const QColor& color)
{
    QIcon icon;
    for (int size : {16, 24, 32})
    {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.scale(size / 16.0, size / 16.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        const auto triangle = [&painter](double left, double right, bool pointsRight) {
            QPainterPath path;
            if (pointsRight)
            {
                path.moveTo(left, 3.0);
                path.lineTo(right, 8.0);
                path.lineTo(left, 13.0);
            }
            else
            {
                path.moveTo(right, 3.0);
                path.lineTo(left, 8.0);
                path.lineTo(right, 13.0);
            }
            path.closeSubpath();
            painter.drawPath(path);
        };
        switch (which)
        {
        case Glyph::Restart:
            painter.drawRect(QRectF(3.0, 3.0, 2.0, 10.0));
            triangle(5.5, 13.0, false);
            break;
        case Glyph::Play:
            triangle(4.5, 12.5, true);
            break;
        case Glyph::Pause:
            painter.drawRect(QRectF(4.0, 3.0, 3.0, 10.0));
            painter.drawRect(QRectF(9.0, 3.0, 3.0, 10.0));
            break;
        case Glyph::Step:
            triangle(3.0, 10.5, true);
            painter.drawRect(QRectF(11.0, 3.0, 2.0, 10.0));
            break;
        }
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

} // namespace

TestMovieWindow::TestMovieWindow(const fla::FLADocument& document, const QString& name,
    std::function<void(const QString&)> print, std::function<void(const QString&, const fla::DOMElement*, int)> printError,
    QWidget* parent, const QString& file)
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
    // The movie's frame, as a player shows it: nothing off the stage.
    _view->setShowPasteboard(false);
    _view->setClipOutsideStage(true);
    layout->addWidget(_view, 1);

    QHBoxLayout* bar = new QHBoxLayout;
    bar->setContentsMargins(8, 4, 8, 4);
    const QColor ink = palette().color(QPalette::ButtonText);
    QPushButton* restartButton = new QPushButton(this);
    restartButton->setIcon(glyph(Glyph::Restart, ink));
    restartButton->setToolTip("Play the movie again from its first frame");
    restartButton->setAutoDefault(false);
    connect(restartButton, &QPushButton::clicked, this, &TestMovieWindow::restart);
    bar->addWidget(restartButton);
    _pause = new QPushButton(this);
    _pause->setIcon(glyph(Glyph::Pause, ink));
    _pause->setToolTip("Hold the movie where it is, or play it on");
    _pause->setAutoDefault(false);
    connect(_pause, &QPushButton::clicked, this, [this]() { setPaused(!_paused); });
    bar->addWidget(_pause);
    _step = new QPushButton(this);
    _step->setIcon(glyph(Glyph::Step, ink));
    _step->setToolTip("Play one frame of the paused movie");
    _step->setAutoDefault(false);
    _step->setEnabled(false);
    connect(_step, &QPushButton::clicked, this, [this]() {
        // A frame on: the sounds play on with it for a frame's time, then
        // hold again.
        holdSounds(false);
        tick();
        QTimer::singleShot(static_cast<int>(1000.0 / std::max(1.0, _frameRate)), this, [this]() {
            if (_paused)
                holdSounds(true);
        });
    });
    bar->addWidget(_step);
    // The buttons never keep the keys from the movie.
    for (QPushButton* button : {restartButton, _pause, _step})
    {
        button->setFocusPolicy(Qt::NoFocus);
        button->setFlat(true);
        button->setIconSize(QSize(16, 16));
        button->setFixedSize(30, 26);
    }
    // The time slider: where the scene is, and dragging it seeks.
    _slider = new QSlider(Qt::Horizontal, this);
    _slider->setFocusPolicy(Qt::NoFocus);
    // No base given: the proxy then uses the application's style without
    // owning it (given one, it would delete it with the slider).
    auto* sliderStyle = new JumpingSliderStyle();
    sliderStyle->setParent(_slider);
    _slider->setStyle(sliderStyle);
    _slider->setToolTip("Drag to go to another frame of the scene");
    connect(_slider, &QSlider::valueChanged, this, [this](int frame) {
        if (_seeking || !_movie)
            return;
        _movie->seek(frame);
        startClock();
        showFrame();
    });
    bar->addWidget(_slider, 1);
    _status = new QLabel(this);
    _status->setForegroundRole(QPalette::PlaceholderText);
    bar->addWidget(_status);
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
        std::vector<const fla::Bitmap*> bitmaps;
        for (const fla::Resource* resource : data.resources)
            if (resource && resource->resourceType() == fla::Resource::Type::Bitmap)
                bitmaps.push_back(static_cast<const fla::Bitmap*>(resource));
        _movie->setLibraryBitmaps(bitmaps);
        // Its classes, images, the web and SharedObjects; what it sends to
        // the web is told in the Output panel.
        connectMovie(*_movie, document, file, this, print);
        // A script that sets stage.frameRate sets the window's pace.
        _movie->frameRateChanged = [this](double rate) {
            _frameRate = rate;
            if (_timer)
                _timer->setInterval(std::max(1, static_cast<int>(1000.0 / _frameRate / 2.0)));
            startClock();
        };
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

    // Wakes twice a frame; which frame is due comes from the clock, so the
    // picture keeps time with the sound however long drawing takes.
    _frameRate = data.frameRate > 0.0 ? data.frameRate : 24.0;
    _timer = new QTimer(this);
    _timer->setTimerType(Qt::PreciseTimer);
    _timer->setInterval(std::max(1, static_cast<int>(1000.0 / _frameRate / 2.0)));
    connect(_timer, &QTimer::timeout, this, &TestMovieWindow::advance);

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
        const int code = flashKeyCode(key->key(), (key->modifiers() & Qt::KeypadModifier) != 0);
        const QString text = key->text();
        const int character = text.size() == 1 && text.at(0).unicode() >= 32 ? text.at(0).unicode() : 0;
        if (code != 0 || character != 0)
        {
            const Qt::KeyboardModifiers held = key->modifiers();
            const bool shift = held & Qt::ShiftModifier;
            const bool ctrl = held & Qt::ControlModifier;
            const bool alt = held & Qt::AltModifier;
            if (event->type() == QEvent::KeyPress)
                _movie->keyDown(code, character, shift, ctrl, alt);
            else if (!key->isAutoRepeat())
                _movie->keyUp(code, character, shift, ctrl, alt);
            showFrame();
        }
        return true;
    }
    return QDialog::eventFilter(watched, event);
}

void TestMovieWindow::setPaused(bool paused)
{
    _paused = paused;
    _pause->setIcon(glyph(paused ? Glyph::Play : Glyph::Pause, palette().color(QPalette::ButtonText)));
    _pause->setToolTip(paused ? "Play the movie on" : "Hold the movie where it is");
    _step->setEnabled(paused);
    holdSounds(paused);
    if (paused)
        _timer->stop();
    else if (_movie)
    {
        startClock();
        _timer->start();
    }
    showFrame();
}

TestMovieWindow::~TestMovieWindow()
{
    stopSounds();
    if (_view)
        _view->setMovie(nullptr);
}

void TestMovieWindow::holdSounds(bool hold)
{
    for (const int voice : _voices)
        AudioEngine::instance().setPaused(voice, hold);
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
    startClock();
    if (!_paused)
        _timer->start();
}

void TestMovieWindow::startClock()
{
    _clock.start();
    _stepsDone = 0;
}

void TestMovieWindow::advance()
{
    if (!_movie || _paused)
        return;
    if (!_clock.isValid())
        startClock();
    const qint64 due = static_cast<qint64>(static_cast<double>(_clock.elapsed()) * _frameRate / 1000.0);
    if (due <= _stepsDone)
        return;
    // Every frame due is entered, so its scripts run and its sounds start;
    // far behind (drawing much slower than the frame rate), the clock is
    // let go of rather than chased.
    constexpr qint64 kMostSteps = 8;
    const qint64 steps = std::min(due - _stepsDone, kMostSteps);
    for (qint64 i = 0; i < steps; ++i)
        _movie->step();
    _stepsDone += steps;
    if (due - _stepsDone > kMostSteps)
        _stepsDone = due;
    showFrame();
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
    if (_slider && !_slider->isSliderDown())
    {
        _seeking = true;
        _slider->setRange(0, std::max(0, root.length - 1));
        _slider->setValue(root.frame);
        _slider->setEnabled(root.length > 1);
        _seeking = false;
    }
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
    // Only when something shown changed, or a script ran that may have: a
    // movie stopped on a frame with nothing moving is not drawn over and
    // over.
    const uint64_t generation = fla::display::displayGeneration();
    if (generation != _shownGeneration || _movie->scriptRuns() != _shownScriptRuns)
    {
        _shownGeneration = generation;
        _shownScriptRuns = _movie->scriptRuns();
        _view->update();
    }
}
