// The web view on macOS: WKWebView, from the system's WebKit, in the
// widget's own NSView.

#include "html_view.h"

#include <QDesktopServices>
#include <QEvent>
#include <QPointer>
#include <QTimer>
#include <QUrl>

#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>

struct HtmlViewBridge
{
    static void answer(HtmlView& view, const QString& kind, const QString& message, const QString& defaultText,
        bool& accepted, QString& text)
    {
        view.answerDialog(kind, message, defaultText, accepted, text);
    }
};

static NSString* toNS(const QString& text)
{
    return text.toNSString();
}

@interface PhoenixWebDelegate : NSObject <WKUIDelegate, WKNavigationDelegate>
@property (nonatomic, assign) QPointer<HtmlView>* owner;
@end

@implementation PhoenixWebDelegate

- (void)webView:(WKWebView*)webView
    runJavaScriptAlertPanelWithMessage:(NSString*)message
                      initiatedByFrame:(WKFrameInfo*)frame
                     completionHandler:(void (^)(void))completionHandler
{
    QPointer<HtmlView> view = *self.owner;
    if (!view)
    {
        completionHandler();
        return;
    }
    const QString text = QString::fromNSString(message);
    void (^done)(void) = [completionHandler copy];
    QTimer::singleShot(0, view, [view, text, done]() {
        bool accepted = true;
        QString answer;
        HtmlViewBridge::answer(*view, "alert", text, QString(), accepted, answer);
        done();
    });
}

- (void)webView:(WKWebView*)webView
    runJavaScriptConfirmPanelWithMessage:(NSString*)message
                        initiatedByFrame:(WKFrameInfo*)frame
                       completionHandler:(void (^)(BOOL))completionHandler
{
    QPointer<HtmlView> view = *self.owner;
    const QString text = QString::fromNSString(message);
    void (^done)(BOOL) = [completionHandler copy];
    if (!view)
    {
        completionHandler(NO);
        return;
    }
    QTimer::singleShot(0, view, [view, text, done]() {
        bool accepted = true;
        QString answer;
        HtmlViewBridge::answer(*view, "confirm", text, QString(), accepted, answer);
        done(accepted ? YES : NO);
    });
}

- (void)webView:(WKWebView*)webView
    runJavaScriptTextInputPanelWithPrompt:(NSString*)prompt
                              defaultText:(NSString*)defaultText
                         initiatedByFrame:(WKFrameInfo*)frame
                        completionHandler:(void (^)(NSString*))completionHandler
{
    QPointer<HtmlView> view = *self.owner;
    if (!view)
    {
        completionHandler(nil);
        return;
    }
    const QString text = QString::fromNSString(prompt);
    const QString preset = defaultText ? QString::fromNSString(defaultText) : QString();
    void (^done)(NSString*) = [completionHandler copy];
    // The page waits for the answer, given once the window's events have
    // run, so what answers it is not inside WebKit's callback.
    QTimer::singleShot(0, view, [view, text, preset, done]() {
        const QString mark = QString::fromLatin1(HtmlView::kCallMark);
        if (text.startsWith(mark))
        {
            const QString answer = view->onCall ? view->onCall(text.mid(mark.size())) : QString();
            done(answer.toNSString());
            return;
        }
        bool accepted = true;
        QString answer;
        HtmlViewBridge::answer(*view, "prompt", text, preset, accepted, answer);
        done(accepted ? answer.toNSString() : nil);
    });
}

- (WKWebView*)webView:(WKWebView*)webView
    createWebViewWithConfiguration:(WKWebViewConfiguration*)configuration
               forNavigationAction:(WKNavigationAction*)navigationAction
                    windowFeatures:(WKWindowFeatures*)windowFeatures
{
    // Links that open a window go to the person's browser.
    const QUrl url = QUrl::fromNSURL(navigationAction.request.URL);
    if (url.scheme() == "http" || url.scheme() == "https" || url.scheme() == "mailto")
        QDesktopServices::openUrl(url);
    return nil;
}

- (void)webView:(WKWebView*)webView didFinishNavigation:(WKNavigation*)navigation
{
    if (QPointer<HtmlView> view = *self.owner)
    {
        emit view->loaded(true);
        emit view->titleChanged(QString::fromNSString(webView.title ?: @""));
    }
}

- (void)webView:(WKWebView*)webView didFailNavigation:(WKNavigation*)navigation withError:(NSError*)error
{
    if (QPointer<HtmlView> view = *self.owner)
        emit view->loaded(false);
}

- (void)webView:(WKWebView*)webView
    didFailProvisionalNavigation:(WKNavigation*)navigation
                       withError:(NSError*)error
{
    if (QPointer<HtmlView> view = *self.owner)
        emit view->loaded(false);
}

@end

struct HtmlView::Impl
{
    WKWebView* webview = nil;
    PhoenixWebDelegate* delegate = nil;
    QPointer<HtmlView> owner;
    QString startScript;
    QString folder, page;
    bool loadWanted = false;
};

HtmlView::HtmlView(QWidget* parent)
    : QWidget(parent)
    , _impl(std::make_unique<Impl>())
{
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_DontCreateNativeAncestors);
    setFocusPolicy(Qt::StrongFocus);
    _impl->owner = this;
}

HtmlView::~HtmlView()
{
    Impl& d = *_impl;
    if (d.webview)
    {
        d.webview.UIDelegate = nil;
        d.webview.navigationDelegate = nil;
        [d.webview removeFromSuperview];
    }
}

bool HtmlView::available(QString*)
{
    return true;
}

void HtmlView::setStartScript(const QString& script)
{
    _impl->startScript = script;
}

void HtmlView::setProfileFolder(const QString&)
{
    // WebKit keeps one store for the app.
}

void HtmlView::load(const QString& folder, const QString& page, const QString&)
{
    Impl& d = *_impl;
    d.folder = folder;
    d.page = page;
    d.loadWanted = true;
    if (!d.webview)
    {
        // The page starts now even with the widget hidden: a panel started
        // in the background runs out of sight.
        showEvent(nullptr);
        return;
    }
    NSURL* base = [NSURL fileURLWithPath:toNS(folder) isDirectory:YES];
    NSURL* file = [NSURL fileURLWithPath:toNS(QString(folder + "/" + page))];
    [d.webview loadFileURL:file allowingReadAccessToURL:base];
    d.loadWanted = false;
}

void HtmlView::runScript(const QString& script)
{
    if (_impl->webview)
        [_impl->webview evaluateJavaScript:toNS(script) completionHandler:nil];
}

bool HtmlView::event(QEvent* event)
{
    if (event->type() == QEvent::WinIdChange && _impl->webview)
    {
        NSView* parent = (__bridge NSView*)reinterpret_cast<void*>(winId());
        [_impl->webview removeFromSuperview];
        [parent addSubview:_impl->webview];
        _impl->webview.frame = parent.bounds;
    }
    return QWidget::event(event);
}

void HtmlView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (_impl->webview)
        _impl->webview.frame = NSMakeRect(0, 0, width(), height());
}

void HtmlView::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (_impl->webview)
        _impl->webview.hidden = YES;
}

void HtmlView::showEvent(QShowEvent* event)
{
    if (event)
        QWidget::showEvent(event);
    Impl& d = *_impl;
    if (d.webview)
    {
        d.webview.hidden = isVisible() ? NO : YES;
        return;
    }
    WKWebViewConfiguration* configuration = [[WKWebViewConfiguration alloc] init];
    if (!d.startScript.isEmpty())
    {
        WKUserScript* start = [[WKUserScript alloc] initWithSource:toNS(d.startScript)
                                                     injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                                                  forMainFrameOnly:NO];
        [configuration.userContentController addUserScript:start];
    }
    [configuration.preferences setValue:@YES forKey:@"developerExtrasEnabled"];
    NSView* parent = (__bridge NSView*)reinterpret_cast<void*>(winId());
    d.webview = [[WKWebView alloc] initWithFrame:parent.bounds configuration:configuration];
    d.webview.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    d.delegate = [[PhoenixWebDelegate alloc] init];
    d.delegate.owner = &d.owner;
    d.webview.UIDelegate = d.delegate;
    d.webview.navigationDelegate = d.delegate;
    [parent addSubview:d.webview];
    started(nullptr);
}

void HtmlView::started(void*)
{
    if (_impl->loadWanted)
        load(_impl->folder, _impl->page, QString());
}
