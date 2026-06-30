#include "configmanager.h"
#include "worker.h"     // ChannelConfig / SerialConfig / SimulationConfig
#include <QFile>
#include <QDebug>

// ---------- 内置默认值（与 Worker 帧格式匹配）----------
static constexpr quint8  DEFAULT_HEADER     = 0xAA;
static constexpr quint8  DEFAULT_TAIL       = 0x55;
static constexpr int     DEFAULT_FRAME_LEN  = 6;
static constexpr double  DEFAULT_THRESHOLD  = 50.0;
static constexpr int     DEFAULT_BATCH      = 50;
static constexpr int     DEFAULT_FLUSH_MS   = 5000;

ConfigManager &ConfigManager::instance()
{
    static ConfigManager mgr;
    return mgr;
}

bool ConfigManager::load(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << ">>> [配置] 无法打开配置文件:" << filePath;
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError err;
    m_root = QJsonDocument::fromJson(data, &err).object();
    if (err.error != QJsonParseError::NoError) {
        qCritical() << ">>> [配置] JSON 解析失败:" << err.errorString();
        m_root = QJsonObject(); // 清空
        return false;
    }

    qInfo() << ">>> [配置] 配置文件加载成功:" << filePath;
    return true;
}

// ---- 辅助函数：读写 JSON 字段并返回默认值 ----
static quint8 jsonToHexU8(const QJsonObject &obj, const QString &key, quint8 def)
{
    if (obj.isEmpty()) return def;
    QString s = obj[key].toString();
    if (s.isEmpty()) return def;
    return static_cast<quint8>(s.toUInt(nullptr, 16));
}

template<typename T>
static T jsonVal(const QJsonObject &obj, const QString &key, T def)
{
    if (obj.isEmpty()) return def;
    QJsonValue v = obj[key];
    if (v.isUndefined()) return def;
    // 类型匹配
    if constexpr (std::is_same_v<T, int>)
        return static_cast<T>(v.toInt(static_cast<int>(def)));
    else if constexpr (std::is_same_v<T, double>)
        return static_cast<T>(v.toDouble(static_cast<double>(def)));
    else if constexpr (std::is_same_v<T, bool>)
        return static_cast<T>(v.toBool(static_cast<bool>(def)));
    return def;
}

// ==================== 协议 ====================
quint8 ConfigManager::frameHeader() const
{
    return jsonToHexU8(m_root["protocol"].toObject(), "frameHeader", DEFAULT_HEADER);
}

quint8 ConfigManager::frameTail() const
{
    return jsonToHexU8(m_root["protocol"].toObject(), "frameTail", DEFAULT_TAIL);
}

int ConfigManager::frameLength() const
{
    return jsonVal<int>(m_root["protocol"].toObject(), "frameLength", DEFAULT_FRAME_LEN);
}

// ==================== 通道 ====================
QVector<ChannelConfig> ConfigManager::channels() const
{
    QJsonArray arr = m_root["channels"].toArray();

    // 配置文件中没有通道定义 → 使用内置默认3通道
    if (arr.isEmpty()) {
        qInfo() << ">>> [配置] 使用内置默认 3 通道配置。";
        QVector<ChannelConfig> defaults;
        struct { int id; QString name; QString port; QString color; } builtin[] = {
            {0, "主轴", "COM1", "#00FF7F"},
            {1, "电机", "COM2", "#00BFFF"},
            {2, "轴承", "COM3", "#FFA500"},
        };
        for (const auto &d : builtin) {
            ChannelConfig cfg;
            cfg.channelId   = d.id;
            cfg.channelName = d.name;
            cfg.portName    = d.port;
            cfg.lineColor   = d.color;
            defaults.append(cfg);
        }
        return defaults;
    }

    QVector<ChannelConfig> result;
    for (const auto &val : arr) {
        QJsonObject obj = val.toObject();
        ChannelConfig cfg;
        cfg.channelId   = obj["id"].toInt();
        cfg.channelName = obj["name"].toString();
        cfg.portName    = obj["port"].toString();
        cfg.lineColor   = obj["color"].toString();
        result.append(cfg);
    }
    return result;
}

// ==================== 报警 ====================
double ConfigManager::alarmThreshold() const
{
    return jsonVal<double>(m_root["alarm"].toObject(), "threshold", DEFAULT_THRESHOLD);
}

// ==================== 数据库 ====================
int ConfigManager::dbBatchSize() const
{
    return jsonVal<int>(m_root["database"].toObject(), "batchSize", DEFAULT_BATCH);
}

int ConfigManager::dbFlushIntervalMs() const
{
    return jsonVal<int>(m_root["database"].toObject(), "flushIntervalMs", DEFAULT_FLUSH_MS);
}

// ==================== 串口 ====================
SerialConfig ConfigManager::serialConfig() const
{
    QJsonObject s = m_root["serial"].toObject();
    SerialConfig cfg;
    cfg.baudRate           = jsonVal<int>(s, "baudRate", 115200);
    cfg.dataBits           = jsonVal<int>(s, "dataBits", 8);

    QString parityStr      = jsonVal<QString>(s, "parity", "None");
    if (parityStr.isEmpty()) parityStr = "None";
    cfg.parity             = (parityStr == "Even") ? 2 :
                             (parityStr == "Odd")  ? 3 : 0;

    QString stopStr        = jsonVal<QString>(s, "stopBits", "One");
    if (stopStr.isEmpty()) stopStr = "One";
    cfg.stopBits           = (stopStr == "Two") ? 2 : 1;

    cfg.pollIntervalMs     = jsonVal<int>(s, "pollIntervalMs", 1000);
    cfg.reconnectThreshold = jsonVal<int>(s, "reconnectThreshold", 3);
    cfg.reconnectDelayMs   = jsonVal<int>(s, "reconnectDelayMs", 3000);
    return cfg;
}

// ==================== 模拟 ====================
SimulationConfig ConfigManager::simulationConfig() const
{
    QJsonObject s = m_root["simulation"].toObject();
    SimulationConfig cfg;
    cfg.enabled    = jsonVal<bool>(s, "enabled", true);
    cfg.intervalMs = jsonVal<int>(s, "intervalMs", 1000);
    cfg.tempMin    = jsonVal<double>(s, "temperatureMin", 25.0);
    cfg.tempMax    = jsonVal<double>(s, "temperatureMax", 55.0);
    return cfg;
}
