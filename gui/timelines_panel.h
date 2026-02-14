#pragma once

#include "../data/fla_document.h"

#include <QWidget>
#include <QScrollArea>

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

private:
    const fla::FLADocument* _flaDocument;
    QScrollArea* _scrollArea;
    TimelineGrid* _gridContainer;
    Player* _player;

    void refreshTimelines();
};
