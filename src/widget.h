#pragma once

#include <QWidget>
#include <QThread>
#include <QVector>
#include <QPushButton>
#include <QPointF>
#include "qcustomplot.h"

class Worker;
class DatabaseManager;
class QPushButton;

class Widget : public QWidget
{
    Q_OBJECT

public:
    explicit Widget(QWidget *parent = nullptr);
    ~Widget() override;

private:
    // ---- 渲染参数 ----
    static constexpr int DOWNSAMPLE_TARGET = 800;
    static constexpr int VISIBLE_WINDOW    = 500;
    static constexpr int CHANNEL_COUNT     = 3;

    // ---- 全量数据存储（每通道独立）----
    QVector<QVector<QPointF>> m_fullData;

    // ---- 多线程组件 ----
    QVector<QThread*> m_threads;
    QVector<Worker*>  m_workers;

    // ---- UI 组件 ----
    QCustomPlot  *m_customPlot;
    QPushButton  *m_btnStart;
    QPushButton  *m_btnStop;
    QPushButton  *m_btnRestart;   // 超温后重启
    QPushButton  *m_btnHistory;   // 查看历史数据

    // ---- 数据库 ----
    DatabaseManager *m_dbManager;

    // ---- 状态 ----
    bool m_alarmTriggered = false;
    int  m_totalDataCount = 0;

    // ---- 方法 ----
    void setupUI();
    void setupWorkers();
    void setupDatabase();
    void processIncomingData(int channelId, const QByteArray &rawData);
    void refreshChart();
    void triggerAlarm(int channelId, double temperature);
    void restartSystem();
    void showHistoryDialog();
    void showHistoryChart();
    void stopAllWorkers();
    void setControlsEnabled(bool startEnabled, bool stopEnabled);
};
