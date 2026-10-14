#pragma once

#include "theme.h"

#include <QDialog>

class QComboBox;
class HotDoubleSpinBox;
class HotSpinBox;
class QTreeWidget;
class QTreeWidgetItem;

/// Window > Theme > Edit Theme: every color role by area, each with its
/// swatch, changed live on the real window as it is edited. Start from
/// any theme, edit, and save it under a name of its own; themes can also
/// be imported and exported as files to share. Font size and UI scale
/// (from the next start) are part of a theme.
class ThemeEditor : public QDialog
{
    Q_OBJECT

public:
    explicit ThemeEditor(QWidget* parent = nullptr);

protected:
    void reject() override;

private:
    void startFrom(const QString& name);
    void fill();
    void pick(QTreeWidgetItem* item);
    void changed();
    bool saveAs();

    theme::Theme _theme;
    /// The theme in use when the editor opened, put back if nothing is
    /// saved.
    QString _was;
    bool _saved = false;
    QComboBox* _start;
    QComboBox* _base;
    HotSpinBox* _fontSize;
    HotDoubleSpinBox* _scale;
    QTreeWidget* _roles;
};
