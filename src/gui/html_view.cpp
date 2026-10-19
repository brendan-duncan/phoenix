// What every platform's web view shares: the page's own dialogs, and the
// note painted where no page can show. The engines are in html_view_win.cpp
// (WebView2) and html_view_mac.mm (WKWebView); without either, the stand-in
// at the end of this file only says why.

#include "html_view.h"
#include "modal.h"

#include <QInputDialog>
#include <QMessageBox>
#include <QPainter>

void HtmlView::answerDialog(const QString& kind, const QString& message, const QString& defaultText, bool& accepted,
    QString& answer)
{
    accepted = true;
    answer.clear();
    const QString title = window() ? window()->windowTitle() : QString("Phoenix");
    if (kind == "alert")
        modal::information(this, title, message);
    // The page waits for its answer; where nothing can wait, it is canceled.
    else if (modal::cannotWait())
        accepted = false;
    else if (kind == "confirm")
        accepted = QMessageBox::question(this, title, message, QMessageBox::Ok | QMessageBox::Cancel) == QMessageBox::Ok;
    else
    {
        bool ok = false;
        answer = QInputDialog::getText(this, title, message, QLineEdit::Normal, defaultText, &ok);
        accepted = ok;
    }
}

void HtmlView::paintEvent(QPaintEvent*)
{
    if (_unavailable.isEmpty())
        return;
    QPainter painter(this);
    painter.fillRect(rect(), palette().window());
    painter.setPen(palette().color(QPalette::PlaceholderText));
    painter.drawText(rect().adjusted(16, 16, -16, -16), Qt::AlignCenter | Qt::TextWordWrap, _unavailable);
}

#ifndef PHOENIX_WEBVIEW

struct HtmlView::Impl
{
};

HtmlView::HtmlView(QWidget* parent)
    : QWidget(parent)
{
    available(&_unavailable);
}

HtmlView::~HtmlView() = default;

bool HtmlView::available(QString* why)
{
    if (why)
        *why = "HTML panels need a web view, which this Phoenix was built without (they show on Windows and macOS).";
    return false;
}

void HtmlView::setStartScript(const QString&) {}
void HtmlView::setProfileFolder(const QString&) {}
void HtmlView::load(const QString&, const QString&, const QString&) {}
void HtmlView::runScript(const QString&) {}
void HtmlView::started(void*) {}
bool HtmlView::event(QEvent* event) { return QWidget::event(event); }
void HtmlView::resizeEvent(QResizeEvent* event) { QWidget::resizeEvent(event); }
void HtmlView::showEvent(QShowEvent* event) { QWidget::showEvent(event); }
void HtmlView::hideEvent(QHideEvent* event) { QWidget::hideEvent(event); }

#endif
