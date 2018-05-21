#include "firmwareflasherwindow.h"
#include "ui_firmwareflasherwindow.h"
#include "firmware_metadata.h"
#include "iap.h"

#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QSerialPortInfo>
#include <QMessageBox>
#include <QDebug>

#include <boost/crc.hpp>
#include <boost/cstdint.hpp>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <ostream>

FirmwareFlasherWindow::FirmwareFlasherWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::FirmwareFlasherWindow)
{
    ui->setupUi(this);

    QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    for (QSerialPortInfo port : ports) {
        ui->cboPort->addItem(port.systemLocation());
    }
}

FirmwareFlasherWindow::~FirmwareFlasherWindow()
{
    delete ui;
}

void FirmwareFlasherWindow::on_btnChooseFile_clicked()
{
    QString fileName = QFileDialog::getOpenFileName(this,
        tr("Open Firmware Image"), QDir::homePath(), tr("BIN Files (*.bin)"));

    ui->lneFirmwareFile->setText(fileName);

    if (fileName.length()) {
        QFile fwFile(fileName);

        if (fwFile.exists()) {
            if (fwFile.open(QIODevice::ReadOnly)) {
                FirmwareMetadata_t firmware_meta;
                fwFile.seek(FW_META_OFFSET);
                fwFile.read((char *)&firmware_meta, sizeof(FirmwareMetadata_t));
                fwFile.close();

                ui->lblFirmwareVersion->setText(QString("%1.%2.%3")
                                                .arg(firmware_meta.major)
                                                .arg(firmware_meta.minor)
                                                .arg(firmware_meta.rev));
                ui->lblFileSize->setText(QString("%1 kB").arg(fwFile.size() / 1024));
            }
        }
    }
}

void FirmwareFlasherWindow::on_btnWriteFirmware_clicked()
{
    QSerialPort::Parity par;
    QSerialPort::StopBits stb;
    QSerialPort::DataBits dat;
    QSerialPort::FlowControl flw;

    switch (ui->cboParity->currentIndex()) {
    case 0:
        par = QSerialPort::NoParity;
        break;
    case 1:
        par = QSerialPort::EvenParity;
        break;
    case 2:
        par = QSerialPort::OddParity;
        break;
    }

    switch (ui->cboStopBits->currentIndex()) {
    case 0:
        stb = QSerialPort::OneStop;
        break;
    case 1:
        stb = QSerialPort::TwoStop;
        break;
    }

    switch (ui->cboDataBits->currentIndex()) {
    case 0:
        dat = QSerialPort::Data8;
        break;
    case 1:
        dat = QSerialPort::Data7;
        break;
    case 2:
        dat = QSerialPort::Data6;
        break;
    case 3:
        dat = QSerialPort::Data5;
        break;
    }

    switch (ui->cboFlowControl->currentIndex()) {
    case 0:
        flw = QSerialPort::NoFlowControl;
        break;
    case 1:
        flw = QSerialPort::SoftwareControl;
        break;
    case 2:
        flw = QSerialPort::HardwareControl;
        break;
    }

    serialPort = new QSerialPort(ui->cboPort->currentText());
    serialPort->setBaudRate(ui->cboSpeed->currentText().toInt());
    serialPort->setParity(par);
    serialPort->setStopBits(stb);
    serialPort->setDataBits(dat);
    serialPort->setFlowControl(flw);

    connect(serialPort, &QSerialPort::readyRead, this, &FirmwareFlasherWindow::on_serialPort_readyRead);

    ui->grpFirmware->setEnabled(false);
    ui->grpSerial->setEnabled(false);
    ui->btnWriteFirmware->setEnabled(false);

    if (!serialPort->open(QIODevice::ReadWrite)) {
        QMessageBox::critical(
                    this,
                    tr("Connection Failed"),
                    tr("Unable to open serial port. Check the configuration and try again."),
                    QMessageBox::Ok);
        serialPort->deleteLater();
        serialPort = nullptr;
        return;
    }

    firmwareFile = new QFile(ui->lneFirmwareFile->text());

    if (firmwareFile->open(QIODevice::ReadOnly)) {
        int fwSize = firmwareFile->size();
        int fwChunks = fwSize / IAP_MAX_PACKET_SIZE;
        bytesToSend = fwSize;
        bytesSent = 0;
        packetCount = 0;
        ui->pgbStatusBar->setMinimum(0);
        ui->pgbStatusBar->setMaximum(fwChunks);
        ui->pgbStatusBar->setValue(0);

        IAP_FirmwareHeader_t header;
        header.chunks = fwChunks;
        header.size = fwSize;
        serialPort->putChar(IAP_SOH);
        serialPort->write((const char *)&header, sizeof(IAP_FirmwareHeader_t));
    }
}

void FirmwareFlasherWindow::on_serialPort_readyRead()
{
    unsigned char buffer[1024];
    memset((void *)&buffer[0], 0, 1024);
    IAP_FirmwarePacket_t *pkt;

    if (serialPort->read((char *)&buffer[0], 1) == 1) {
        switch ((unsigned char)buffer[0]) {
        case IAP_ACK:
            qDebug() << "Received ACK from target";

            pkt = (IAP_FirmwarePacket_t *)malloc(sizeof(IAP_FirmwarePacket_t));
            memset((void *)pkt, 0, sizeof(IAP_FirmwarePacket_t));
            pkt->sequence = ++packetCount;

            if (bytesToSend > IAP_MAX_PACKET_SIZE) {
                firmwareFile->read((char *)&pkt->data[0], IAP_MAX_PACKET_SIZE);

                boost::crc_32_type crc32;
                crc32.process_bytes((void *)&pkt->data[0], IAP_MAX_PACKET_SIZE);
                pkt->checksum = (uint32_t)crc32.checksum();

                bytesToSend -= IAP_MAX_PACKET_SIZE;
                bytesSent += IAP_MAX_PACKET_SIZE;
                serialPort->putChar(IAP_SOH);
                serialPort->write((char *)pkt, sizeof(IAP_FirmwarePacket_t));
                ui->pgbStatusBar->setValue(packetCount);
                ui->lblStatusLabel->setText(QString("Transferring %1/%2").arg(bytesSent).arg(firmwareFile->size()));
            }
            else if (bytesToSend > 0) {
                firmwareFile->read((char *)&pkt->data[0], bytesToSend);
                firmwareFile->close();
                firmwareFile->deleteLater();
                firmwareFile = nullptr;

                boost::crc_32_type crc32;
                crc32.process_bytes((void *)&pkt->data[0], IAP_MAX_PACKET_SIZE);
                pkt->checksum = (uint32_t)crc32.checksum();

                serialPort->putChar(IAP_SOH);
                serialPort->write((char *)pkt, bytesToSend + 8);
                ui->pgbStatusBar->setValue(packetCount);
                ui->lblStatusLabel->setText(QString("Transferred %1 bytes").arg(bytesSent + bytesToSend));
                bytesSent = 0;
                bytesToSend = 0;
                packetCount = 0;
            }
            else {
                serialPort->close();
                serialPort->deleteLater();
                serialPort = nullptr;
                ui->grpFirmware->setEnabled(true);
                ui->grpSerial->setEnabled(true);
                ui->btnWriteFirmware->setEnabled(true);
                ui->pgbStatusBar->setValue(0);
            }

            free(pkt);
            break;
        case IAP_NAK:
            qDebug() << "Received NAK from target!";

            firmwareFile->close();
            firmwareFile->deleteLater();
            firmwareFile = nullptr;

            serialPort->close();
            serialPort->deleteLater();
            serialPort = nullptr;

            ui->grpFirmware->setEnabled(true);
            ui->grpSerial->setEnabled(true);
            ui->btnWriteFirmware->setEnabled(true);
            ui->pgbStatusBar->setValue(0);
        }
    }
}
