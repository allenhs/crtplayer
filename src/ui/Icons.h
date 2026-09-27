#pragma once
#include <QIcon>
#include <QString>

// Vector icons painted at runtime, so the UI does not depend on the desktop icon theme.
namespace Icons {
QIcon get(const QString& name);
}
