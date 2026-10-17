#include "movie_hooks.h"

#include "../data/document.h"
#include "../data/fla_document.h"
#include "../edit/canvas_document.h"
#include "../play/movie_runtime.h"
#include "../play/script/class_path.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>

#ifdef PHOENIX_NETWORK
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#endif

QStringList actionScriptClassPaths()
{
    return QSettings().value("preferences/actionScriptClassPaths").toStringList();
}

void setActionScriptClassPaths(const QStringList& paths)
{
    QSettings().setValue("preferences/actionScriptClassPaths", paths);
}

void connectMovie(fla::play::MovieRuntime& movie, const fla::FLADocument& document, const QString& file, QObject* owner,
    std::function<void(const QString&)> notice)
{
    if (!document.document)
        return;
    const fla::Document& data = *document.document;
    movie.setActionScriptVersion(fla::play::actionScriptVersion(data));
    // An HTML5 Canvas document's scripts are JavaScript.
    movie.setJavaScript(fla::canvas::isCanvasDocument(data));

    // Where the movie is: what its scripts load from, and its address.
    const QFileInfo info(file);
    const QString folder = file.isEmpty() ? QString() : info.absolutePath();
    movie.folder = folder.toStdString();
    if (!file.isEmpty())
        movie.url = QUrl::fromLocalFile(QDir(folder).filePath(info.completeBaseName() + ".swf")).toString().toStdString();

    // Its classes: beside it, on its class path, and Phoenix's own.
    std::vector<std::string> extra;
    for (const QString& path : actionScriptClassPaths())
        if (!path.trimmed().isEmpty())
            extra.push_back(QDir::fromNativeSeparators(path.trimmed()).toStdString());
    fla::play::ClassPath path(data, folder.toStdString(), extra);
    path.builtIn = [](const std::string& relative) -> std::optional<std::string> {
        QFile source(":/as3/" + QString::fromStdString(relative));
        if (!source.open(QIODevice::ReadOnly))
            return std::nullopt;
        return source.readAll().toStdString();
    };
    movie.findClass = [path](const std::string& name) { return path.find(name); };
    movie.setClasses({}, fla::play::documentClassOf(data));

    // Images in any format Qt reads.
    movie.decodeImage = [](const std::vector<uint8_t>& bytes, int& width, int& height, std::vector<uint32_t>& pixels) {
        QImage image;
        if (!image.loadFromData(bytes.data(), static_cast<int>(bytes.size())))
            return false;
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        width = image.width();
        height = image.height();
        pixels.resize(static_cast<size_t>(width) * height);
        for (int y = 0; y < height; ++y)
        {
            const auto* row = reinterpret_cast<const uint32_t*>(image.constScanLine(y));
            std::copy(row, row + width, pixels.begin() + static_cast<std::ptrdiff_t>(y) * width);
        }
        return true;
    };

    // SharedObjects, kept with Phoenix's settings.
    const QString apart = qEnvironmentVariable("PHOENIX_SETTINGS_DIR");
    const QString base = !apart.isEmpty() ? apart : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!base.isEmpty())
        movie.sharedObjectFolder = QDir(base).filePath("SharedObjects").toStdString();

    movie.notice = [notice](const std::string& text) {
        if (notice)
            notice(QString::fromStdString(text));
    };

#ifdef PHOENIX_NETWORK
    // The web, asked as the movie asks it; what comes is given to the movie
    // as it comes.
    auto* network = new QNetworkAccessManager(owner);
    movie.fetch = [network](const fla::play::MovieRuntime::Request& request,
                      std::function<void(const fla::play::MovieRuntime::Response&)> done) {
        QNetworkRequest asked(QUrl(QString::fromStdString(request.url)));
        asked.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        for (const auto& [name, value] : request.headers)
            asked.setRawHeader(QByteArray::fromStdString(name), QByteArray::fromStdString(value));
        if (!request.body.empty() && !request.contentType.empty())
            asked.setHeader(QNetworkRequest::ContentTypeHeader, QString::fromStdString(request.contentType));
        const QByteArray body(reinterpret_cast<const char*>(request.body.data()), static_cast<int>(request.body.size()));
        QNetworkReply* reply = request.method == "GET" && body.isEmpty()
            ? network->get(asked)
            : network->sendCustomRequest(asked, QByteArray::fromStdString(request.method), body);
        QObject::connect(reply, &QNetworkReply::finished, reply, [reply, done]() {
            fla::play::MovieRuntime::Response response;
            response.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray bytes = reply->readAll();
            response.bytes.assign(bytes.begin(), bytes.end());
            response.ok = reply->error() == QNetworkReply::NoError;
            if (!response.ok)
                response.error = reply->errorString().toStdString();
            reply->deleteLater();
            done(response);
        });
    };
#else
    (void)owner;
#endif
}
