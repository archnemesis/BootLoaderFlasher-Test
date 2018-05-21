#ifndef FIRMWAREFLASHERWINDOW_H
#define FIRMWAREFLASHERWINDOW_H

#include <QMainWindow>
#include <QSerialPort>
#include <QFile>

namespace Ui {
class FirmwareFlasherWindow;
}

class FirmwareFlasherWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit FirmwareFlasherWindow(QWidget *parent = 0);
    ~FirmwareFlasherWindow();

private slots:
    void on_btnChooseFile_clicked();
    void on_btnWriteFirmware_clicked();
    void on_serialPort_readyRead();

private:
    Ui::FirmwareFlasherWindow *ui;
    QSerialPort *serialPort;
    QFile *firmwareFile;
    int bytesToSend;
    int bytesSent;
    int packetCount;
};

#endif // FIRMWAREFLASHERWINDOW_H
