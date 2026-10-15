#pragma once

#include <QDialog>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

class QLabel;
class QPushButton;
class QTreeWidget;

/// One render: a document, where its frames go (the extension choosing the
/// format: a folder for PNGs, .mp4, .mov, .gif, .svg, .swf), and what else
/// Phoenix render is told.
struct RenderJob
{
    QString document;
    QString output;
    /// "1-240", or empty for every frame.
    QString range;
    /// "1920x1080", or empty for the stage's size.
    QString size;
    /// The scene by name, or "*" for every scene one after another, "+"
    /// for each scene to its own file; empty for the one the document
    /// opens on.
    QString scene;
    /// Other options, as Phoenix render takes them ("--transparent",
    /// "--prores4444", "--everything", "--no-camera").
    QStringList options;
    /// Rows of template values (a CSV file): a movie for each row, into
    /// the output folder as \a format files (mp4, mov, gif or png).
    QString data;
    QString format;

    enum class State
    {
        Waiting,
        Rendering,
        Done,
        Failed
    };
    State state = State::Waiting;
    QString message;

    /// The arguments Phoenix is run with.
    QStringList arguments() const;
};

/// Window > Render Queue: renders lined up and run one after another, each
/// in a Phoenix of its own with no window, while work goes on here. The
/// queue is kept between runs.
class RenderQueue : public QDialog
{
    Q_OBJECT

public:
    /// \a current gives the document open now, for Add Current.
    RenderQueue(std::function<QString()> current, QWidget* parent);
    ~RenderQueue() override;

private:
    void fill();
    void save() const;
    void load();
    void addJob(const QString& document, const QString& data = QString());
    bool editJob(RenderJob& job);
    bool editDataJob(RenderJob& job, QDialog& dialog, class QFormLayout* form);
    void start();
    void runNext();
    void stop();

    std::function<QString()> _current;
    std::vector<RenderJob> _jobs;
    QTreeWidget* _list = nullptr;
    QPushButton* _start = nullptr;
    QPushButton* _stop = nullptr;
    QLabel* _status = nullptr;
    QProcess* _process = nullptr;
    int _running = -1;
    bool _stopping = false;
};
