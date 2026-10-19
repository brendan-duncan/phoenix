#include "output_panel.h"
#include "file_pick.h"

#include <QFile>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextStream>
#include <QVBoxLayout>

namespace {

/// What double-clicking an error line does.
struct OpenAction : QTextBlockUserData
{
    std::function<void()> open;
};

} // namespace

OutputPanel::OutputPanel(QWidget* parent)
    : QWidget(parent)
    , _text(new QPlainTextEdit(this))
{
    _text->setReadOnly(true);
    _text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    _text->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    // Long runs of output stay usable.
    _text->setMaximumBlockCount(100000);
    _text->viewport()->installEventFilter(this);

    QPushButton* clearButton = new QPushButton("Clear", this);
    clearButton->setToolTip("Empty the Output panel");
    connect(clearButton, &QPushButton::clicked, this, &OutputPanel::clear);
    QPushButton* saveButton = new QPushButton("Save...", this);
    saveButton->setToolTip("Write what the panel shows to a text file");
    connect(saveButton, &QPushButton::clicked, this, [this]() {
        file_pick::save(this, "Save Output", "output.txt", "Text files (*.txt)", [this](const QString& path) {
            QFile file(path);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text))
                file.write(text().toUtf8());
        });
    });

    QHBoxLayout* buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->addStretch();
    buttons->addWidget(saveButton);
    buttons->addWidget(clearButton);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    layout->addWidget(_text, 1);
    layout->addLayout(buttons);
}

void OutputPanel::append(const QString& text)
{
    _text->appendPlainText(text);
    _text->verticalScrollBar()->setValue(_text->verticalScrollBar()->maximum());
    emit written();
}

bool OutputPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == _text->viewport() && event->type() == QEvent::MouseButtonDblClick)
    {
        const QTextBlock block = _text->cursorForPosition(static_cast<QMouseEvent*>(event)->position().toPoint()).block();
        if (auto* action = dynamic_cast<OpenAction*>(block.userData()); action && action->open)
        {
            action->open();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void OutputPanel::appendError(const QString& text, std::function<void()> open)
{
    // A colored line, so an error stands out from what the script traced.
    QTextCharFormat format;
    format.setForeground(QColor(224, 80, 80));
    QTextCursor cursor(_text->document());
    cursor.movePosition(QTextCursor::End);
    if (!_text->document()->isEmpty())
        cursor.insertBlock();
    cursor.insertText(text, format);
    if (open)
    {
        auto* action = new OpenAction;
        action->open = std::move(open);
        cursor.block().setUserData(action);
    }
    _text->verticalScrollBar()->setValue(_text->verticalScrollBar()->maximum());
    emit written();
}

void OutputPanel::clear()
{
    _text->clear();
}

QString OutputPanel::text() const
{
    return _text->toPlainText();
}
