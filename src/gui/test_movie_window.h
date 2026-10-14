#pragma once

#include <QDialog>

#include <functional>
#include <memory>

class PhoenixView;
class Player;
class QLabel;
class QTimer;

namespace fla {
class FLADocument;
namespace play {
class MovieRuntime;
}
}

/// Control > Test Movie: the scene played as Flash Player plays it, at its
/// frame rate, its scripts run and its buttons answering the mouse.
/// Restart plays it again from the first frame. While it is open the
/// document cannot be edited, so it stays as the movie found it.
class TestMovieWindow : public QDialog
{
    Q_OBJECT

public:
    /// \a name is the document's, for the title; \a print and \a printError
    /// get what the scripts trace and the errors they stop on.
    TestMovieWindow(const fla::FLADocument& document, const QString& name, std::function<void(const QString&)> print,
        std::function<void(const QString&)> printError, QWidget* parent = nullptr);
    ~TestMovieWindow() override;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void restart();
    void tick();
    void showFrame();

    PhoenixView* _view = nullptr;
    Player* _player = nullptr;
    QTimer* _timer = nullptr;
    QLabel* _status = nullptr;
    std::unique_ptr<fla::play::MovieRuntime> _movie;
    int _errors = 0;
};
