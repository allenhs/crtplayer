#pragma once
#include <QStringList>
#include <QWidget>
class QListWidget;

class PlaylistPanel : public QWidget {
    Q_OBJECT
public:
    explicit PlaylistPanel(QWidget* parent = nullptr);
    QStringList items() const;
    void setItems(const QStringList& paths);
    int addItems(const QStringList& paths);   // returns index of the first added item
    void replaceAt(int i, const QStringList& paths);   // entry i gives way to these, in its place
    int count() const;
    QString at(int i) const;
    QString labelAt(int i) const;
    int currentIndex() const { return m_current; }
    void setCurrentIndex(int i);
    void setShuffle(bool on);
    void setRepeatMode(int mode);   // 0 off, 1 the whole playlist, 2 this video
    void click(const QString& button);   // tests: "shuffle", "repeat", "clear"
signals:
    void activated(int index);
    void addRequested();
    void shuffleChanged(bool on);
    void repeatModeChanged(int mode);
    void saveRequested();
    void changed();                 // items added, removed or reordered
private:
    class QToolButton* m_shuffle = nullptr;
    class QToolButton* m_repeat = nullptr;
    class QToolButton* m_clear = nullptr;
    int m_repeatMode = 0;
    void restyle();
    QListWidget* m_list;
    int m_current = -1;
};
