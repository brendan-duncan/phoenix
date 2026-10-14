#pragma once

#include "../edit/trace_bitmap.h"

#include <QDialog>

#include <functional>

class HotSpinBox;
class QCheckBox;
class QComboBox;
class QLabel;
class QTimer;

/// Modify > Bitmap > Trace Bitmap: the settings, and Preview, which traces
/// the selected bitmaps on the stage with them so the result can be judged
/// in place. With Auto Preview on, the stage is traced again a moment after
/// the settings stop changing.
class TraceBitmapDialog : public QDialog
{
    Q_OBJECT

public:
    /// What a preview came to, over every bitmap traced.
    struct Counts
    {
        int areas = 0;
        int points = 0;
        /// How many pixels the bitmaps have, for the advice.
        long long pixels = 0;
    };
    /// Traces the bitmaps on the stage with the options given, in place of
    /// any earlier preview.
    using Preview = std::function<Counts(const fla::trace::Options&)>;

    TraceBitmapDialog(int count, Preview trace, QWidget* parent = nullptr);

    fla::trace::Options options() const;

private:
    void settingsChanged(bool live);
    void preview();

    Preview _preview;
    HotSpinBox* _threshold = nullptr;
    HotSpinBox* _area = nullptr;
    QComboBox* _fit = nullptr;
    QCheckBox* _auto = nullptr;
    QLabel* _status = nullptr;
    QLabel* _advice = nullptr;
    QTimer* _wait = nullptr;
};
