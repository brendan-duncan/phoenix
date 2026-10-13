#include "preferences.h"

#include "autosave.h"

#include <QCheckBox>
#include "mcp_server.h"
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace Preferences {

namespace {

struct Values
{
    bool loaded = false;
    int undoLevels = 200;
    double clickPixels = 4.0;
    double connectPixels = 15.0;
    bool penPreview = true;
    bool solidPoints = false;
    bool layerHighlight = true;
    QColor highlight = QColor(0, 170, 255);
};

Values& store()
{
    static Values values;
    return values;
}

const Values& current()
{
    if (!store().loaded)
        reload();
    return store();
}

} // namespace

void reload()
{
    QSettings settings;
    Values& v = store();
    v.loaded = true;
    v.undoLevels = settings.value("preferences/undoLevels", 200).toInt();
    v.clickPixels = settings.value("preferences/clickPixels", 4.0).toDouble();
    v.connectPixels = settings.value("preferences/connectPixels", 15.0).toDouble();
    v.penPreview = settings.value("preferences/penPreview", true).toBool();
    v.solidPoints = settings.value("preferences/solidPoints", false).toBool();
    v.layerHighlight = settings.value("preferences/layerHighlight", true).toBool();
    v.highlight = QColor(settings.value("preferences/highlightColor", "#00aaff").toString());
    if (!v.highlight.isValid())
        v.highlight = QColor(0, 170, 255);
}

int undoLevels() { return current().undoLevels; }
double clickPixels() { return current().clickPixels; }
double connectPixels() { return current().connectPixels; }
bool penPreview() { return current().penPreview; }
bool solidPoints() { return current().solidPoints; }
bool layerHighlight() { return current().layerHighlight; }
QColor highlightColor() { return current().highlight; }

} // namespace Preferences

namespace {

QIcon colorIcon(const QColor& color)
{
    QPixmap pixmap(28, 16);
    pixmap.fill(color);
    QPainter painter(&pixmap);
    painter.setPen(QColor(90, 90, 90));
    painter.drawRect(0, 0, 27, 15);
    return QIcon(pixmap);
}

} // namespace

PreferencesDialog::PreferencesDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Preferences");
    QVBoxLayout* layout = new QVBoxLayout(this);

    QGroupBox* general = new QGroupBox("General", this);
    QFormLayout* generalForm = new QFormLayout(general);
    _undoLevels = new QSpinBox(general);
    _undoLevels->setRange(0, 9999);
    _undoLevels->setSpecialValueText("No limit");
    _undoLevels->setValue(Preferences::undoLevels());
    _undoLevels->setToolTip("How many steps Edit > Undo can go back; more keeps more memory");
    generalForm->addRow("Undo levels", _undoLevels);
    _autoSave = new QComboBox(general);
    const int minutes[] = {0, 1, 2, 5, 10};
    for (int m : minutes)
        _autoSave->addItem(m == 0 ? QString("Off") : QString("Every %1 minute%2").arg(m).arg(m == 1 ? "" : "s"), m);
    _autoSave->setCurrentIndex(std::max(0, _autoSave->findData(AutoSave::interval())));
    _autoSave->setToolTip("How often a recovery copy of unsaved work is written");
    generalForm->addRow("Auto-recovery", _autoSave);
    _startScreen = new QCheckBox("Show the start screen", general);
    _startScreen->setChecked(QSettings().value("showStartDialog", true).toBool());
    generalForm->addRow("At startup", _startScreen);
    _reopenDocuments = new QCheckBox("Reopen documents from last time", general);
    _reopenDocuments->setChecked(QSettings().value("reopenDocuments", true).toBool());
    _reopenDocuments->setToolTip("Open the saved documents that were open when Phoenix last quit, each in its tab");
    generalForm->addRow(QString(), _reopenDocuments);
    QWidget* highlightRow = new QWidget(general);
    QHBoxLayout* highlightLayout = new QHBoxLayout(highlightRow);
    highlightLayout->setContentsMargins(0, 0, 0, 0);
    _highlight = new QComboBox(highlightRow);
    _highlight->addItems({"Layer color", "One color"});
    _highlight->setCurrentIndex(Preferences::layerHighlight() ? 0 : 1);
    _color = Preferences::highlightColor();
    _highlightColor = new QToolButton(highlightRow);
    _highlightColor->setIcon(colorIcon(_color));
    _highlightColor->setEnabled(!Preferences::layerHighlight());
    connect(_highlight, &QComboBox::currentIndexChanged, this, [this](int index) { _highlightColor->setEnabled(index == 1); });
    connect(_highlightColor, &QToolButton::clicked, this, [this]() {
        const QColor chosen = QColorDialog::getColor(_color, this, "Highlight Color");
        if (chosen.isValid())
        {
            _color = chosen;
            _highlightColor->setIcon(colorIcon(_color));
        }
    });
    highlightLayout->addWidget(_highlight, 1);
    highlightLayout->addWidget(_highlightColor);
    generalForm->addRow("Highlight", highlightRow);
    layout->addWidget(general);

    QGroupBox* drawing = new QGroupBox("Drawing", this);
    QFormLayout* drawingForm = new QFormLayout(drawing);
    _clickAccuracy = new QComboBox(drawing);
    _clickAccuracy->addItem("Strict", 2.0);
    _clickAccuracy->addItem("Normal", 4.0);
    _clickAccuracy->addItem("Tolerant", 8.0);
    _clickAccuracy->setCurrentIndex(std::max(0, _clickAccuracy->findData(Preferences::clickPixels())));
    _clickAccuracy->setToolTip("How near a click must be to a line or a point to pick it");
    drawingForm->addRow("Click accuracy", _clickAccuracy);
    _connectLines = new QComboBox(drawing);
    _connectLines->addItem("Must be close", 8.0);
    _connectLines->addItem("Normal", 15.0);
    _connectLines->addItem("Can be distant", 30.0);
    _connectLines->setCurrentIndex(std::max(0, _connectLines->findData(Preferences::connectPixels())));
    _connectLines->setToolTip("How near a Straighten pencil line's ends must be to be joined");
    drawingForm->addRow("Connect lines", _connectLines);
    _penPreview = new QCheckBox("Show pen preview", drawing);
    _penPreview->setChecked(Preferences::penPreview());
    _penPreview->setToolTip("The pen draws the next segment to the cursor before you click");
    _solidPoints = new QCheckBox("Show solid points", drawing);
    _solidPoints->setChecked(Preferences::solidPoints());
    drawingForm->addRow("Pen", _penPreview);
    drawingForm->addRow(QString(), _solidPoints);
    layout->addWidget(drawing);

    // AI assistants (MCP clients) attaching to this Phoenix.
    QGroupBox* assistants = new QGroupBox("AI Assistants", this);
    QFormLayout* assistantsForm = new QFormLayout(assistants);
    _mcp = new QCheckBox("Let AI assistants connect (MCP)", assistants);
    _mcp->setChecked(McpServer::enabled());
    _mcp->setToolTip("An AI assistant on this computer, such as Claude Desktop or Claude Code, can read and change "
                     "the open document. Phoenix asks before each one works, and only connections from this "
                     "computer are taken.");
    _mcpPort = new QSpinBox(assistants);
    _mcpPort->setRange(1024, 65535);
    _mcpPort->setValue(McpServer::port());
    _mcpPort->setEnabled(_mcp->isChecked());
    connect(_mcp, &QCheckBox::toggled, _mcpPort, &QSpinBox::setEnabled);
    assistantsForm->addRow("Connections", _mcp);
    assistantsForm->addRow("Port", _mcpPort);
    layout->addWidget(assistants);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void PreferencesDialog::save()
{
    QSettings settings;
    settings.setValue("preferences/undoLevels", _undoLevels->value());
    AutoSave::setInterval(_autoSave->currentData().toInt());
    settings.setValue("showStartDialog", _startScreen->isChecked());
    settings.setValue("reopenDocuments", _reopenDocuments->isChecked());
    settings.setValue("preferences/layerHighlight", _highlight->currentIndex() == 0);
    settings.setValue("preferences/highlightColor", _color.name());
    settings.setValue("preferences/clickPixels", _clickAccuracy->currentData().toDouble());
    settings.setValue("preferences/connectPixels", _connectLines->currentData().toDouble());
    settings.setValue("preferences/penPreview", _penPreview->isChecked());
    settings.setValue("preferences/solidPoints", _solidPoints->isChecked());
    McpServer::setEnabled(_mcp->isChecked(), _mcpPort->value());
    Preferences::reload();
}
