#pragma once
#include "jellyfin/JellyfinClient.h"
#include <QWidget>

class QLineEdit;
class QPushButton;
class QToolButton;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QComboBox;
class QStackedWidget;

// Sign-in form and library browser for a Jellyfin server.
class JellyfinPanel : public QWidget {
    Q_OBJECT
public:
    JellyfinPanel(JellyfinClient* client, QWidget* parent = nullptr);
    void showHome();
    // Automation / keyboard helpers
    bool openByName(const QString& name);   // folder: navigate; video: play
    QString currentTitle() const;
    int itemCount() const;
    int totalCount() const { return m_total; }   // the whole folder, loaded or not
    bool loadMore();                             // the next page, if any (scrolling does this)
    // Streaming quality: the most the server may send, in Mbit/s (0 = the original file).
    void setMaxBitrateMbps(int mbps, bool notify = false);
    void scrollToEnd();
    bool playConvertedByName(const QString& name);
    int maxBitrateMbps() const;

signals:
    void playRequested(const JfItem& item, bool fromStart);
    void convertedPlayRequested(const JfItem& item);   // "Play converted by the server"
    void maxBitrateChanged(int mbps);
    void enqueueRequested(const JfItem& item);
    void listingChanged();

private:
    void refreshSessionUi();
    void showItems(const QString& title, const QVector<JfItem>& items, const QVector<JfItem>& resume = {});
    void appendItems(const QVector<JfItem>& items, const QString& prefix = QString());
    void updateTitle();
    void maybeLoadMore();
    void navigateInto(const JfItem& folder);
    void goBack();
    void activate(QListWidgetItem* li);
    void contextMenu(const QPoint& pos);
    QIcon placeholderIcon(const JfItem& it) const;
    static QPixmap card(const QPixmap& img, const JfItem& it);

    JellyfinClient* m_client;
    QStackedWidget* m_pages;
    // sign-in page
    QLineEdit* m_server;
    QLineEdit* m_user;
    QLineEdit* m_password;
    QPushButton* m_signIn;
    QLabel* m_status;
    // browser page
    QToolButton* m_back;
    QToolButton* m_home;
    QLineEdit* m_search;
    QLabel* m_title;
    QListWidget* m_list;
    QLabel* m_who;
    QPushButton* m_signOut;
    struct Level { QString parentId, title; };
    QVector<Level> m_stack;
    QString m_pendingParent;
    QString m_listTitle;
    int m_total = 0, m_loaded = 0;    // paging of the folder shown
    bool m_loadingMore = false;
    QComboBox* m_quality;
};
