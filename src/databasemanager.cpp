#include "databasemanager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QMutexLocker>

DatabaseManager::DatabaseManager(int batchSize, int flushIntervalMs, QObject *parent)
    : QObject(parent)
    , m_batchSize(batchSize)
{
    m_flushTimer = new QTimer(this);
    m_flushTimer->setInterval(flushIntervalMs);
    connect(m_flushTimer, &QTimer::timeout, this, &DatabaseManager::flush);
}

DatabaseManager::~DatabaseManager()
{
    flush(); // 退出前最后一次刷盘
    m_flushTimer->stop();

    if (m_db.isOpen()) {
        m_db.close();
    }
    qInfo() << ">>> [数据库] DatabaseManager 已安全关闭。";
}

bool DatabaseManager::initialize(const QString &dbPath)
{
    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        qCritical() << ">>> [数据库] 无法打开数据库:" << m_db.lastError().text();
        emit dbError("无法打开数据库: " + m_db.lastError().text());
        return false;
    }

    QSqlQuery query(m_db);
    QString sql = "CREATE TABLE IF NOT EXISTS TempData ("
                  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                  "time TEXT, "
                  "device_id INTEGER, "
                  "channel INTEGER, "
                  "temperature REAL)";

    if (!query.exec(sql)) {
        qCritical() << ">>> [数据库] 建表失败:" << query.lastError().text();
        emit dbError("建表失败: " + query.lastError().text());
        return false;
    }

    // 创建索引加速按时间和通道查询
    query.exec("CREATE INDEX IF NOT EXISTS idx_time ON TempData(time)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_channel ON TempData(channel)");

    m_initialized.store(true);
    m_flushTimer->start();
    qInfo() << ">>> [数据库] 初始化成功，批量大小:" << m_batchSize
            << "刷新间隔:" << m_flushTimer->interval() << "ms";
    return true;
}

void DatabaseManager::enqueue(const TempRecord &record)
{
    QMutexLocker locker(&m_mutex);
    m_buffer.append(record);

    // 缓冲区满了，立即触发刷新
    if (m_buffer.size() >= m_batchSize) {
        locker.unlock(); // flush() 内部也会加锁，先释放
        flush();
    }
}

void DatabaseManager::flush()
{
    QMutexLocker locker(&m_mutex);

    if (m_buffer.isEmpty()) return;

    if (!m_initialized.load()) {
        qWarning() << ">>> [数据库] 尚未初始化，跳过刷盘。";
        return;
    }

    QSqlQuery query(m_db);

    // 开启事务 —— 批量插入的关键性能优化
    if (!m_db.transaction()) {
        qWarning() << ">>> [数据库] 事务开启失败:" << m_db.lastError().text();
    }

    query.prepare("INSERT INTO TempData (time, device_id, channel, temperature) "
                  "VALUES (:time, :dev, :ch, :temp)");

    int written = 0;
    for (const auto &rec : m_buffer) {
        query.bindValue(":time", rec.timeStamp);
        query.bindValue(":dev",  rec.deviceId);
        query.bindValue(":ch",   rec.channel);
        query.bindValue(":temp", rec.temperature);

        if (!query.exec()) {
            qWarning() << ">>> [数据库] 单条写入失败:" << query.lastError().text();
        } else {
            ++written;
        }
    }

    if (!m_db.commit()) {
        qWarning() << ">>> [数据库] 事务提交失败:" << m_db.lastError().text();
        emit dbError("事务提交失败: " + m_db.lastError().text());
    }

    int count = m_buffer.size();
    m_buffer.clear();

    qInfo() << ">>> [数据库] 批量刷盘完成:" << written << "/" << count << "条记录已写入";
    emit flushCompleted(written);
}

int DatabaseManager::pendingCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_buffer.size();
}
