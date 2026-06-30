#pragma once
#include <QString>
#include <QVector>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

// 前向声明（定义在 worker.h 中）
struct ChannelConfig;
struct SerialConfig;
struct SimulationConfig;

class ConfigManager
{
public:
    static ConfigManager &instance();

    bool load(const QString &filePath);

    // ---- 协议 ----
    quint8 frameHeader()  const;
    quint8 frameTail()    const;
    int    frameLength()  const;

    // ---- 通道列表 ----
    QVector<ChannelConfig> channels() const;

    // ---- 报警 ----
    double alarmThreshold() const;

    // ---- 数据库 ----
    int dbBatchSize()       const;
    int dbFlushIntervalMs() const;

    // ---- 串口 ----
    SerialConfig serialConfig() const;

    // ---- 模拟 ----
    SimulationConfig simulationConfig() const;

private:
    ConfigManager() = default;
    QJsonObject m_root;
};
