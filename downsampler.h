#pragma once
#include <QVector>
#include <QPointF>
#include <algorithm>
#include <cmath>

// ============================================================
//  Largest Triangle Three Buckets (LTTB) 降采样算法
//  用于将海量数据点缩减至可渲染数量，同时保留曲线的视觉趋势
//  参考: Sveinn Steinarsson, 2013
// ============================================================
class Downsampler
{
public:
    // 将 data 降采样到最多 threshold 个点
    // 如果 data.size() <= threshold，直接返回原数据
    static QVector<QPointF> downsample(const QVector<QPointF> &data,
                                       int threshold = 800)
    {
        const int n = data.size();
        if (n <= threshold || threshold < 3) {
            return data;
        }

        QVector<QPointF> sampled;
        sampled.reserve(threshold);

        // 第一个点始终保留
        sampled.append(data[0]);

        // 每个桶的大小（除首尾外）
        double bucketSize = static_cast<double>(n - 2) / (threshold - 2);
        int prevBucketIdx = 0; // 上一个选中点所在的桶索引

        for (int i = 1; i < threshold - 1; ++i) {
            int bucketStart = static_cast<int>(i * bucketSize) + 1;
            int bucketEnd   = static_cast<int>((i + 1) * bucketSize) + 1;
            if (bucketEnd >= n - 1) bucketEnd = n - 2;

            // 下一个桶的平均点（用于计算三角形面积）
            int nextBucketStart = bucketEnd;
            int nextBucketEnd   = static_cast<int>((i + 2) * bucketSize) + 1;
            if (nextBucketEnd >= n) nextBucketEnd = n - 1;

            QPointF avgNext(0, 0);
            int cnt = 0;
            for (int j = nextBucketStart; j < nextBucketEnd && j < n; ++j) {
                avgNext += data[j];
                ++cnt;
            }
            if (cnt > 0) {
                avgNext /= cnt;
            }

            // 在当前桶中找面积最大的点
            int bestIdx = bucketStart;
            double bestArea = -1.0;
            const QPointF &prevPoint = sampled.last();

            for (int j = bucketStart; j <= bucketEnd && j < n; ++j) {
                double area = triangleArea(prevPoint, data[j], avgNext);
                if (area > bestArea) {
                    bestArea = area;
                    bestIdx = j;
                }
            }

            sampled.append(data[bestIdx]);
            (void)prevBucketIdx; // 保留变量以备后续扩展
        }

        // 最后一个点始终保留
        sampled.append(data.last());

        return sampled;
    }

private:
    // 计算三个点构成三角形的面积（使用叉积公式，面积 = 0.5 * |AB × AC|）
    static double triangleArea(const QPointF &a, const QPointF &b, const QPointF &c)
    {
        return std::abs((a.x() - c.x()) * (b.y() - a.y()) -
                        (a.x() - b.x()) * (c.y() - a.y())) * 0.5;
    }
};
