#pragma once
#include <QObject>
#include <QThread>
#include <QDebug>
#include <QString>
#include <QSerialPort>
#include <atomic>

// 通道配置结构体，由 ConfigManager 填充后传给 Worker
struct ChannelConfig {
    int channelId        = 0;
    QString channelName  = "Unknown";
    QString portName     = "COM1";
    QString lineColor    = "#00FF7F";
};

struct SerialConfig {
    int baudRate           = 115200;
    int dataBits           = 8;
    int parity             = 0;   // QSerialPort::NoParity
    int stopBits           = 1;   // QSerialPort::OneStop
    int pollIntervalMs     = 1000;
    int reconnectThreshold = 3;
    int reconnectDelayMs   = 3000;
};

struct SimulationConfig {
    bool enabled     = true;
    int  intervalMs  = 1000;
    double tempMin   = 25.0;
    double tempMax   = 55.0;
};

class Worker : public QObject
{
    Q_OBJECT

public:
    explicit Worker(const ChannelConfig &chCfg,
                    const SerialConfig &serCfg,
                    const SimulationConfig &simCfg,
                    QObject *parent = nullptr);

    void stopWork() {
        m_isWorking.store(false);
    }

private:
    ChannelConfig      m_chCfg;
    SerialConfig       m_serCfg;
    SimulationConfig   m_simCfg;
    std::atomic<bool>  m_isWorking{false};

    // ---------- 真实串口模式 ----------
    void runRealMode();
    bool openSerialPort(QSerialPort &serial);
    int  m_timeoutCounter = 0;   // 连续超时计数

    // ---------- 模拟模式 ----------
    void runSimulationMode();

    // ---------- 通用 ----------
    QByteArray buildFrame(quint8 deviceId, quint8 channel, double temperature);

public slots:
    void doWork();

signals:
    void workFinished();
    void dataReceived(QByteArray rawData);
    void statusMessage(const QString &msg);
};
