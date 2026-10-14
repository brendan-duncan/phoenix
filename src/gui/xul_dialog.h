#pragma once

#include <QDialog>

#include <map>

namespace jsfl::xul {
class Dialog;
struct Control;
struct Node;
} // namespace jsfl::xul

class QBoxLayout;
class QWidget;

/// A script's XML (XUL) dialog as a window dialog: boxes and grids laid
/// out, a widget for each control, OK and Cancel along the bottom. What the
/// person does goes to the dialog model, which runs the script's handlers;
/// what the handlers change comes back to the widgets.
class XulDialog : public QDialog
{
public:
    XulDialog(jsfl::xul::Dialog& dialog, QWidget* parent = nullptr);
    ~XulDialog() override;

    void accept() override;
    void reject() override;

private:
    void fill(const jsfl::xul::Node& node, QBoxLayout* box);
    QWidget* make(const jsfl::xul::Node& node);
    QWidget* makeGrid(const jsfl::xul::Node& node);
    QWidget* makeControl(jsfl::xul::Control& control);
    /// Shows \a control's value, items, enabled and visible state.
    void refresh(jsfl::xul::Control& control);

    jsfl::xul::Dialog& _dialog;
    std::map<const jsfl::xul::Control*, QWidget*> _widgets;
    /// Labels whose control attribute names a control, to point at it.
    std::map<QWidget*, std::string> _buddies;
    bool _refreshing = false;
};
