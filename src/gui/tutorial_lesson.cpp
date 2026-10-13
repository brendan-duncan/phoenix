#include "tutorial_lesson.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace tutorials {

QString userFolder()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("tutorials");
}

bool readLesson(const QString& folder, TutorialLesson& lesson, QString& error)
{
    QFile file(QDir(folder).filePath("lesson.json"));
    if (!file.open(QIODevice::ReadOnly))
    {
        error = "no lesson.json";
        return false;
    }
    QJsonParseError parse;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parse);
    if (json.isNull() || !json.isObject())
    {
        error = "lesson.json is not readable JSON: " + parse.errorString();
        return false;
    }
    const QJsonObject root = json.object();
    lesson = TutorialLesson();
    lesson.id = QFileInfo(folder).fileName();
    lesson.folder = folder;
    lesson.builtIn = folder.startsWith(":/");
    lesson.title = root.value("title").toString(lesson.id);
    lesson.summary = root.value("summary").toString();
    lesson.minutes = root.value("minutes").toInt(5);
    lesson.order = root.value("order").toInt(100);
    lesson.guide = root.value("guide").toString();

    const QJsonObject start = root.value("start").toObject();
    if (start.contains("new"))
    {
        const QJsonObject made = start.value("new").toObject();
        lesson.startKind = "new";
        lesson.startWidth = std::clamp(made.value("width").toInt(1280), 1, 8192);
        lesson.startHeight = std::clamp(made.value("height").toInt(720), 1, 8192);
        lesson.startFps = std::clamp(made.value("fps").toDouble(24.0), 1.0, 120.0);
    }
    else if (start.contains("document"))
    {
        lesson.startKind = "document";
        lesson.startDocument = QDir(folder).filePath(start.value("document").toString());
    }

    for (const QJsonValue& value : root.value("steps").toArray())
    {
        const QJsonObject object = value.toObject();
        TutorialStep step;
        step.title = object.value("title").toString();
        step.text = object.value("text").toString();
        step.target = object.value("target").toString();
        step.image = object.value("image").toString();
        if (!step.image.isEmpty())
            step.image = QDir(folder).filePath(step.image);
        const QJsonObject wait = object.value("wait").toObject();
        if (!wait.isEmpty())
        {
            step.waitKind = wait.keys().front();
            const QJsonValue what = wait.value(step.waitKind);
            step.waitValue = what.isDouble() ? QString::number(what.toInt())
                : what.isBool()              ? QString(what.toBool() ? "true" : "false")
                                             : what.toString();
        }
        lesson.steps.push_back(step);
    }
    if (lesson.steps.empty())
    {
        error = "the lesson has no steps";
        return false;
    }
    return true;
}

std::vector<TutorialLesson> allLessons(QStringList* problems)
{
    std::vector<TutorialLesson> lessons;
    // PHOENIX_TUTORIALS_DIR: lessons being written, read from their folder
    // without building them into the app.
    QStringList roots = {QString(":/tutorials"), userFolder()};
    const QString writing = qEnvironmentVariable("PHOENIX_TUTORIALS_DIR");
    if (!writing.isEmpty())
        roots << writing;
    for (const QString& root : roots)
    {
        const QDir dir(root);
        for (const QString& name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        {
            TutorialLesson lesson;
            QString error;
            if (readLesson(dir.filePath(name), lesson, error))
            {
                // One of one's own with a built-in lesson's name takes its place.
                lessons.erase(std::remove_if(lessons.begin(), lessons.end(),
                                  [&](const TutorialLesson& other) { return other.id == lesson.id; }),
                    lessons.end());
                lessons.push_back(lesson);
            }
            else if (problems)
                problems->append(QString("%1: %2").arg(dir.filePath(name), error));
        }
    }
    std::stable_sort(lessons.begin(), lessons.end(), [](const TutorialLesson& a, const TutorialLesson& b) {
        return a.order != b.order ? a.order < b.order : a.title < b.title;
    });
    return lessons;
}

int stepReached(const QString& id)
{
    return QSettings().value("tutorials/" + id + "/step", 0).toInt();
}

bool finished(const QString& id)
{
    return QSettings().value("tutorials/" + id + "/done", false).toBool();
}

void recordProgress(const QString& id, int step, bool done)
{
    QSettings settings;
    settings.setValue("tutorials/" + id + "/step", step);
    if (done)
        settings.setValue("tutorials/" + id + "/done", true);
}

} // namespace tutorials
