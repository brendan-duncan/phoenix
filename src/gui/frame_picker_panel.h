#pragma once

#include <QImage>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QTimer;

namespace fla {
class SymbolInstance;
}

/// Animate's Frame Picker: the frames of the selected graphic instance's
/// symbol as pictures, one click picking the frame the instance starts on,
/// with its looping and, optionally, a new keyframe for the choice.
class FramePickerPanel : public QWidget
{
    Q_OBJECT

public:
    /// A picture of frame \a frame of the symbol \a name.
    using Preview = std::function<QImage(const QString& name, int frame, const QSize& size)>;

    explicit FramePickerPanel(QWidget* parent = nullptr);

    void setPreviewProvider(Preview preview) { _preview = std::move(preview); }

    /// Shows the frames of \a instance's symbol, or a hint when it is null
    /// or not a graphic.
    void setInstance(const fla::SymbolInstance* instance, int frameCount);

    /// Re-reads the instance's first frame and looping, after an edit.
    void refresh();

signals:
    /// The frame picked (counting from 0), with whether to set it on a new
    /// keyframe at the playhead.
    void frameChosen(int frame, bool createKeyframe);
    /// The looping picked, as an fla::LoopType.
    void loopChosen(int loopType);

private:
    void renderSome();

    const fla::SymbolInstance* _instance = nullptr;
    QLabel* _title = nullptr;
    QComboBox* _loop = nullptr;
    QCheckBox* _createKeyframe = nullptr;
    QListWidget* _frames = nullptr;
    QTimer* _renderTimer = nullptr;
    Preview _preview;
    int _nextToRender = 0;
    bool _updating = false;
};
