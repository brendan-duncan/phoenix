#pragma once

#include <QDialog>

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
    /// line (0 when not known).
    TestMovieWindow(const fla::FLADocument& document, const QString& name, std::function<void(const QString&)> print,
        std::function<void(const QString&, const fla::DOMElement*, int)> printError, QWidget* parent = nullptr);
    ~TestMovieWindow() override;

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void restart();
    void setPaused(bool paused);
    void stopSounds();
    void tick();
    void showFrame();

    PhoenixView* _view = nullptr;
    Player* _player = nullptr;
    QTimer* _timer = nullptr;
    QLabel* _status = nullptr;
    QPushButton* _pause = nullptr;
    QPushButton* _step = nullptr;
    bool _paused = false;
    /// The movie's sounds: decoded from the library, by linkage, and the
    /// voices playing.
    TimelineAudio* _audio = nullptr;
    std::map<std::string, std::string> _soundNames;
    std::vector<int> _voices;
    std::unique_ptr<fla::play::MovieRuntime> _movie;
    int _errors = 0;
};
