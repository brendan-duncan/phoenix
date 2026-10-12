#pragma once

#include "../data/dom_element.h"

#include <QDialog>
#include <QPointF>
#include <QString>

class QComboBox;
class QLineEdit;
class RegistrationGrid;

/// The dialog Modify > Convert to Symbol puts up, laid out like Animate's:
/// a name, a type and a three-by-three grid of registration points.
class ConvertToSymbolDialog : public QDialog
{
    Q_OBJECT
public:
    ConvertToSymbolDialog(const QString& defaultName, QWidget* parent = nullptr);

    QString symbolName() const;

    /// For Create New Symbol, which has nothing to place a registration
    /// point in: the grid goes.
    void hideRegistration();

    fla::SymbolType symbolType() const;

    /// Where the registration point sits inside the selection's bounds, each
    /// coordinate 0, 0.5 or 1.
    QPointF registrationFraction() const;

private:
    QLineEdit* _name;
    QComboBox* _type;
    RegistrationGrid* _registration;
    QWidget* _registrationRow = nullptr;
    class QFormLayout* _form = nullptr;
};

/// Nine small boxes, the chosen one filled, for picking which corner, edge
/// or center of the selection becomes the symbol's origin.
class RegistrationGrid : public QWidget
{
    Q_OBJECT
public:
    explicit RegistrationGrid(QWidget* parent = nullptr);

    /// Column and row of the chosen box, each 0, 1 or 2.
    int column() const { return _column; }
    int row() const { return _row; }

    void setChoice(int column, int row);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QRect boxRect(int column, int row) const;

    int _column = 0;
    int _row = 0;
};
