#include "widget.h"
#include "worker.h"
#include "configmanager.h"
#include "downsampler.h"
#include "databasemanager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDebug>
#include <QThread>
#include <QDateTime>
#include <QApplication>
#include <QPushButton>
#include <QDir>
#include <QDialog>
#include <QTableView>
#include <QSqlTableModel>
#include <QHeaderView>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QSqlQuery>
#include <QStyledItemDelegate>

// ==================== 构造 ====================
Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , m_fullData(CHANNEL_COUNT)
{
    // 1. 加载配置文件
    ConfigManager &cfg = ConfigManager::instance();
    // 多级路径尝试加载配置文件
    QStringList searchPaths = {
        QApplication::applicationDirPath() + "/config.json",   // 与可执行文件同目录
        QApplication::applicationDirPath() + "/../config.json", // 上级目录
        QDir::currentPath() + "/config.json",                  // 当前工作目录
        "config.json"                                          // 相对路径
    };
    bool loaded = false;
    for (const auto &path : searchPaths) {
        if (cfg.load(path)) {
            loaded = true;
            break;
        }
    }
    if (!loaded) {
        qWarning() << ">>> [配置] 未找到配置文件，使用默认内置参数运行。";
    }

    // 2. 构建 UI
    setupUI();

    // 3. 初始化数据库
    setupDatabase();

    // 4. 启动多通道工作线程
    setupWorkers();
}

// ==================== 析构 —— 安全关闭所有线程 ====================
Widget::~Widget()
{
    stopAllWorkers();

    // 安全关闭所有工作线程并清理 Worker
    for (int i = 0; i < m_threads.size(); ++i) {
        QThread *thread = m_threads[i];
        Worker  *worker = m_workers[i];

        if (thread && worker) {
            // 断开 finished→deleteLater，改为手动管理 Worker 生命周期
            disconnect(thread, &QThread::finished, worker, &QObject::deleteLater);

            thread->quit();
            thread->wait(3000);

            // 线程已完全停止，安全删除 Worker（线程由 Qt 父子树自动销毁）
            delete worker;
            m_workers[i] = nullptr;
        }
    }

    qInfo() << ">>> 主窗口已安全销毁，所有工作线程已退出。";
}

// ==================== UI 构建 ====================
void Widget::setupUI()
{
    // ---- QSS 深色工业风主题 ----
    this->setStyleSheet(
        "QWidget { background-color: #121212; color: #E0E0E0; "
        "font-family: 'Segoe UI', 'Microsoft YaHei'; }"
        "QPushButton { background-color: #1F1F1F; border: 1px solid #333333; "
        "border-radius: 4px; padding: 8px 15px; min-height: 20px; font-weight: bold; }"
        "QPushButton:hover { background-color: #2D2D2D; border-color: #007ACC; }"
        "QPushButton:pressed { background-color: #007ACC; color: white; }"
        "QPushButton:disabled { background-color: #151515; color: #555555; "
        "border-color: #222222; }"
        );

    this->setWindowTitle("多通道工业温控监控系统 V2.0");
    this->resize(900, 550);

    // ---- QCustomPlot 初始化 ----
    m_customPlot = new QCustomPlot(this);
    m_customPlot->setBackground(QBrush(QColor(18, 18, 18)));

    // 从配置读取通道颜色
    QVector<ChannelConfig> channels = ConfigManager::instance().channels();
    for (int i = 0; i < channels.size() && i < CHANNEL_COUNT; ++i) {
        m_customPlot->addGraph();
        QColor color(channels[i].lineColor);
        m_customPlot->graph(i)->setPen(QPen(color, 2));
        m_customPlot->graph(i)->setLineStyle(QCPGraph::lsLine);
        // 设置曲线名称，便于图例显示
        m_customPlot->graph(i)->setName(channels[i].channelName);
    }

    // 坐标轴美化
    QColor gridColor(40, 40, 40);
    m_customPlot->xAxis->grid()->setPen(QPen(gridColor, 1, Qt::DotLine));
    m_customPlot->yAxis->grid()->setPen(QPen(gridColor, 1, Qt::DotLine));
    m_customPlot->xAxis->setBasePen(QPen(Qt::gray, 1));
    m_customPlot->yAxis->setBasePen(QPen(Qt::gray, 1));
    m_customPlot->xAxis->setTickLabelColor(QColor(170, 170, 170));
    m_customPlot->yAxis->setTickLabelColor(QColor(170, 170, 170));
    m_customPlot->xAxis->setLabel("采样序号");
    m_customPlot->yAxis->setLabel("温度 (℃)");
    m_customPlot->xAxis->setLabelColor(QColor(170, 170, 170));
    m_customPlot->yAxis->setLabelColor(QColor(170, 170, 170));

    m_customPlot->xAxis->setRange(0, VISIBLE_WINDOW);
    m_customPlot->yAxis->setRange(0, 60);
    m_customPlot->setMinimumHeight(380);

    // 图例
    m_customPlot->legend->setVisible(true);
    m_customPlot->legend->setTextColor(QColor(200, 200, 200));
    m_customPlot->legend->setBrush(QBrush(QColor(30, 30, 30, 180)));
    m_customPlot->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignTop | Qt::AlignRight);

    // ---- 按钮 ----
    m_btnStart   = new QPushButton("启动多通道轮询", this);
    m_btnStop    = new QPushButton("紧急停止 (STOP)", this);
    m_btnRestart = new QPushButton("🔄 重启系统", this);
    m_btnHistory = new QPushButton("📋 查看历史数据", this);

    m_btnStop->setEnabled(false);
    m_btnRestart->setVisible(false);   // 超温后才显示

    // 重启按钮红色高亮
    m_btnRestart->setStyleSheet(
        "QPushButton { background-color: #8B0000; border-color: #FF4444; color: white; }"
        "QPushButton:hover { background-color: #B22222; }"
        );

    // ---- 布局 ----
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(15, 15, 15, 15);
    layout->setSpacing(10);
    layout->addWidget(m_customPlot);

    // 操作按钮行
    QHBoxLayout *btnRow1 = new QHBoxLayout();
    btnRow1->setSpacing(10);
    btnRow1->addWidget(m_btnStart);
    btnRow1->addWidget(m_btnStop);
    btnRow1->addWidget(m_btnRestart);
    layout->addLayout(btnRow1);

    // 历史数据按钮单独一行
    layout->addWidget(m_btnHistory);
}

// ==================== 数据库初始化 ====================
void Widget::setupDatabase()
{
    ConfigManager &cfg = ConfigManager::instance();
    m_dbManager = new DatabaseManager(cfg.dbBatchSize(),
                                      cfg.dbFlushIntervalMs(),
                                      this);
    if (!m_dbManager->initialize("TemperatureHistory.db")) {
        qCritical() << ">>> 数据库初始化失败！";
    }
}

// ==================== 多通道工作线程初始化 ====================
void Widget::setupWorkers()
{
    ConfigManager &cfg = ConfigManager::instance();
    QVector<ChannelConfig> channels = cfg.channels();

    int channelCount = qMin(channels.size(), CHANNEL_COUNT);
    m_threads.resize(channelCount);
    m_workers.resize(channelCount);

    for (int i = 0; i < channelCount; ++i) {
        // 创建线程
        m_threads[i] = new QThread(this);

        // 创建 Worker，传入该通道的配置
        m_workers[i] = new Worker(channels[i],
                                  cfg.serialConfig(),
                                  cfg.simulationConfig());
        m_workers[i]->moveToThread(m_threads[i]);

        // ---- 连接信号 ----
        int chId = channels[i].channelId;

        // 数据到达 → 主线程处理
        connect(m_workers[i], &Worker::dataReceived, this,
                [this, chId](const QByteArray &rawData) {
                    processIncomingData(chId, rawData);
                });

        // 工作结束
        connect(m_workers[i], &Worker::workFinished, this, [this, i]() {
            qInfo() << ">>> [主线程] Worker" << i << "工作完成。";
            if (!m_alarmTriggered) {
                m_btnStart->setEnabled(true);
                m_btnStop->setEnabled(false);
            }
        });

        // 状态消息
        connect(m_workers[i], &Worker::statusMessage, this,
                [](const QString &msg) {
                    qInfo() << ">>> [状态]" << msg;
                });

        // 线程结束时清理 Worker
        connect(m_threads[i], &QThread::finished,
                m_workers[i], &QObject::deleteLater);

        // 启动线程
        m_threads[i]->start();
    }

    // ---- 按钮信号 ----
    // 启动按钮 → 所有 Worker 同时开始
    connect(m_btnStart, &QPushButton::clicked, this, [this]() {
        if (m_alarmTriggered) return;

        m_btnStart->setText("轮询中...");
        m_btnStart->setEnabled(false);
        m_btnStop->setEnabled(true);

        for (auto *worker : m_workers) {
            // 使用 QMetaObject::invokeMethod 确保跨线程调用安全
            QMetaObject::invokeMethod(worker, "doWork", Qt::QueuedConnection);
        }

        qInfo() << ">>> [主线程] 所有通道轮询已启动。线程ID:" << QThread::currentThreadId();
    });

    // 停止按钮 → 所有 Worker 安全停止
    connect(m_btnStop, &QPushButton::clicked, this, [this]() {
        stopAllWorkers();
        setControlsEnabled(true, false);
    });

    // 重启按钮 → 恢复系统正常状态
    connect(m_btnRestart, &QPushButton::clicked, this, &Widget::restartSystem);

    // 历史数据按钮 → 弹窗查看
    connect(m_btnHistory, &QPushButton::clicked, this, &Widget::showHistoryDialog);
}

// ==================== 数据处理核心 ====================
void Widget::processIncomingData(int channelId, const QByteArray &rawData)
{
    const int FRAME_LEN = ConfigManager::instance().frameLength();
    const quint8 HEAD   = ConfigManager::instance().frameHeader();
    const quint8 TAIL   = ConfigManager::instance().frameTail();
    const double ALARM  = ConfigManager::instance().alarmThreshold();

    if (rawData.size() < FRAME_LEN) {
        qWarning() << ">>> [协议] 数据长度不足:" << rawData.size();
        return;
    }

    // 协议解析：6 字节帧
    quint8 head     = static_cast<quint8>(rawData.at(0));
    quint8 deviceId = static_cast<quint8>(rawData.at(1));
    quint8 chNum    = static_cast<quint8>(rawData.at(2));
    quint8 dataHigh = static_cast<quint8>(rawData.at(3));
    quint8 dataLow  = static_cast<quint8>(rawData.at(4));
    quint8 tail     = static_cast<quint8>(rawData.at(5));

    // 帧头尾校验
    if (head != HEAD || tail != TAIL) {
        qWarning() << ">>> [协议] 帧校验失败: head=" << Qt::hex << head
                   << "tail=" << tail;
        return;
    }

    // 温度解析
    quint16 rawTemp = (static_cast<quint16>(dataHigh) << 8) | dataLow;
    double realTemp = rawTemp / 10.0;

    qInfo() << ">>> [解析] 设备:" << deviceId
            << "通道:" << chNum
            << "温度:" << realTemp << "℃";

    // 通道号有效性检查
    if (chNum < 0 || chNum >= CHANNEL_COUNT) {
        qWarning() << ">>> [协议] 无效通道号:" << chNum;
        return;
    }

    // ============ 正常数据追加（先记录，再判断报警）============
    // 数据点始终追加到全量存储和图表，包括超温点
    m_fullData[chNum].append(QPointF(m_totalDataCount, realTemp));

    // 异步写入数据库
    TempRecord rec;
    rec.timeStamp   = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    rec.deviceId    = deviceId;
    rec.channel     = chNum;
    rec.temperature = realTemp;
    m_dbManager->enqueue(rec);

    // 刷新图表
    refreshChart();
    m_totalDataCount++;

    // ============ 超温报警判断（数据已记录后再触发）============
    if (realTemp >= ALARM) {
        triggerAlarm(chNum, realTemp);
    }
}

// ==================== 图表刷新（应用下采样） ====================
void Widget::refreshChart()
{
    for (int ch = 0; ch < CHANNEL_COUNT; ++ch) {
        const auto &full = m_fullData[ch];
        if (full.isEmpty()) continue;

        // LTTB 降采样：从全量数据中提取不超过 DOWNSAMPLE_TARGET 个点
        QVector<QPointF> displayData = Downsampler::downsample(full, DOWNSAMPLE_TARGET);

        // 更新曲线数据（替换全部数据点以保证性能）
        m_customPlot->graph(ch)->setData(
            QVector<double>(),   // x
            QVector<double>(),   // y — 先清空
            true                 // 已排序
            );

        // 逐点设置降采样后的数据
        QVector<double> xs, ys;
        xs.reserve(displayData.size());
        ys.reserve(displayData.size());
        for (const auto &pt : displayData) {
            xs.append(pt.x());
            ys.append(pt.y());
        }
        m_customPlot->graph(ch)->setData(xs, ys, true);
    }

    // 滚动 X 轴窗口
    if (m_totalDataCount > VISIBLE_WINDOW) {
        m_customPlot->xAxis->setRange(m_totalDataCount - VISIBLE_WINDOW,
                                       m_totalDataCount + 50);
    }

    m_customPlot->replot();
}

// ==================== 超温报警 ====================
void Widget::triggerAlarm(int channelId, double temperature)
{
    m_alarmTriggered = true;

    qCritical() << ">>> [🚨 紧急制动 🚨] 通道" << channelId
                << "严重超温！当前温度:" << temperature << "℃";

    // 1. 视觉警报：红色背景
    m_customPlot->setBackground(QBrush(QColor(120, 20, 20)));
    m_customPlot->replot();

    // 2. 停止所有通道的轮询
    stopAllWorkers();

    // 3. 切换 UI：隐藏常规按钮，显示重启按钮
    m_btnStart->setVisible(false);
    m_btnStop->setVisible(false);
    m_btnRestart->setVisible(true);
    m_btnRestart->setText(QString("🔄 重启系统（通道%1 超温 %2℃）")
                          .arg(channelId).arg(temperature, 0, 'f', 1));

    // 4. 记录报警到数据库
    TempRecord rec;
    rec.timeStamp   = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    rec.deviceId    = 1;
    rec.channel     = channelId;
    rec.temperature = temperature;
    m_dbManager->enqueue(rec);
    m_dbManager->flush(); // 紧急情况立即落盘
}

// ==================== 重启系统 ====================
void Widget::restartSystem()
{
    m_alarmTriggered = false;

    // 恢复图表背景
    m_customPlot->setBackground(QBrush(QColor(18, 18, 18)));
    m_customPlot->replot();

    // 恢复按钮状态
    m_btnStart->setText("启动多通道轮询");
    m_btnStart->setVisible(true);
    m_btnStart->setEnabled(true);
    m_btnStop->setVisible(true);
    m_btnStop->setEnabled(false);
    m_btnRestart->setVisible(false);

    qInfo() << ">>> [系统] 已复位，可重新启动轮询。";
}

// ==================== 历史数据查看 ====================
void Widget::showHistoryDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle("历史温度数据");
    dlg.resize(700, 450);
    dlg.setStyleSheet(
        "QDialog { background-color: #1A1A1A; }"
        "QTableView { background-color: #121212; color: #E0E0E0; "
        "gridline-color: #333333; selection-background-color: #007ACC; "
        "alternate-background-color: #1E1E1E; }"
        "QHeaderView::section { background-color: #2A2A2A; color: #CCCCCC; "
        "padding: 4px; border: 1px solid #333333; }"
        "QLabel { color: #AAAAAA; }"
        );

    QVBoxLayout *dlgLayout = new QVBoxLayout(&dlg);

    QLabel *title = new QLabel("数据库记录（最近 500 条，按时间倒序）");
    title->setStyleSheet("font-size: 13px; font-weight: bold;");
    dlgLayout->addWidget(title);

    // 使用 QSqlTableModel 绑定数据库表
    QSqlTableModel *model = new QSqlTableModel(&dlg);
    model->setTable("TempData");
    model->setSort(0, Qt::DescendingOrder);  // 按 id 倒序（最新在前）
    model->setEditStrategy(QSqlTableModel::OnManualSubmit);
    model->select();

    // 设置列标题
    model->setHeaderData(1, Qt::Horizontal, "时间");
    model->setHeaderData(2, Qt::Horizontal, "设备 ID");
    model->setHeaderData(3, Qt::Horizontal, "通道");
    model->setHeaderData(4, Qt::Horizontal, "温度 (℃)");

    // 隐藏 id 列
    QTableView *view = new QTableView();
    view->setModel(model);
    view->hideColumn(0);
    view->setAlternatingRowColors(true);

    // 全部单元格居中显示
    class CenterDelegate : public QStyledItemDelegate {
    public:
        using QStyledItemDelegate::QStyledItemDelegate;
        void initStyleOption(QStyleOptionViewItem *option,
                             const QModelIndex &index) const override {
            QStyledItemDelegate::initStyleOption(option, index);
            option->displayAlignment = Qt::AlignCenter;
        }
    };
    view->setItemDelegate(new CenterDelegate(view));
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setSortingEnabled(true);
    view->verticalHeader()->setVisible(false);

    // 手动设置列宽：时间列自适应拉伸，其余列固定宽度
    QHeaderView *header = view->horizontalHeader();
    header->setSectionResizeMode(1, QHeaderView::Interactive);      // 时间列
    header->setSectionResizeMode(2, QHeaderView::Fixed);        // 设备ID
    header->setSectionResizeMode(3, QHeaderView::Fixed);        // 通道
    header->setSectionResizeMode(4, QHeaderView::Fixed);        // 温度
    view->setColumnWidth(1, 200);
    view->setColumnWidth(2, 80);   // 设备 ID
    view->setColumnWidth(3, 60);   // 通道
    view->setColumnWidth(4, 100);  // 温度 (℃)

    // 限制显示行数
    while (model->rowCount() > 500) {
        model->removeRow(model->rowCount() - 1);
    }

    dlgLayout->addWidget(view);

    // ---- 底部按钮栏 ----
    QHBoxLayout *bottomRow = new QHBoxLayout();

    // 清空数据按钮
    QPushButton *btnClear = new QPushButton("🗑 清空数据");
    btnClear->setStyleSheet(
        "QPushButton { background-color: #5C1A1A; border-color: #AA3333; color: #FF8888; }"
        "QPushButton:hover { background-color: #7A2222; }"
        );
    connect(btnClear, &QPushButton::clicked, &dlg, [&dlg, model]() {
        QMessageBox::StandardButton ret = QMessageBox::warning(
            &dlg, "确认清空",
            "确定要删除全部历史温度记录吗？\n此操作不可撤销！",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (ret == QMessageBox::Yes) {
            QSqlQuery query;
            query.exec("DELETE FROM TempData");
            model->select(); // 刷新视图
        }
    });
    bottomRow->addWidget(btnClear);

    // 折线图按钮
    QPushButton *btnChart = new QPushButton("📈 折线图展示");
    bottomRow->addWidget(btnChart);
    connect(btnChart, &QPushButton::clicked, &dlg, [this]() {
        showHistoryChart();
    });

    bottomRow->addStretch();

    // 关闭按钮
    QDialogButtonBox *btnBox = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    bottomRow->addWidget(btnBox);

    dlgLayout->addLayout(bottomRow);

    dlg.exec();
}

// ==================== 历史数据折线图 ====================
void Widget::showHistoryChart()
{
    // 从数据库读取各通道数据
    QVector<QVector<QPointF>> channelData(CHANNEL_COUNT);
    int maxIndex = 0;

    QSqlQuery query;
    query.exec("SELECT channel, temperature FROM TempData ORDER BY id ASC");

    while (query.next()) {
        int ch     = query.value(0).toInt();
        double tmp = query.value(1).toDouble();

        if (ch >= 0 && ch < CHANNEL_COUNT) {
            channelData[ch].append(QPointF(maxIndex, tmp));
        }
        ++maxIndex;
    }

    if (maxIndex == 0) {
        QMessageBox::information(nullptr, "无数据", "数据库中暂无历史记录。");
        return;
    }

    // 构建折线图对话框
    QDialog dlg;
    dlg.setWindowTitle("历史温度曲线");
    dlg.resize(750, 420);
    dlg.setStyleSheet("QDialog { background-color: #1A1A1A; }");

    QVBoxLayout *layout = new QVBoxLayout(&dlg);

    QCustomPlot *plot = new QCustomPlot();
    plot->setBackground(QBrush(QColor(18, 18, 18)));

    // 读取通道颜色配置
    QVector<ChannelConfig> channels = ConfigManager::instance().channels();
    for (int i = 0; i < qMin(channels.size(), CHANNEL_COUNT); ++i) {
        plot->addGraph();
        QColor color(channels[i].lineColor);
        plot->graph(i)->setPen(QPen(color, 1.5));
        plot->graph(i)->setName(channels[i].channelName);

        // 对历史数据也做降采样（数据量可能很大）
        QVector<QPointF> display = Downsampler::downsample(channelData[i], 800);

        QVector<double> xs, ys;
        xs.reserve(display.size());
        ys.reserve(display.size());
        for (const auto &pt : display) {
            xs.append(pt.x());
            ys.append(pt.y());
        }
        plot->graph(i)->setData(xs, ys, true);
    }

    // 坐标轴样式
    QColor gridColor(40, 40, 40);
    plot->xAxis->grid()->setPen(QPen(gridColor, 1, Qt::DotLine));
    plot->yAxis->grid()->setPen(QPen(gridColor, 1, Qt::DotLine));
    plot->xAxis->setBasePen(QPen(Qt::gray, 1));
    plot->yAxis->setBasePen(QPen(Qt::gray, 1));
    plot->xAxis->setTickLabelColor(QColor(170, 170, 170));
    plot->yAxis->setTickLabelColor(QColor(170, 170, 170));
    plot->xAxis->setLabel("记录序号");
    plot->yAxis->setLabel("温度 (℃)");
    plot->xAxis->setLabelColor(QColor(170, 170, 170));
    plot->yAxis->setLabelColor(QColor(170, 170, 170));
    plot->yAxis->setRange(0, 60);
    plot->xAxis->setRange(0, qMax(maxIndex, 1));

    // 图例
    plot->legend->setVisible(true);
    plot->legend->setTextColor(QColor(200, 200, 200));
    plot->legend->setBrush(QBrush(QColor(30, 30, 30, 180)));
    plot->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignTop | Qt::AlignRight);

    layout->addWidget(plot);

    QDialogButtonBox *btnBox = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(btnBox);

    dlg.exec();
}

// ==================== 辅助方法 ====================
void Widget::stopAllWorkers()
{
    for (auto *worker : m_workers) {
        if (worker) {
            worker->stopWork();
        }
    }
}

void Widget::setControlsEnabled(bool startEnabled, bool stopEnabled)
{
    m_btnStart->setEnabled(startEnabled);
    m_btnStop->setEnabled(stopEnabled);
}
