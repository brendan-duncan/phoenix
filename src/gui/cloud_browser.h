#pragma once

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <functional>

class MainWindow;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QToolButton;

/// Phoenix Cloud's files in a window, as its dashboard lists them: Recent,
/// My Files and its folders, Shared with Me, Favorites, each group's files,
/// and a search, with each document's picture.
///
/// It opens a document (File > Phoenix Cloud > Open from Cloud), picks one
/// whose library opens beside this one (File > Import > Open Library from
/// Phoenix Cloud), or picks where a copy goes (Save a Copy to Phoenix Cloud),
/// and makes new documents (New Cloud Document, or New Document here).
class CloudBrowser : public QDialog
{
    Q_OBJECT
public:
    enum class Mode
    {
        Open,
        Library,
        SaveTo,
    };

    /// \a saveTo, in SaveTo mode, gets the name, folder and group (empty for
    /// My Files' top) chosen.
    CloudBrowser(MainWindow* window, Mode mode,
        std::function<void(const QString& name, const QString& folderId, const QString& groupId)> saveTo = {},
        const QString& name = QString());

    /// The New Document window: blank, from a template or a lesson, made in
    /// \a folderId / \a groupId.
    static void newDocument(MainWindow* window, const QString& folderId = QString(), const QString& groupId = QString());

private:
    void showPlace(const QString& view, const QString& group = QString(), const QString& folder = QString());
    void search(const QString& text);
    void fill(const QJsonObject& listing);
    void loadThumbnail(QListWidgetItem* item, const QString& url);
    void choose(QListWidgetItem* item);
    void up();
    void updateButtons();

    MainWindow* _window;
    Mode _mode;
    std::function<void(const QString&, const QString&, const QString&)> _saveTo;
    QListWidget* _places = nullptr;
    QListWidget* _items = nullptr;
    QLineEdit* _search = nullptr;
    QLineEdit* _name = nullptr;
    QLabel* _where = nullptr;
    QLabel* _empty = nullptr;
    QToolButton* _up = nullptr;
    QPushButton* _ok = nullptr;
    QPushButton* _new = nullptr;
    QPushButton* _upload = nullptr;
    /// Where the listing is: its view ("files", "group", "recent"...),
    /// group and folder, and the folder above.
    QString _view;
    QString _group;
    QString _folder;
    QString _parent;
    bool _canAdd = false;
    int _generation = 0;
};
