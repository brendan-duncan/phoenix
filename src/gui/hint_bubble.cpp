#include "hint_bubble.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

HintBubble::HintBubble(QWidget* stage)
    : QFrame(stage)
    , _stage(stage)
{
    setObjectName("hintBubble");
    setFrameShape(QFrame::StyledPanel);
    setAutoFillBackground(true);
    // A tooltip's colors: it reads as a note, not as part of the art.
    setBackgroundRole(QPalette::ToolTipBase);
    setForegroundRole(QPalette::ToolTipText);

    QLabel* tip = new QLabel("Tip", this);
    QFont bold = tip->font();
    bold.setBold(true);
    tip->setFont(bold);
    tip->setForegroundRole(QPalette::ToolTipText);
    _text = new QLabel(this);
    _text->setObjectName("hintText");
    _text->setWordWrap(true);
    _text->setForegroundRole(QPalette::ToolTipText);
    _text->setMinimumWidth(260);
    _text->setMaximumWidth(360);

    QPushButton* off = new QPushButton("Don't Show Hints", this);
    off->setFlat(true);
    off->setCursor(Qt::PointingHandCursor);
    off->setToolTip("Turn the hints off; Help > Reset Hints shows them again");
    QPushButton* gotIt = new QPushButton("Got It", this);
    gotIt->setObjectName("hintGotIt");
    connect(gotIt, &QPushButton::clicked, this, &QWidget::hide);
    connect(off, &QPushButton::clicked, this, [this]() {
        hide();
        emit hintsTurnedOff();
    });

    QHBoxLayout* buttons = new QHBoxLayout();
    buttons->addWidget(off);
    buttons->addStretch();
    buttons->addWidget(gotIt);
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 6);
    layout->addWidget(tip);
    layout->addWidget(_text);
    layout->addLayout(buttons);

    // Gone by itself after a while, so it never stands in the way for long.
    _timer = new QTimer(this);
    _timer->setSingleShot(true);
    _timer->setInterval(20000);
    connect(_timer, &QTimer::timeout, this, &QWidget::hide);

    stage->installEventFilter(this);
    hide();
}

void HintBubble::showHint(const QString& text)
{
    _text->setText(text);
    adjustSize();
    place();
    show();
    raise();
    _timer->start();
}

QString HintBubble::text() const
{
    return _text->text();
}

void HintBubble::place()
{
    const int margin = 12;
    move(margin, std::max(margin, _stage->height() - height() - margin));
}

bool HintBubble::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == _stage && event->type() == QEvent::Resize && isVisible())
        place();
    return QFrame::eventFilter(watched, event);
}
