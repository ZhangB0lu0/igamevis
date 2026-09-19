#ifndef iGameCellMeshMetrics_h
#define iGameCellMeshMetrics_h

#include "iGameFilter.h"
#include "iGameVolumeMeshMetricsFilter.h"

IGAME_NAMESPACE_BEGIN
class CellMeshMetricsFilter : public Filter {
public:
    I_OBJECT(CellMeshMetricsFilter);
    static Pointer New() { return new CellMeshMetricsFilter; }

    /// 统一的质量评估指标（对齐 ParaView Cell Quality 的交互）：
    /// 用户只选一个指标，底层按每个单元的【真实单元类型】分发计算，
    /// 该指标对某类单元不适用时填入"不支持值"（默认 -1.0）。
    enum CellQualityMetric {
        QUALITY_EDGE_RATIO,     // 边长比
        QUALITY_VOLUME,         // 单元体积
        QUALITY_ASPECT_RATIO,   // 纵横比/长宽比
        QUALITY_JACOBIAN,       // 雅可比行列式
        QUALITY_SKEW,           // 歪斜度
        QUALITY_MIN_ANGLE,      // 最小内角
        QUALITY_TAPER,          // 锥度
        QUALITY_STRETCH,        // 伸展度
        QUALITY_DIAGONAL,       // 对角线比值
        QUALITY_MAX_EDGE_RATIO, // 最大长宽比
        QUALITY_COLLAPSE_RATIO  // 塌陷率
    };

    bool Execute() override;
    bool Execute(DataObject::Pointer);
    bool Execute(DataObject::Pointer, DataObject::Pointer&);

    // 设置统一评估指标
    void setMetric(CellQualityMetric metric) { m_QualityMetric = metric; }
    CellQualityMetric getMetric() const { return m_QualityMetric; }

    // 设置不支持/无效值（默认 -1.0）
    void setUnsupportedValue(double val) { m_UnsupportedValue = val; }
    double getUnsupportedValue() const { return m_UnsupportedValue; }

    // 统计结果查询接口（供 Qt 弹窗与自动化测试断言）
    int GetSupportedCount() const { return m_SupportedCount; }
    int GetUnsupportedCount() const { return m_UnsupportedCount; }
    int GetSkippedBlockCount() const { return m_SkippedBlockCount; }

protected:
    DataObject::Pointer input;
    DataObject::Pointer output;
    CellMeshMetricsFilter();
    ~CellMeshMetricsFilter() override = default;

    bool ComputeCellMetrics(DataObject::Pointer, DataObject::Pointer&);

    CellQualityMetric m_QualityMetric = QUALITY_EDGE_RATIO;
    // 不支持的 cell 返回值
    double m_UnsupportedValue = -1.0;

    int m_SupportedCount = 0;
    int m_UnsupportedCount = 0;
    int m_SkippedBlockCount = 0;
};

IGAME_NAMESPACE_END

#endif // !iGameCellMeshMetrics_h
