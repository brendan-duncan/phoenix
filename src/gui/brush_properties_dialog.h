#pragma once

#include "../edit/brush_presets.h"

#include <QDialog>
#include <QImage>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

namespace fla {
class DrawingStyle;
}

class QComboBox;
class QLabel;
class QListWidget;

/// Brush Properties, as Harmony's: the brush's presets with previews of
/// the stroke each paints, and every setting of the tip, its dynamics, the
/// bitmap brush and the texture. Each brush has its own list of presets,
/// and the bitmap brush another; they are kept between runs, and a list
/// can be exported to share and imported. Changes apply as they are made.
class BrushPropertiesDialog : public QDialog
{
    Q_OBJECT

public:
    /// What the dialog asks of the document: its library bitmaps' names
    /// and pixels, for bitmap tips and textures.
    struct Library
    {
        std::function<QStringList()> bitmapNames;
        std::function<QImage(const QString&)> bitmapImage;
    };

    /// \a list names the brush (fla::brushpresets::kFlow, kNib or kInk);
    /// with the bitmap brush on, the bitmap brush's list shows instead.
    BrushPropertiesDialog(fla::DrawingStyle& style, const std::string& list, Library library, QWidget* parent);

    /// Shows the settings as they are now, after a change made elsewhere.
    void refresh();

    /// The stroke a brush paints, for a preset's thumbnail: an S across
    /// \a size, pressing harder in the middle.
    static QImage strokePreview(const fla::BrushSettings& brush, const fla::DrawingStyle& style, const QSize& size,
        bool bitmap);

signals:
    /// A setting was changed here; the Tool tab shows it.
    void changed();

protected:
    void changeEvent(QEvent* event) override;

private:
    fla::BrushSettings& brush();
    /// The preset list in use: the brush's, or the bitmap brush's.
    std::string listKey() const;
    std::vector<fla::brushpresets::Preset> loadList() const;
    void saveList(const std::vector<fla::brushpresets::Preset>& presets) const;
    void fillList();
    void applyPreset(int row);
    void touched();
    void updatePreview();

    fla::DrawingStyle& _style;
    std::string _list;
    Library _library;
    QListWidget* _presets = nullptr;
    QComboBox* _view = nullptr;
    QLabel* _preview = nullptr;
    /// Each control re-reads its setting.
    std::vector<std::function<void()>> _refreshers;
    bool _refreshing = false;
};
