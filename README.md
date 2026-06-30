# 基于 Qt 多线程架构的多通道温控数据采集系统 (SCADA)

![C++](https://img.shields.io/badge/C++-17-blue.svg)
![Qt](https://img.shields.io/badge/Qt-6-green.svg)
![Build](https://img.shields.io/badge/build-passing-brightgreen.svg)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey.svg)

## 📖 项目背景
面向多通道温度传感器的实时采集、可视化监控与异常预警需求，独立设计并实现了一套从底层串口通信、协议解析到上层可视化与管理的一体化温控原型系统。

## 🛠️ 技术栈
- **核心语言与框架：** C++17、Qt 6 (Widgets)
- **底层通信：** 串口通信 (QSerialPort)、自定义十六进制协议
- **数据存储与配置：** SQLite、JSON
- **UI 与可视化：** QCustomPlot (高性能数据可视化)

## ✨ 核心工作与技术亮点

* **🚀 多通道并发轮询引擎**
  为每个通道构建独立采集线程（3 路并发），通过跨线程队列机制将串口 I/O 与 UI 彻底解耦，使用原子变量保证线程间启停安全，彻底解决海量数据吞吐下的主 UI 线程假死问题。

* **📈 协议解析与高性能可视化**
  设计 6 字节十六进制帧校验与位运算提取逻辑。实现 LTTB 降采样算法，在保留关键峰值趋势的同时，保障十万级数据点长时间运行下心电图式图表的平滑滚动。

* **🛡️ 异常熔断与数据持久化**
  实现温度阈值拦截与多重联动响应（视觉警报、线程终止、界面锁定），支持一键复位恢复。采用缓冲区批量事务写入 SQLite，配套历史数据表格查询与曲线回溯，为后期故障追溯提供可靠的现场依据。

* **⚙️ 配置驱动架构与工业 UI 设计**
  将协议参数、通道属性、报警阈值等外置为 JSON 可配文件，内置默认值兜底。定制工业级深色防反光主题与多通道分色曲线；**内置模拟模式，支持无硬件状态下开箱即跑**。

<img width="670" height="427" alt="image" src="https://github.com/user-attachments/assets/acb238cf-5843-46b4-8b46-e0836f4a1d1f" />

<img width="668" height="426" alt="image" src="https://github.com/user-attachments/assets/1ab5efe4-f72d-4b66-b4d3-a5e214db1bd9" />

<img width="712" height="429" alt="image" src="https://github.com/user-attachments/assets/16168669-02c4-4b3c-81d5-a8b256267dc7" />
