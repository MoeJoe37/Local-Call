#include "UiTheme.h"

#include <QAbstractButton>
#include <QApplication>
#include <QFile>
#include <QSize>
#include <QStyle>
#include <QWidget>

namespace UiTheme {

bool apply()
{
    QFile file(QStringLiteral(":/theme/localcall.qss"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    qApp->setStyleSheet(QString::fromUtf8(file.readAll()));
    return true;
}

QIcon icon(const QString& name)
{
    return QIcon(QStringLiteral(":/icons/") + name + QStringLiteral(".png"));
}

void applyIcon(QAbstractButton* button, const QString& iconName, int px)
{
    if (!button) return;
    button->setIcon(icon(iconName));
    button->setIconSize(QSize(px, px));
}

void setClass(QWidget* widget, const QString& className)
{
    if (!widget) return;
    widget->setProperty("class", className);
    if (QStyle* style = widget->style()) {
        style->unpolish(widget);
        style->polish(widget);
    }
}

}  // namespace UiTheme
