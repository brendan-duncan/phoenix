#pragma once

#include <QString>
#include <QWidget>

#include <functional>
#include <memory>

/// A web page in a widget, through the system's own browser engine:
/// WebView2 on Windows, WKWebView on macOS. Where there is none (Linux, or
/// a Windows without the WebView2 runtime) the widget says so instead.
///
/// The page reaches Phoenix through window.prompt(): a prompt whose
/// message starts with kCallMark goes to onCall, which answers it at once,
/// so a page's calls into Phoenix can be synchronous. Other alerts,
/// confirms and prompts come up as Phoenix's own dialogs.
class HtmlView : public QWidget
{
    Q_OBJECT

public:
    /// What starts a prompt meant for onCall.
    static constexpr const char* kCallMark = "\x01phoenix-call\x01";

    explicit HtmlView(QWidget* parent = nullptr);
    ~HtmlView() override;

    /// Whether pages can show here; \a why gets the reason when not.
    static bool available(QString* why = nullptr);

    /// Script run in each page before the page's own scripts.
    void setStartScript(const QString& script);
    /// Where the browser keeps its profile (storage, cache).
    void setProfileFolder(const QString& folder);
    /// Shows \a page, a file inside \a folder. The folder's files are what
    /// the page can load beside it; \a host names the folder in the page's
    /// address where the engine serves it under a name of its own.
    void load(const QString& folder, const QString& page, const QString& host);
    /// Runs \a script in the page, its result unwanted.
    void runScript(const QString& script);

    /// Answers a call from the page: the text after kCallMark in, the
    /// prompt's answer out.
    std::function<QString(const QString& request)> onCall;

signals:
    /// The page finished loading, or failed to.
    void loaded(bool ok);
    /// The page set document.title.
    void titleChanged(const QString& title);

protected:
    bool event(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    /// Answers a dialog the page opened: kind "alert", "confirm" or
    /// "prompt". \a accepted and \a answer are what the page gets back.
    void answerDialog(const QString& kind, const QString& message, const QString& defaultText, bool& accepted,
        QString& answer);
    /// The engine is up (its controller, on Windows): the page can load.
    void started(void* engine);

    struct Impl;
    std::unique_ptr<Impl> _impl;
    QString _unavailable;

    friend struct HtmlViewBridge;
};
