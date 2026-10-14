#pragma once

#include <QColor>
#include <QDialog>
#include <QStringList>

#include <vector>

class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class HotSpinBox;
class QToolButton;

/// Animate's New Document dialog: categories of presets down the left
/// (Character Animation, Social, Game, Education, Ads, Web, Advanced), the
/// presets of the chosen category as tiles, and the details of the document
/// to make on the right: width, height, frame rate and stage color, which a
/// preset fills in and which can be changed before creating it. The last
/// choice is remembered for the next time.
class NewDocumentDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NewDocumentDialog(QWidget* parent = nullptr);

    int documentWidth() const;
    int documentHeight() const;
    double frameRate() const;
    QColor stageColor() const { return _stageColor; }

    /// One preset: a name, a stage size and a frame rate.
    struct Preset
    {
        const char* category;
        const char* name;
        int width;
        int height;
        double frameRate;
    };

    /// Every preset, in the order the dialog shows them.
    static const std::vector<Preset>& presets();

    /// The category names, in order.
    static QStringList categories();

protected:
    void accept() override;

private:
    void showCategory(const QString& category);
    void applyPreset(const Preset& preset);
    void markMatchingPreset();
    void updateSwatch();

    QListWidget* _categories;
    QListWidget* _tiles;
    HotSpinBox* _width;
    HotSpinBox* _height;
    QComboBox* _frameRate;
    QToolButton* _swatch;
    QLabel* _summary;
    QColor _stageColor = Qt::white;
    bool _applying = false;
};
