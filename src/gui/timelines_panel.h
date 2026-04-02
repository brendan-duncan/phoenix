#pragma once

#include "../data/fla_document.h"

#include <QWidget>
#include <QScrollArea>
#include <QPushButton>
#include <QTimer>

class Player;

class TimelineGrid : public QWidget
{
    Q_OBJECT
public:
    TimelineGrid(Player* player, QWidget* parent = nullptr);

    void setDocument(const fla::FLADocument* document)
    {
        _flaDocument = document;
    }

    void setPlayer(Player* player) { _player = player; }

    void updateSize();

public slots:
    void onPlayerFrameChanged(int frame) { update(); }

protected:
    void paintEvent(QPaintEvent* event) override;

    void mousePressEvent(QMouseEvent* event) override;

    void mouseMoveEvent(QMouseEvent* event) override;

    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    const fla::FLADocument* _flaDocument;
    Player* _player;
    bool _isDragging = false;

    void updatePlayerFromMouse(int x);
};

class TimelinesPanel : public QWidget
{
    Q_OBJECT
public:
    explicit TimelinesPanel(Player* player, QWidget *parent = nullptr);

    void setDocument(const fla::FLADocument* document);

private slots:
    void onGotoFirstFrame();

    void onStepBack();

    void onPlayStop();

    void onStepForward();

    void onTimerTick();

    void onPlayerFrameChanged(int frame);

private:
    const fla::FLADocument* _flaDocument;
    QScrollArea* _scrollArea;
    QWidget* _header;
    TimelineGrid* _gridContainer;
    Player* _player;
    QPushButton* _playButton;
    QTimer* _playTimer;
    bool _isPlaying = false;
};
