// What every platform's web view shares: the page's own dialogs, and the
// note painted where no page can show. The engines are in web_view_win.cpp
// (WebView2) and web_view_mac.mm (WKWebView); without either, the stand-in
// at the end of this file only says why.

#include "web_view.h"

#include <QInputDialog>
#include <QMessageBox>
#include <QPainter>

void WebView::answerDialog(const QString& kind, const QString& message, const QString& defaultText, bool& accepted,
    QString& answer)
{
    accepted = true;
    answer.clear();
    const QString title = window() ? window()->windowTitle() : QString("Phoenix");
    if (kind == "alert")
        QMessageBox::information(this, title, message);
    else if (kind == "confirm")
        accepted = QMessageBox::question(this, title, message, QMessageBox::Ok | QMessageBox::Cancel) == QMessageBox::Ok;
    else
    {
        bool ok = false;
        answer = QInputDialog::getText(this, title, message, QLineEdit::Normal, defaultText, &ok);
        accepted = ok;
    }
}

void WebView::paintEvent(QPaintEvent*)
{
    if (_unavailable.isEmpty())
        return;
    QPainter painter(this);
    painter.fillRect(rect(), palette().window());
    painter.setPen(palette().color(QPalette::PlaceholderText));
    painter.drawText(rect().adjusted(16, 16, -16, -16), Qt::AlignCenter | Qt::TextWordWrap, _unavailable);
}

#ifndef PHOENIX_WEBVIEW

struct WebView::Impl
{
};

WebView::WebView(QWidget* parent)
    : QWidget(parent)
{
    available(&_unavailable);
}

WebView::~WebView() = default;

bool WebView::available(QString* why)
{
    if (why)
        *why = "HTML panels need a web view, which this Phoenix was built without (they show on Windows and macOS).";
    return false;
}

void WebView::setStartScript(const QString&) {}
void WebView::setProfileFolder(const QString&) {}
void WebView::load(const QString&, const QString&, const QString&) {}
void WebView::runScript(const QString&) {}
void WebView::started(void*) {}
bool WebView::event(QEvent* event) { return QWidget::event(event); }
void WebView::resizeEvent(QResizeEvent* event) { QWidget::resizeEvent(event); }
void WebView::showEvent(QShowEvent* event) { QWidget::showEvent(event); }
void WebView::hideEvent(QHideEvent* event) { QWidget::hideEvent(event); }

#endif
