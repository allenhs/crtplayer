#pragma once
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QEvent>
#include <QSlider>
#include <QWidget>

// Scrolling a long settings panel must scroll the panel, not silently change whichever
// slider happens to be under the pointer. Value widgets only take the wheel once they
// have focus (after a click); otherwise the event is ignored and Qt propagates it to
// the enclosing scroll area.
class WheelGuard : public QObject {
public:
    using QObject::QObject;
    static void install(QWidget* root)
    {
        auto* guard = new WheelGuard(root);
        const auto widgets = root->findChildren<QWidget*>();
        for (QWidget* w : widgets) {
            if (qobject_cast<QSlider*>(w) || qobject_cast<QAbstractSpinBox*>(w) || qobject_cast<QComboBox*>(w)) {
                w->setFocusPolicy(Qt::StrongFocus);
                w->installEventFilter(guard);
            }
        }
    }
protected:
    bool eventFilter(QObject* o, QEvent* e) override
    {
        if (e->type() == QEvent::Wheel) {
            auto* w = static_cast<QWidget*>(o);
            if (!w->hasFocus()) {
                e->ignore();   // not accepted -> QApplication propagates it to the parent scroll area
                return true;
            }
        }
        return QObject::eventFilter(o, e);
    }
};
