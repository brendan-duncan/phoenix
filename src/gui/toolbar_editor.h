#pragma once

#include "toolbox.h"

#include <QDialog>

class QTreeWidget;
class QTreeWidgetItem;

/// Animate's Edit Toolbar: the Tools panel's slots as a list, each holding
/// its tools, the first one shown. Drag a tool to reorder it, into another
/// slot to group it there, or between slots to give it a slot of its own;
/// drag it under Hidden to take it off the panel (its key still works).
/// Lines part the sections. Reset puts back Phoenix's own layout.
class ToolbarEditor : public QDialog
{
    Q_OBJECT

public:
    explicit ToolbarEditor(Toolbox* toolbox, QWidget* parent = nullptr);

    /// The layout as edited.
    Toolbox::Layout layout() const;

private:
    void load(const Toolbox::Layout& layout);
    /// After a drop: tools only inside slots, no slot inside another, no
    /// empty slots, Hidden last.
    void tidy();
    QTreeWidgetItem* slotItem(const Toolbox::Slot& slot);
    QTreeWidgetItem* toolItem(const QString& id);

    Toolbox* _toolbox;
    QTreeWidget* _tree;
    QTreeWidgetItem* _hidden = nullptr;
    int _columns = 1;
    bool _tidying = false;
};
