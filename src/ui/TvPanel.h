#pragma once
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTreeWidget;
class TvController;

// Cable TV: the channel list and its settings, and the switch for TV mode.
class TvPanel : public QWidget {
    Q_OBJECT
public:
    explicit TvPanel(QWidget* parent = nullptr);
    void setController(TvController* tv);
    void refresh();

signals:
    void tvModeRequested(bool on);

private:
    int selected() const;
    void showSelected();

    TvController* m_tv = nullptr;
    QPushButton* m_power;
    QLabel* m_now;
    QTreeWidget* m_list;
    QLineEdit* m_name;
    QSpinBox* m_number;
    QCheckBox* m_shuffle;
    QLabel* m_bumpers;
    QLabel* m_details;
    QWidget* m_edit;
    bool m_updating = false;
};
