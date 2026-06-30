#include "worker.h"
#include <QRandomGenerator>
#include <QDateTime>

Worker::Worker(const ChannelConfig &chCfg,
               const SerialConfig &serCfg,
               const SimulationConfig &simCfg,
               QObject *parent)
    : QObject{parent}
    , m_chCfg(chCfg)
    , m_serCfg(serCfg)
    , m_simCfg(simCfg)
{
    m_isWorking.store(false);
}

// ==================== 主入口：doWork() ====================
void Worker::doWork()
{
    m_isWorking.store(true);
    qInfo() << ">>> [子线程] Worker 启动 — 通道:" << m_chCfg.channelId
            << m_chCfg.channelName << "线程ID:" << QThread::currentThreadId();

    if (m_simCfg.enabled) {
        runSimulationMode();
    } else {
        runRealMode();
    }

    qInfo() << ">>> [子线程] Worker 退出 — 通道:" << m_chCfg.channelId;
    emit workFinished();
}

// ==================== 模拟模式 ====================
void Worker::runSimulationMode()
{
    qInfo() << ">>> [模拟模式] 通道" << m_chCfg.channelId
            << "启动模拟数据生成，间隔" << m_simCfg.intervalMs << "ms";

    // 各通道基准温度不同，让三条曲线自然分散
    static const double baseline[3] = { 38.0, 35.0, 42.0 };
    double base = baseline[m_chCfg.channelId % 3];

    while (m_isWorking.load()) {
        // 模拟真实工况：基准温度 + 小幅随机波动（±3℃）
        double noise = (QRandomGenerator::global()->generateDouble() - 0.5) * 6.0;
        double temp = base + noise;

        // 偶发超温尖峰（约 3% 概率），用于验证报警功能
        if (QRandomGenerator::global()->bounded(100) < 3) {
            temp = m_simCfg.tempMax - 2.0 +
                   QRandomGenerator::global()->generateDouble() * 5.0; // 53~58℃
        }

        temp = qBound(20.0, temp, 60.0);

        QByteArray frame = buildFrame(1, m_chCfg.channelId, temp);
        emit dataReceived(frame);

        QThread::msleep(m_simCfg.intervalMs);
    }
}

// ==================== 真实串口模式 ====================
void Worker::runRealMode()
{
    qInfo() << ">>> [真实模式] 通道" << m_chCfg.channelId
            << "尝试连接串口:" << m_chCfg.portName;

    QSerialPort serial;
    m_timeoutCounter = 0;

    // 首次尝试打开串口
    if (!openSerialPort(serial)) {
        qWarning() << ">>> [串口] 初始连接失败，将进入重连循环...";
    }

    while (m_isWorking.load()) {
        // 如果串口未打开，尝试重连
        if (!serial.isOpen()) {
            qInfo() << ">>> [重连] 通道" << m_chCfg.channelId
                    << "等待" << m_serCfg.reconnectDelayMs << "ms后重试...";
            QThread::msleep(m_serCfg.reconnectDelayMs);

            if (!m_isWorking.load()) break;

            if (openSerialPort(serial)) {
                m_timeoutCounter = 0;
            }
            continue;
        }

        // ---- 发送请求帧 ----
        QByteArray request = buildFrame(1, m_chCfg.channelId, 0.0);
        serial.write(request);
        if (!serial.waitForBytesWritten(1000)) {
            qWarning() << ">>> [串口] 发送超时 — 通道:" << m_chCfg.channelId;
            m_timeoutCounter++;
        } else {
            // ---- 等待回复 ----
            if (serial.waitForReadyRead(3000)) {
                QByteArray response = serial.readAll();
                qInfo() << ">>> [串口] 收到回复 — 通道:" << m_chCfg.channelId
                        << "数据:" << response.toHex(' ');
                emit dataReceived(response);
                m_timeoutCounter = 0;   // 成功 → 超时计数归零
            } else {
                qWarning() << ">>> [串口] 接收超时 — 通道:" << m_chCfg.channelId;
                m_timeoutCounter++;
            }
        }

        // ---- 断线重连判断 ----
        if (m_timeoutCounter >= m_serCfg.reconnectThreshold) {
            qCritical() << ">>> [串口] 连续超时" << m_timeoutCounter
                        << "次，触发断线重连 — 通道:" << m_chCfg.channelId;
            emit statusMessage(QString("通道%1 断线，正在重连...").arg(m_chCfg.channelId));
            serial.close();
            m_timeoutCounter = 0;
            // 下一轮循环自动进入重连逻辑
            continue;
        }

        // 轮询间隔
        QThread::msleep(m_serCfg.pollIntervalMs);
    }

    if (serial.isOpen()) {
        serial.close();
    }
}

bool Worker::openSerialPort(QSerialPort &serial)
{
    serial.setPortName(m_chCfg.portName);
    serial.setBaudRate(m_serCfg.baudRate);
    serial.setDataBits(static_cast<QSerialPort::DataBits>(m_serCfg.dataBits));
    serial.setParity(static_cast<QSerialPort::Parity>(m_serCfg.parity));
    serial.setStopBits(static_cast<QSerialPort::StopBits>(m_serCfg.stopBits));

    if (serial.open(QIODevice::ReadWrite)) {
        qInfo() << ">>> [串口]" << m_chCfg.portName << "打开成功 — 通道:"
                << m_chCfg.channelId;
        emit statusMessage(QString("通道%1 串口已连接").arg(m_chCfg.channelId));
        return true;
    } else {
        qWarning() << ">>> [串口] 无法打开" << m_chCfg.portName
                   << "—" << serial.errorString();
        return false;
    }
}

// ==================== 构造协议帧 ====================
QByteArray Worker::buildFrame(quint8 deviceId, quint8 channel, double temperature)
{
    // 帧格式: [HEAD 1B] [DEV_ID 1B] [CH 1B] [TEMP_H 1B] [TEMP_L 1B] [TAIL 1B]
    quint16 rawTemp = static_cast<quint16>(temperature * 10.0);

    QByteArray frame;
    frame.resize(6);
    frame[0] = 0xAA;              // 帧头
    frame[1] = deviceId;          // 设备 ID
    frame[2] = channel;           // 通道号
    frame[3] = (rawTemp >> 8) & 0xFF;  // 温度高字节
    frame[4] = rawTemp & 0xFF;         // 温度低字节
    frame[5] = 0x55;              // 帧尾

    return frame;
}
