#pragma once

#include <QDialog>
#include <QImage>
#include <QString>
#include <QStringList>

#include <functional>

class QLabel;
class QListWidget;

/// Modify > Symbol > Swap Symbol: picks the library symbol an instance
/// shows instead of its own, keeping everything else about it, with a
/// preview of each and a Duplicate Symbol button.
class SwapSymbolDialog : public QDialog
{
    Q_OBJECT

public:
    using Preview = std::function<QImage(const QString& name, const QSize& size)>;
    /// Duplicates the named symbol and answers the copy's name, or an empty
    /// string when it could not.
    using Duplicate = std::function<QString(const QString& name)>;

    /// With no \a duplicate the Duplicate button is left out, as for
    /// Modify > Bitmap > Swap Bitmap, which shares the dialog.
    SwapSymbolDialog(const QStringList& symbols, const QString& current, Preview preview,
        Duplicate duplicate, QWidget* parent = nullptr, const QString& title = "Swap Symbol");

    /// The symbol chosen.
    QString chosen() const;

private:
    void showPreview();

    QListWidget* _list = nullptr;
    QLabel* _preview = nullptr;
    Preview _previewOf;
    Duplicate _duplicate;
};
