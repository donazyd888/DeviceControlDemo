#include "widget.h"
#include "configmanager.h"

#include <QApplication>
#include <QDebug>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    qInfo() << "========================================";
    qInfo() << "  多通道工业温控监控系统 V2.0";
    qInfo() << "  技术栈: Qt 6 + QCustomPlot + SQLite";
    qInfo() << "========================================";

    Widget w;
    w.show();
    return QApplication::exec();
}
