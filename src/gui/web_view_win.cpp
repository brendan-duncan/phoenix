// The web view on Windows: WebView2, the Edge runtime that comes with
// Windows, in a native child window of the widget.

#include "web_view.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QEvent>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <windows.h>
#include <wrl.h>

#include <WebView2.h>
// The SDK marks a member with a clang attribute that MSVC reports through
// __has_attribute but cannot compile.
#ifndef __clang__
#define __attribute__(x)
#endif
#include <WebView2EnvironmentOptions.h>
#ifndef __clang__
#undef __attribute__
#endif

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

/// A string WebView2 handed over, freed once read.
QString taken(LPWSTR text)
{
    const QString out = text ? QString::fromWCharArray(text) : QString();
    CoTaskMemFree(text);
    return out;
}

std::wstring wide(const QString& text)
{
    return text.toStdWString();
}

} // namespace

struct WebViewBridge
{
    static void answer(WebView& view, const QString& kind, const QString& message, const QString& defaultText,
        bool& accepted, QString& text)
    {
        view.answerDialog(kind, message, defaultText, accepted, text);
    }
    static void unavailable(WebView& view, const QString& why)
    {
        view._unavailable = why;
        view.update();
    }
};

struct WebView::Impl
{
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    QString startScript;
    QString profile;
    QString folder, page, host;
    bool creating = false;
    bool loadWanted = false;
    QStringList scripts;
};

WebView::WebView(QWidget* parent)
    : QWidget(parent)
    , _impl(std::make_unique<Impl>())
{
    // Only this widget needs a window of its own, not its neighbors.
    QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_DontCreateNativeAncestors);
    setFocusPolicy(Qt::StrongFocus);
    if (!available(&_unavailable))
        update();
}

WebView::~WebView()
{
    if (_impl->controller)
        _impl->controller->Close();
}

bool WebView::available(QString* why)
{
    LPWSTR version = nullptr;
    const HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &version);
    const bool found = SUCCEEDED(hr) && version;
    CoTaskMemFree(version);
    if (!found && why)
        *why = "HTML panels need the Microsoft Edge WebView2 Runtime, which is not installed. It comes with "
               "Windows 11 and can be installed from Microsoft on Windows 10.";
    return found;
}

void WebView::setStartScript(const QString& script)
{
    _impl->startScript = script;
}

void WebView::setProfileFolder(const QString& folder)
{
    _impl->profile = folder;
}

void WebView::load(const QString& folder, const QString& page, const QString& host)
{
    Impl& d = *_impl;
    d.folder = folder;
    d.page = page;
    d.host = host;
    d.loadWanted = true;
    if (d.webview)
    {
        ComPtr<ICoreWebView2_3> mapping;
        if (SUCCEEDED(d.webview.As(&mapping)))
            mapping->SetVirtualHostNameToFolderMapping(wide(host).c_str(), wide(folder).c_str(),
                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
        QString path = page;
        path.replace('\\', '/');
        const QString address = "https://" + host + "/" + QString::fromUtf8(QUrl::toPercentEncoding(path, "/"));
        d.webview->Navigate(wide(address).c_str());
        d.loadWanted = false;
        return;
    }
    // The browser starts now even with the widget hidden: a panel started
    // in the background runs its page out of sight.
    showEvent(nullptr);
}

void WebView::runScript(const QString& script)
{
    Impl& d = *_impl;
    if (!d.webview)
    {
        d.scripts << script;
        return;
    }
    d.webview->ExecuteScript(wide(script).c_str(),
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>([](HRESULT, LPCWSTR) -> HRESULT { return S_OK; }).Get());
}

bool WebView::event(QEvent* event)
{
    // Moving between a dock and a floating window can give the widget a
    // new native window; the browser follows it.
    if (event->type() == QEvent::WinIdChange && _impl->controller)
        _impl->controller->put_ParentWindow(reinterpret_cast<HWND>(winId()));
    else if (event->type() == QEvent::Move && _impl->controller)
        _impl->controller->NotifyParentWindowPositionChanged();
    return QWidget::event(event);
}

void WebView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (_impl->controller)
    {
        const qreal ratio = devicePixelRatioF();
        _impl->controller->put_Bounds(RECT{0, 0, qRound(width() * ratio), qRound(height() * ratio)});
    }
}

void WebView::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (_impl->controller)
        _impl->controller->put_IsVisible(FALSE);
}

void WebView::showEvent(QShowEvent* event)
{
    if (event)
        QWidget::showEvent(event);
    Impl& d = *_impl;
    if (d.controller)
    {
        d.controller->put_IsVisible(isVisible() ? TRUE : FALSE);
        return;
    }
    if (d.creating || !_unavailable.isEmpty())
        return;
    d.creating = true;

    // The browser comes up in the background: the environment, then the
    // controller in this widget's window, then the page.
    QPointer<WebView> self(this);
    const HWND window = reinterpret_cast<HWND>(winId());
    auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    const HRESULT started = CreateCoreWebView2EnvironmentWithOptions(nullptr,
        d.profile.isEmpty() ? nullptr : wide(d.profile).c_str(), options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [self, window](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
                if (!self)
                    return S_OK;
                if (FAILED(result) || !environment)
                {
                    WebViewBridge::unavailable(*self, "The web view could not start (WebView2 error 0x" +
                                                          QString::number(static_cast<uint>(result), 16) + ").");
                    return S_OK;
                }
                return environment->CreateCoreWebView2Controller(window,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [self](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                            if (!self)
                            {
                                if (controller)
                                    controller->Close();
                                return S_OK;
                            }
                            if (FAILED(result) || !controller)
                            {
                                WebViewBridge::unavailable(*self, "The web view could not start (WebView2 error 0x" +
                                                                      QString::number(static_cast<uint>(result), 16) + ").");
                                return S_OK;
                            }
                            self->started(controller);
                            return S_OK;
                        })
                        .Get());
            })
            .Get());
    if (FAILED(started))
    {
        d.creating = false;
        WebViewBridge::unavailable(*this, "The web view could not start (WebView2 error 0x" +
                                              QString::number(static_cast<uint>(started), 16) + ").");
    }
}

void WebView::started(void* handle)
{
    Impl& d = *_impl;
    auto* controller = static_cast<ICoreWebView2Controller*>(handle);
    d.controller = controller;
    d.controller->get_CoreWebView2(&d.webview);
    d.creating = false;

    ComPtr<ICoreWebView2Settings> settings;
    if (SUCCEEDED(d.webview->get_Settings(&settings)))
    {
        // The page's alerts and prompts come here: prompts carry its calls.
        settings->put_AreDefaultScriptDialogsEnabled(FALSE);
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_IsZoomControlEnabled(FALSE);
        settings->put_AreDevToolsEnabled(TRUE);
    }
    const qreal ratio = devicePixelRatioF();
    d.controller->put_Bounds(RECT{0, 0, qRound(width() * ratio), qRound(height() * ratio)});
    d.controller->put_IsVisible(isVisible() ? TRUE : FALSE);

    if (!d.startScript.isEmpty())
        d.webview->AddScriptToExecuteOnDocumentCreated(wide(d.startScript).c_str(), nullptr);

    QPointer<WebView> self(this);
    EventRegistrationToken token;
    d.webview->add_ScriptDialogOpening(
        Callback<ICoreWebView2ScriptDialogOpeningEventHandler>(
            [self](ICoreWebView2*, ICoreWebView2ScriptDialogOpeningEventArgs* args) -> HRESULT {
                if (!self)
                    return S_OK;
                COREWEBVIEW2_SCRIPT_DIALOG_KIND kind;
                args->get_Kind(&kind);
                LPWSTR message = nullptr;
                args->get_Message(&message);
                LPWSTR defaultText = nullptr;
                args->get_DefaultText(&defaultText);
                const QString text = taken(message);
                const QString preset = taken(defaultText);
                // The page waits for the answer; it is given once the
                // window's events have run, so whatever answers it (a file
                // dialog, a script) is not inside the browser's callback.
                ComPtr<ICoreWebView2Deferral> deferral;
                args->GetDeferral(&deferral);
                ComPtr<ICoreWebView2ScriptDialogOpeningEventArgs> held(args);
                QTimer::singleShot(0, self, [self, kind, text, preset, deferral, held]() {
                    bool accepted = true;
                    QString answer;
                    const QString mark = QString::fromLatin1(WebView::kCallMark);
                    if (kind == COREWEBVIEW2_SCRIPT_DIALOG_KIND_PROMPT && text.startsWith(mark))
                        answer = self->onCall ? self->onCall(text.mid(mark.size())) : QString();
                    else if (kind == COREWEBVIEW2_SCRIPT_DIALOG_KIND_ALERT)
                        WebViewBridge::answer(*self, "alert", text, preset, accepted, answer);
                    else if (kind == COREWEBVIEW2_SCRIPT_DIALOG_KIND_CONFIRM)
                        WebViewBridge::answer(*self, "confirm", text, preset, accepted, answer);
                    else if (kind == COREWEBVIEW2_SCRIPT_DIALOG_KIND_PROMPT)
                        WebViewBridge::answer(*self, "prompt", text, preset, accepted, answer);
                    if (accepted)
                    {
                        held->put_ResultText(wide(answer).c_str());
                        held->Accept();
                    }
                    deferral->Complete();
                });
                return S_OK;
            })
            .Get(),
        &token);
    d.webview->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [self](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                BOOL ok = FALSE;
                args->get_IsSuccess(&ok);
                if (self)
                    emit self->loaded(ok == TRUE);
                return S_OK;
            })
            .Get(),
        &token);
    d.webview->add_DocumentTitleChanged(
        Callback<ICoreWebView2DocumentTitleChangedEventHandler>([self](ICoreWebView2* webview, IUnknown*) -> HRESULT {
            LPWSTR title = nullptr;
            webview->get_DocumentTitle(&title);
            const QString text = taken(title);
            if (self)
                emit self->titleChanged(text);
            return S_OK;
        }).Get(),
        &token);
    // Links that open a window go to the person's browser.
    d.webview->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                LPWSTR uri = nullptr;
                args->get_Uri(&uri);
                const QUrl url(taken(uri));
                args->put_Handled(TRUE);
                if (url.scheme() == "http" || url.scheme() == "https" || url.scheme() == "mailto")
                    QDesktopServices::openUrl(url);
                return S_OK;
            })
            .Get(),
        &token);

    for (const QString& script : d.scripts)
        runScript(script);
    d.scripts.clear();
    if (d.loadWanted)
        load(d.folder, d.page, d.host);
}
