#include "player.h"

Player::Player(QObject *parent)
    : QObject(parent)
{
}

void Player::setCurrentFrame(int frame)
{
    if (_currentFrame != frame && frame >= 0)
    {
        _currentFrame = frame;
        emit currentFrameChanged(frame);
    }
}

