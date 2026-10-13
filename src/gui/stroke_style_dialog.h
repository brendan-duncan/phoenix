#pragma once

#include "../edit/drawing_style.h"

#include <QColor>
#include <QDialog>

class QLabel;
class QVBoxLayout;

/// Edit > Custom Stroke Style: the settings of a dashed, dotted, ragged,
/// stipple or hatched stroke (dash and gap lengths, dot spacing, the
/// ragged line's pattern and waves, the stipple's dots, the hatching's
/// lines), with a stroke drawn the way they make it.
class StrokeStyleDialog : public QDialog
{
    Q_OBJECT

public:
    StrokeStyleDialog(const fla::StrokeLook& look, double weight, const QColor& color, QWidget* parent = nullptr);

    /// The look as edited.
    const fla::StrokeLook& look() const { return _look; }

private:
    void addChoice(QVBoxLayout* layout, const QString& label, const char* attribute, const QStringList& values,
        const QString& fallback);
    void showPreview();

    fla::StrokeLook _look;
    double _weight;
    QColor _color;
    QLabel* _preview = nullptr;
};
