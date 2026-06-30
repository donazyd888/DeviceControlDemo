#pragma once
#include <QObject>
#include <QTimer>
#include <QVector>
#include <QSqlDatabase>
#include <QDateTime>
#include <QMutex>
#include <atomic>

// 单条温度记录
struct TempRecord {
    QString timeStamp;
    int     deviceId;
    int     channel;
    double  temperature;
};

// 异步批量写库管理器
// 数据先入缓冲区，由 QTimer 定时批量刷入 SQLite（事务保护）
class DatabaseManager : public QObject
{
    Q_OBJECT

public:
    explicit DatabaseManager(int batchSize = 50,
                             int flushIntervalMs = 5000,
                             QObject *parent = nullptr);
    ~DatabaseManager() override;

    // 初始化数据库连接和建表
    bool initialize(const QString &dbPath = "TemperatureHistory.db");

    // 入队一条温度记录（线程安全，可跨线程调用）
    void enqueue(const TempRecord &record);

    // 手动刷新缓冲区到数据库
    void flush();

    // 缓冲区中待写入的记录数
    int pendingCount() const;

signals:
    void flushCompleted(int recordsWritten);
    void dbError(const QString &message);

private:
    QSqlDatabase       m_db;
    QVector<TempRecord> m_buffer;
    int                m_batchSize;
    QTimer            *m_flushTimer;
    std::atomic<bool>  m_initialized{false};

    // 用 mutable 修饰 mutex 以在 const 函数中使用
    mutable QMutex m_mutex;
};
