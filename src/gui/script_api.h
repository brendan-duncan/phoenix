#pragma once

#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <functional>
#include <vector>

class MainWindow;

/// The document operations scripts and AI assistants call: one set, used by
/// the MCP server (mcp_server.h) and meant for the JSFL engine and the
/// command line too. Each operation takes and gives JSON. Every change goes
/// through the undo history as an ordinary edit named "AI: <operation>",
/// and begin_edit/end_edit gather a request's changes into one undo step.
///
/// Addresses, as the operations take them:
/// - "symbol": a library symbol to work inside; the scene shown when absent
/// - "layer": a layer's name, or its index counting from 0 at the top
/// - "frame": a frame number, counting from 1 as the timeline does
/// - "index": an element's place on its keyframe, from 0 at the bottom
///
/// Calls run on the GUI thread.
class ScriptApi
{
public:
    explicit ScriptApi(MainWindow& window);

    struct Result
    {
        QJsonValue value;
        /// A picture to hand back too (render_frame).
        QImage image;
        QString error;
        bool ok() const { return error.isEmpty(); }
    };

    struct Operation
    {
        QString name;
        QString title;
        QString description;
        /// The JSON Schema of its arguments.
        QJsonObject schema;
        /// Whether it changes the document.
        bool writes = false;
        std::function<Result(const QJsonObject&)> run;
    };

    const std::vector<Operation>& operations() const { return _operations; }
    Result call(const QString& name, const QJsonObject& arguments);

    /// The document and its symbols as readable resources:
    /// phoenix://document, phoenix://timeline, phoenix://symbol/<name>.
    QJsonArray resources() const;
    Result readResource(const QString& uri);

    /// Asks the person whether a file may be written at \a path; remembers
    /// the answer for the folder until Phoenix closes.
    std::function<bool(const QString& path)> approveWrite;

    /// Calls come from outside Phoenix (an AI assistant), not from a script
    /// it runs: one arriving while a change of its own is under way (a
    /// drag, a script) is turned away rather than folded into it.
    bool fromOutside = false;

private:
    void add(const QString& name, const QString& title, const QString& description, const QJsonObject& properties,
        const QStringList& required, bool writes, std::function<Result(const QJsonObject&)> run);
    void addReadOperations();
    void addWriteOperations();
    void addHelperOperations();
    void addMotionGraphicsOperations();

    MainWindow& _window;
    std::vector<Operation> _operations;
    /// What is wrong with the group begin_edit opened (its document is no
    /// longer the one in front, or something else closed its undo step);
    /// empty while it is sound, or when none is open.
    QString groupTrouble() const;

    /// The label of the group begin_edit opened, while it is open.
    QString _group;
    /// The document it was opened on, and where the history stood then:
    /// inside the group nothing reaches the history, so a change there means
    /// something was undone or redone meanwhile.
    const void* _groupDocument = nullptr;
    size_t _groupIndex = 0;
    size_t _groupCount = 0;
};
