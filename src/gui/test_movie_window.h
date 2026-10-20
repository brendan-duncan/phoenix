#pragma once

#include <QDialog>
#include <QElapsedTimer>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class PhoenixView;
class Player;
class TimelineAudio;
class QLabel;
class QPushButton;
class QTimer;

namespace fla {
class DOMElement;
class FLADocument;
namespace play {
class MovieRuntime;
}
}

/// Control > Test Movie: the scene played as Flash Player plays it, at its
/// frame rate, its scripts run and its buttons answering the mouse.
/// Keys typed go to the movie. Restart plays it again from the first frame;
/// Pause holds it, and Step moves a paused movie on one frame. While it is
/// open the document cannot be edited, so it stays as the movie found it.
class TestMovieWindow : public QDialog
{
    Q_OBJECT

public:
    /// \a name is the document's, for the title; \a print and \a printError
    /// get what the scripts trace and the errors they stop on, with the
    /// script an error is in (a keyframe or an instance, or null) and its
    /// line (0 when not known). ActionScript 3 classes (the document
    /// class, symbols' classes, the rest) are looked for beside \a file, the
    /// document's path, and on its class path.
    TestMovieWindow(const fla::FLADocument& document, const QString& name, std::function<void(const QString&)> print,
        std::function<void(const QString&, const fla::DOMElement*, int)> printError, QWidget* parent = nullptr,
        const QString& file = QString());
    ~TestMovieWindow() override;

    /// Plays the movie as a panel: the movie alone, without the playing
    /// controls, its MMExecute calls given to \a execute (JSFL, and its
    /// value back as text).
    void setPanel(std::function<std::string(const std::string& code)> execute);

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void restart();
    void setPaused(bool paused);
    void stopSounds();
    /// The movie's sounds held where they are, or played on.
    void holdSounds(bool hold);
    void tick();
    /// Steps on to the frame the clock says is due, entering each frame on
    /// the way so its scripts and sounds run, and draws once.
    void advance();
    /// Counts time from now (playing started, resumed, or the movie sent
    /// somewhere else).
    void startClock();
    void showFrame();

    PhoenixView* _view = nullptr;
    Player* _player = nullptr;
    QTimer* _timer = nullptr;
    /// Playing goes by the clock, as sound does: frames are due by the time
    /// gone since it started, not by how many ticks there were.
    QElapsedTimer _clock;
    qint64 _stepsDone = 0;
    /// The display engine's generation when the movie was last drawn: a
    /// frame that changed nothing is not drawn again.
    uint64_t _shownGeneration = 0;
    uint64_t _shownScriptRuns = 0;
    double _frameRate = 24.0;
    QLabel* _status = nullptr;
    /// The playing controls under the movie, hidden for a panel.
    QWidget* _controls = nullptr;
    QPushButton* _pause = nullptr;
    QPushButton* _step = nullptr;
    class QSlider* _slider = nullptr;
    bool _seeking = false;
    bool _paused = false;
    /// The movie's sounds: decoded from the library, by linkage, and the
    /// voices playing.
    TimelineAudio* _audio = nullptr;
    std::map<std::string, std::string> _soundNames;
    std::vector<int> _voices;
    /// The frames' sounds playing, by name, for start and stop syncs.
    std::map<std::string, std::vector<int>> _frameVoices;
    std::unique_ptr<fla::play::MovieRuntime> _movie;
    int _errors = 0;
};
