#include "firmwareflasherwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    FirmwareFlasherWindow w;
    w.show();

    return a.exec();
}
