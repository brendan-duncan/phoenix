#pragma once

#include <QColor>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

class QAbstractButton;
class QAction;
class QIcon;

/// Phoenix's look in one place: named color roles (the window, panels,
/// text, the timeline's frame kinds, the stage's pasteboard and rulers...)
/// that every panel and painted widget reads, so a theme is a set of
/// values for them. Dark and Light come built in, with Follow System, the
/// default, picking between them as the system's mode does; themes of the
/// user's own are small JSON files in the settings folder.
namespace theme {

/// A color role: its key, the area it belongs to and a label for the
/// theme editor, with its value in the Dark and Light themes.
struct Role
{
    const char* key;
    const char* area;
    const char* label;
    QColor dark;
    QColor light;
};

/// Every role, grouped by area in the order the editor shows them.
const std::vector<Role>& roles();

/// A theme: the theme it starts from ("Dark" or "Light") and the roles
/// it sets differently, plus the font size (0 for the system's) and the
/// UI scale (1 for none; used from the next start).
struct Theme
{
    QString name;
    QString base = "Dark";
    QMap<QString, QColor> colors;
    int fontSize = 0;
    double uiScale = 1.0;

    /// A role's value: set here, or the base theme's. Roles a theme made
    /// by an older Phoenix does not have come from its base.
    QColor color(const QString& key) const;

    QJsonObject toJson() const;
    /// \a json read back; \a ok says whether it was a theme at all.
    static Theme fromJson(const QJsonObject& json, bool* ok = nullptr);
};

/// The current theme's value for \a key.
QColor color(const char* key);

/// "Follow System", "Dark", "Light" or the name of a saved theme.
QString currentName();
/// The theme in use, Follow System resolved to Dark or Light.
const Theme& current();
bool isDark();

/// Makes \a name the theme in use (and remembers it), applying it to the
/// whole application live.
void use(const QString& name);
/// Applies \a theme without remembering it, for the editor's preview.
void preview(const Theme& theme);
/// Applies the theme remembered, at the start.
void applySaved();

/// The built-in themes and the saved ones, in menu order.
QStringList names();
QStringList builtInNames();
/// A theme by name, built in or saved.
Theme load(const QString& name);
/// Saves \a theme under its name in the themes folder.
bool save(const Theme& theme);
bool remove(const QString& name);
QString folder();

/// Sets the icon of \a button (or \a action) from \a make, now and again
/// whenever the theme changes, for glyphs drawn in the theme's colors.
void themedIcon(QAbstractButton* button, std::function<QIcon()> make);
void themedIcon(QAction* action, std::function<QIcon()> make);

/// \a color with alpha \a alpha.
QColor withAlpha(QColor color, int alpha);

/// Sends changed() whenever the theme in use changes.
class Notifier : public QObject
{
    Q_OBJECT

signals:
    void changed();
};
Notifier* notifier();

} // namespace theme
