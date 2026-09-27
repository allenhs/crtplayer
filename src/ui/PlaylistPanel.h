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
    int count() const;
    QString at(int i) const;
    QString labelAt(int i) const;
    int currentIndex() const { return m_current; }
    void setCurrentIndex(int i);
signals:
    void activated(int index);
    void addRequested();
private:
    void restyle();
    QListWidget* m_list;
    int m_current = -1;
};
