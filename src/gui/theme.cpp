#include "theme.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QIcon>
#include <QJsonDocument>
#include <QPalette>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleHints>
#include <QWidget>

#include <algorithm>

namespace theme {

namespace {

// An invalid color means the style's own palette color for that scheme,
// so the built-in themes look exactly as the system draws them.
const QColor kNative;

const std::vector<Role> kRoles = {
    {"window", "Window", "Panels and window", kNative, kNative},
    {"base", "Window", "Lists and fields", kNative, kNative},
    {"text", "Window", "Text", kNative, kNative},
    {"dimText", "Window", "Dim text", kNative, kNative},
    {"accent", "Window", "Accent and selection", kNative, kNative},
    {"panelHeader", "Window", "Panel headers", QColor(45, 45, 45), QColor(228, 228, 228)},
    {"icon", "Window", "Icons", QColor(220, 220, 225), QColor(56, 56, 62)},
    {"folder", "Window", "Folder icons", QColor(230, 200, 100), QColor(196, 146, 30)},
    {"hotText", "Window", "Values you drag (hot text)", QColor(86, 156, 255), QColor(0, 95, 204)},

    {"timeline.background", "Timeline", "Background", QColor(30, 30, 30), QColor(236, 236, 236)},
    {"timeline.cell", "Timeline", "Frame cells", QColor(40, 40, 40), QColor(224, 224, 224)},
    {"timeline.cellFifth", "Timeline", "Every fifth cell", QColor(55, 55, 55), QColor(208, 208, 208)},
    {"timeline.text", "Timeline", "Frame numbers", QColor(170, 170, 170), QColor(80, 80, 80)},
    {"frame.empty", "Timeline", "Empty keyframe span", QColor(236, 236, 236), QColor(255, 255, 255)},
    {"frame.static", "Timeline", "Span with content", QColor(196, 196, 196), QColor(186, 186, 186)},
    {"frame.classicTween", "Timeline", "Classic tween", QColor(197, 183, 232), QColor(186, 168, 230)},
    {"frame.shapeTween", "Timeline", "Shape tween", QColor(186, 224, 180), QColor(168, 214, 160)},
    {"frame.motionTween", "Timeline", "Motion tween", QColor(156, 196, 238), QColor(140, 186, 236)},
    {"frame.pose", "Timeline", "IK pose span", QColor(214, 228, 168), QColor(204, 220, 150)},
    {"frame.mark", "Timeline", "Keyframe dots and marks", QColor(20, 20, 20), QColor(20, 20, 20)},
    {"frame.waveform", "Timeline", "Sound waveform", QColor(40, 70, 190), QColor(40, 70, 190)},
    {"playhead", "Timeline", "Playhead and frame selection", QColor(41, 128, 238), QColor(24, 110, 222)},

    {"stage.pasteboard", "Stage", "Pasteboard", kNative, QColor(214, 214, 214)},
    {"stage.ruler", "Stage", "Rulers", QColor(43, 43, 43), QColor(232, 232, 232)},
    {"stage.rulerText", "Stage", "Ruler numbers", QColor(170, 170, 170), QColor(70, 70, 70)},
    {"stage.rulerLine", "Stage", "Ruler edges", QColor(70, 70, 70), QColor(180, 180, 180)},
    {"stage.onionBefore", "Stage", "Onion skin before", QColor(0, 102, 255), QColor(0, 102, 255)},
    {"stage.onionAfter", "Stage", "Onion skin after", QColor(0, 170, 60), QColor(0, 170, 60)},

    {"graph.background", "Graphs", "Background", QColor(36, 36, 36), QColor(250, 250, 250)},
    {"graph.area", "Graphs", "Unit area", QColor(48, 48, 48), QColor(238, 238, 238)},
    {"graph.grid", "Graphs", "Grid", QColor(70, 70, 70), QColor(208, 208, 208)},
    {"graph.text", "Graphs", "Labels", QColor(150, 150, 150), QColor(90, 90, 90)},
};

const QStringList kBuiltIn = {"Follow System", "Dark", "Light", "Turquoise"};

/// Turquoise: an example of a theme that is neither black nor white, dark
/// teal panels with a turquoise accent. Its roles are what a saved theme
/// would set.
Theme turquoise()
{
    Theme theme;
    theme.name = "Turquoise";
    theme.base = "Dark";
    const std::pair<const char*, const char*> colors[] = {
        {"window", "#16292b"},
        {"base", "#0f2022"},
        {"text", "#e2f4f2"},
        {"dimText", "#7fa3a0"},
        {"accent", "#1fb5a8"},
        {"panelHeader", "#1d3638"},
        {"icon", "#cdeeea"},
        {"folder", "#e8c96a"},
        {"hotText", "#48e3d4"},
        {"timeline.background", "#0e1d1f"},
        {"timeline.cell", "#18302f"},
        {"timeline.cellFifth", "#22403f"},
        {"timeline.text", "#8fb8b4"},
        {"playhead", "#2ad4c4"},
        {"stage.pasteboard", "#0c1a1c"},
        {"stage.ruler", "#132a2c"},
        {"stage.rulerText", "#8fb8b4"},
        {"stage.rulerLine", "#2a4a4c"},
        {"graph.background", "#0e1d1f"},
        {"graph.area", "#18302f"},
        {"graph.grid", "#2a4a4c"},
        {"graph.text", "#8fb8b4"},
    };
    for (const auto& [key, value] : colors)
        theme.colors[key] = QColor(value);
    return theme;
}

Theme g_current;
QString g_name = "Follow System";

const Role* roleOf(const QString& key)
{
    for (const Role& role : kRoles)
        if (key == role.key)
            return &role;
    return nullptr;
}

/// The value the base theme gives \a key: its own, or the style's palette.
QColor baseColor(const QString& base, const QString& key)
{
    const Role* role = roleOf(key);
    const QColor own = role ? (base == "Light" ? role->light : role->dark) : QColor();
    if (own.isValid())
        return own;
    const QPalette palette = qApp ? qApp->palette() : QPalette();
    if (key == "window")
        return palette.color(QPalette::Window);
    if (key == "base" || key == "stage.pasteboard")
        return palette.color(QPalette::Base);
    if (key == "text")
        return palette.color(QPalette::WindowText);
    if (key == "dimText")
        return palette.color(QPalette::Disabled, QPalette::Text);
    if (key == "accent")
        return palette.color(QPalette::Highlight);
    return QColor(128, 128, 128);
}

QString fileFor(const QString& name)
{
    QString safe = name;
    for (QChar& c : safe)
        if (!c.isLetterOrNumber() && c != ' ' && c != '-' && c != '_')
            c = '_';
    return QDir(folder()).filePath(safe + ".phxtheme");
}

void apply(const Theme& theme)
{
    g_current = theme;
    if (!qApp)
        return;
    // The style draws in the base theme's mode (the system's, following
    // it); the palette is the system's for that mode with only the
    // theme's own colors set over it.
    if (g_name == "Follow System" && theme.name == "Follow System")
        qApp->styleHints()->unsetColorScheme();
    else
        qApp->styleHints()->setColorScheme(theme.base == "Light" ? Qt::ColorScheme::Light : Qt::ColorScheme::Dark);
    QPalette palette;
    const auto set = [&](const char* key, std::initializer_list<QPalette::ColorRole> roles) {
        if (!theme.colors.contains(key))
            return;
        for (QPalette::ColorRole role : roles)
            palette.setColor(role, theme.colors.value(key));
    };
    set("window", {QPalette::Window, QPalette::Button});
    set("base", {QPalette::Base, QPalette::AlternateBase});
    set("text", {QPalette::WindowText, QPalette::Text, QPalette::ButtonText});
    set("accent", {QPalette::Highlight, QPalette::Accent});
    if (theme.colors.contains("dimText"))
    {
        palette.setColor(QPalette::PlaceholderText, theme.colors.value("dimText"));
        palette.setColor(QPalette::Disabled, QPalette::Text, theme.colors.value("dimText"));
        palette.setColor(QPalette::Disabled, QPalette::WindowText, theme.colors.value("dimText"));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, theme.colors.value("dimText"));
    }
    if (theme.colors.contains("hotText"))
        palette.setColor(QPalette::Link, theme.colors.value("hotText"));
    qApp->setPalette(palette);

    QFont font = qApp->font();
    static const int systemSize = font.pointSize();
    font.setPointSize(theme.fontSize > 0 ? theme.fontSize : systemSize);
    qApp->setFont(font);
    // Widgets with a style sheet hold on to the colors they were polished
    // with; set again, they take the new palette. The rest just redraw.
    for (QWidget* widget : QApplication::allWidgets())
    {
        const QString sheet = widget->styleSheet();
        if (!sheet.isEmpty())
        {
            widget->setStyleSheet(QString());
            widget->setStyleSheet(sheet);
        }
        widget->update();
    }
    emit notifier()->changed();
}

} // namespace

QColor Theme::color(const QString& key) const
{
    const auto it = colors.find(key);
    if (it != colors.end() && it->isValid())
        return *it;
    return baseColor(base, key);
}

QJsonObject Theme::toJson() const
{
    QJsonObject json;
    json["phoenixTheme"] = 1;
    json["name"] = name;
    json["base"] = base;
    QJsonObject values;
    for (auto it = colors.begin(); it != colors.end(); ++it)
        values[it.key()] = it.value().name(it.value().alpha() < 255 ? QColor::HexArgb : QColor::HexRgb);
    json["colors"] = values;
    if (fontSize > 0)
        json["fontSize"] = fontSize;
    if (uiScale != 1.0)
        json["uiScale"] = uiScale;
    return json;
}

Theme Theme::fromJson(const QJsonObject& json, bool* ok)
{
    Theme theme;
    if (ok)
        *ok = json.contains("phoenixTheme");
    theme.name = json["name"].toString();
    theme.base = json["base"].toString() == "Light" ? "Light" : "Dark";
    const QJsonObject values = json["colors"].toObject();
    // Unknown roles (from a newer Phoenix) are kept, harmlessly.
    for (auto it = values.begin(); it != values.end(); ++it)
    {
        const QColor color(it.value().toString());
        if (color.isValid())
            theme.colors[it.key()] = color;
    }
    theme.fontSize = std::clamp(json["fontSize"].toInt(0), 0, 24);
    theme.uiScale = std::clamp(json["uiScale"].toDouble(1.0), 0.75, 2.5);
    return theme;
}

const std::vector<Role>& roles()
{
    return kRoles;
}

QColor color(const char* key)
{
    return g_current.color(key);
}

QString currentName()
{
    return g_name;
}

const Theme& current()
{
    return g_current;
}

bool isDark()
{
    return g_current.base != "Light";
}

QStringList builtInNames()
{
    return kBuiltIn;
}

QString folder()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("themes");
}

QStringList names()
{
    QStringList own;
    for (const QFileInfo& file : QDir(folder()).entryInfoList({"*.phxtheme"}, QDir::Files, QDir::Name))
    {
        QFile in(file.absoluteFilePath());
        if (!in.open(QIODevice::ReadOnly))
            continue;
        bool ok = false;
        const Theme theme = Theme::fromJson(QJsonDocument::fromJson(in.readAll()).object(), &ok);
        if (ok && !theme.name.isEmpty() && !kBuiltIn.contains(theme.name))
            own << theme.name;
    }
    own.sort(Qt::CaseInsensitive);
    return kBuiltIn + own;
}

Theme load(const QString& name)
{
    Theme theme;
    theme.name = name;
    if (name == "Follow System")
    {
        // Whichever mode the system is in now.
        qApp->styleHints()->unsetColorScheme();
        theme.base = qApp->styleHints()->colorScheme() == Qt::ColorScheme::Light ? "Light" : "Dark";
        return theme;
    }
    if (name == "Turquoise")
        return turquoise();
    if (name == "Dark" || name == "Light")
    {
        theme.base = name;
        return theme;
    }
    QFile in(fileFor(name));
    if (in.open(QIODevice::ReadOnly))
    {
        bool ok = false;
        Theme saved = Theme::fromJson(QJsonDocument::fromJson(in.readAll()).object(), &ok);
        if (ok)
        {
            saved.name = name;
            return saved;
        }
    }
    theme.base = "Dark";
    return theme;
}

bool save(const Theme& theme)
{
    if (theme.name.isEmpty() || kBuiltIn.contains(theme.name))
        return false;
    QDir().mkpath(folder());
    QSaveFile out(fileFor(theme.name));
    if (!out.open(QIODevice::WriteOnly))
        return false;
    out.write(QJsonDocument(theme.toJson()).toJson());
    return out.commit();
}

bool remove(const QString& name)
{
    if (kBuiltIn.contains(name))
        return false;
    return QFile::remove(fileFor(name));
}

void use(const QString& name)
{
    g_name = names().contains(name) ? name : QString("Follow System");
    QSettings().setValue("theme/name", g_name);
    QSettings().setValue("theme/uiScale", load(g_name).uiScale);
    apply(load(g_name));
}

void preview(const Theme& theme)
{
    apply(theme);
}

void applySaved()
{
    g_name = QSettings().value("theme/name", "Follow System").toString();
    if (!names().contains(g_name))
        g_name = "Follow System";
    apply(load(g_name));
    // Following the system, a change of its mode changes Phoenix's.
    static bool watching = false;
    if (!watching && qApp)
    {
        watching = true;
        QObject::connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, notifier(), [](Qt::ColorScheme) {
            if (g_name != "Follow System")
                return;
            const QString base = qApp->styleHints()->colorScheme() == Qt::ColorScheme::Light ? "Light" : "Dark";
            if (base != g_current.base)
                apply(load("Follow System"));
        });
    }
}

void themedIcon(QAbstractButton* button, std::function<QIcon()> make)
{
    button->setIcon(make());
    QObject::connect(notifier(), &Notifier::changed, button, [button, make]() { button->setIcon(make()); });
}

void themedIcon(QAction* action, std::function<QIcon()> make)
{
    action->setIcon(make());
    QObject::connect(notifier(), &Notifier::changed, action, [action, make]() { action->setIcon(make()); });
}

QColor withAlpha(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

Notifier* notifier()
{
    static Notifier* instance = new Notifier();
    return instance;
}

} // namespace theme
