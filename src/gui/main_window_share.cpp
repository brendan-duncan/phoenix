#include "main_window.h"

#include "phoenix_view.h"
#include "share_dialog.h"

#include "../data/document.h"
#include "../data/fla_document.h"

#include <QFileInfo>
#include <QMessageBox>
#include <QStatusBar>

#include <cmath>

void MainWindow::shareMovie()
{
    if (!_flaDocument || !_flaDocument->document || !_phoenixView)
    {
        QMessageBox::information(this, "No Document", "No FLA document is currently loaded.");
        return;
    }
    const fla::Document* document = _flaDocument->document;
    const QString base = _documentPath.isEmpty() ? QString("animation") : QFileInfo(_documentPath).completeBaseName();
    ShareDialog::Hooks hooks;
    hooks.render = [this](int frame, const QSize& size, bool transparent) {
        return _phoenixView->renderFrame(frame, size, transparent);
    };
    hooks.write = [this, document](const MovieExportSettings& settings, QString& error) {
        const bool written =
            MovieExport::run(settings, *_phoenixView, _timelineAudio, document->frameRate, this, error);
        _phoenixView->update();
        if (written)
            statusBar()->showMessage(QString("Shared %1").arg(QFileInfo(settings.path).fileName()), 5000);
        return written;
    };
    hooks.reveal = [](const QString& path) { revealInFileManager(path); };
    ShareDialog dialog(QSize(static_cast<int>(std::lround(document->width)), static_cast<int>(std::lround(document->height))),
        document->frameRate, std::max(1, _phoenixView->frameCount()), base, hooks, this);
    dialog.exec();
}
