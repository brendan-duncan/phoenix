#include "convert_to_symbol_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
#include <algorithm>

namespace
{

/// The last type and registration chosen, remembered across documents.
const char* kTypeSetting = "convertToSymbol/type";
const char* kRegistrationColumnSetting = "convertToSymbol/registrationColumn";
const char* kRegistrationRowSetting = "convertToSymbol/registrationRow";

const int kBox = 9;
const int kGap = 4;

} // namespace

ConvertToSymbolDialog::ConvertToSymbolDialog(const QString& defaultName, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Convert to Symbol");
    setModal(true);

    _name = new QLineEdit(defaultName, this);
    _name->selectAll();

    _type = new QComboBox(this);
    _type->addItem("Movie Clip");
    _type->addItem("Button");
    _type->addItem("Graphic");

    _registration = new RegistrationGrid(this);

    QSettings settings;
    _type->setCurrentIndex(settings.value(kTypeSetting, 0).toInt());
    _registration->setChoice(settings.value(kRegistrationColumnSetting, 0).toInt(),
        settings.value(kRegistrationRowSetting, 0).toInt());

    QWidget* registrationRow = new QWidget(this);
    _registrationRow = registrationRow;
    QHBoxLayout* registrationLayout = new QHBoxLayout(registrationRow);
    registrationLayout->setContentsMargins(0, 0, 0, 0);
    registrationLayout->addWidget(_registration);
    registrationLayout->addStretch();

    QFormLayout* form = new QFormLayout;
    _form = form;
    form->addRow("Name:", _name);
    form->addRow("Type:", _type);
    form->addRow("Registration:", registrationRow);

    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);

    connect(this, &QDialog::accepted, this, [this]() {
        QSettings settings;
        settings.setValue(kTypeSetting, _type->currentIndex());
        settings.setValue(kRegistrationColumnSetting, _registration->column());
        settings.setValue(kRegistrationRowSetting, _registration->row());
    });

    _name->setFocus();
}

QString ConvertToSymbolDialog::symbolName() const
{
    return _name->text().trimmed();
}

fla::SymbolType ConvertToSymbolDialog::symbolType() const
{
    switch (_type->currentIndex())
    {
    case 1: return fla::SymbolType::Button;
    case 2: return fla::SymbolType::Graphic;
    default: return fla::SymbolType::MovieClip;
    }
}

QPointF ConvertToSymbolDialog::registrationFraction() const
{
    return QPointF(_registration->column() * 0.5, _registration->row() * 0.5);
}

RegistrationGrid::RegistrationGrid(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(sizeHint());
    setCursor(Qt::PointingHandCursor);
}

void RegistrationGrid::setChoice(int column, int row)
{
    _column = std::max(0, std::min(2, column));
    _row = std::max(0, std::min(2, row));
    update();
}

QSize RegistrationGrid::sizeHint() const
{
    const int side = 3 * kBox + 2 * kGap + 2;
    return QSize(side, side);
}

QRect RegistrationGrid::boxRect(int column, int row) const
{
    return QRect(1 + column * (kBox + kGap), 1 + row * (kBox + kGap), kBox, kBox);
}

void RegistrationGrid::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    const QColor line = palette().color(QPalette::Text);
    const QColor fill = palette().color(QPalette::Base);

    // The boxes are joined by thin lines, like the outline of the selection
    // they stand for.
    painter.setPen(QPen(line, 1));
    const QRect outer = boxRect(0, 0).united(boxRect(2, 2));
    painter.drawRect(outer.adjusted(kBox / 2, kBox / 2, -kBox / 2 - 1, -kBox / 2 - 1));
    painter.drawLine(outer.center().x(), outer.top() + kBox / 2, outer.center().x(), outer.bottom() - kBox / 2);
    painter.drawLine(outer.left() + kBox / 2, outer.center().y(), outer.right() - kBox / 2, outer.center().y());

    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            const QRect box = boxRect(column, row);
            const bool chosen = column == _column && row == _row;
            painter.setBrush(chosen ? line : fill);
            painter.drawRect(box.adjusted(0, 0, -1, -1));
        }
    }
}

void RegistrationGrid::mousePressEvent(QMouseEvent* event)
{
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            if (boxRect(column, row).adjusted(-kGap / 2, -kGap / 2, kGap / 2, kGap / 2)
                    .contains(event->pos()))
            {
                setChoice(column, row);
                return;
            }
        }
    }
    QWidget::mousePressEvent(event);
}

void ConvertToSymbolDialog::hideRegistration()
{
    if (_form && _registrationRow)
        _form->setRowVisible(_registrationRow, false);
}
