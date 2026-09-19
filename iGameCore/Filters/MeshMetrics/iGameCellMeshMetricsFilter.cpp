#include "iGameCellMeshMetricsFilter.h"

IGAME_NAMESPACE_BEGIN

CellMeshMetricsFilter::CellMeshMetricsFilter() {
    this->SetNumberOfInputs(1);
    this->SetNumberOfOutputs(1);
    input = nullptr;
    output = nullptr;
}

bool CellMeshMetricsFilter::Execute() {
    if (m_Inputs->GetNumberOfElements() == 0) { return false; }
    input = m_Inputs->GetElement(0);
    return Execute(input);
}

bool CellMeshMetricsFilter::Execute(DataObject::Pointer input) {
    if (!input) { return false; }
    this->output = DataObject::New();
    return Execute(input, output);
}

bool CellMeshMetricsFilter::Execute(DataObject::Pointer input, DataObject::Pointer& output) {
    if (!input) { return false; }
    m_SupportedCount = 0;
    m_UnsupportedCount = 0;
    m_SkippedBlockCount = 0;
    bool success = ComputeCellMetrics(input, output);
    if (success && output) {
        this->SetOutput(0, output);
        return true;
    }
    return false;
}

bool CellMeshMetricsFilter::ComputeCellMetrics(DataObject::Pointer input, DataObject::Pointer& output) {
    if (!input) { return false; }

    // Branch node: recursive traversal
    if (input->HasSubDataObject()) {
        // 这里必须用 DrawObject：复合容器传入 Scene 时会被强转成 DrawObject
        DrawObject::Pointer outputContainer = DrawObject::New();
        outputContainer->SetName(input->GetName()); // Preserve name hierarchy

        for (auto it = input->SubDataObjectIteratorBegin(); it != input->SubDataObjectIteratorEnd(); ++it) {
            DataObject::Pointer subOutput;
            if (ComputeCellMetrics(it->second, subOutput)) {
                if (subOutput) { outputContainer->AddSubDataObject(subOutput); }
            } else {
                // Todo::没成功计算怎么办？保留网格，不能静默删除
                if (it->second) { outputContainer->AddSubDataObject(it->second); }
            }
        }
        output = outputContainer;
        return true;
    }
    // Leaf node: compute volume cell quality metrics
    else {
        auto srcMesh = DynamicCast<UnstructuredMesh>(input);
        if (!srcMesh || srcMesh->GetNumberOfCells() == 0) {
            // 块级跳过：不是可处理的非结构网格（可能是 VolumeMesh / SurfaceMesh / 空网格）
            // 原样保留，不静默丢弃；同时计数并在日志中报告块名
            m_SkippedBlockCount++;
            output = input;
            IGAME_CORE_WARN("[CellMeshMetrics] Block skipped: not an UnstructuredMesh or NumberOfCells == 0, name='{}'",
                            input->GetName());
            return true;
        }

        // 1. 创建新网格
        auto outMesh = UnstructuredMesh::New();
        outMesh->SetName(srcMesh->GetName());
        outMesh->SetPoints(srcMesh->GetPoints());                         // 共享只读几何点
        outMesh->SetCells(srcMesh->GetCells(), srcMesh->GetCellTypes());  // 共享拓扑和权威类型
        // 此处浅拷贝函数有问题，未实现
        // outMesh->GetAttributeSet()->ShallowCopy(srcMesh->GetAttributeSet()); // 浅拷贝已有属性
        auto attrSet = srcMesh->GetAttributeSet();
        auto attrAll = attrSet->GetAllAttributes();
        for (auto i = attrAll->Begin(); i < attrAll->End(); i++) { 
            outMesh->GetAttributeSet()->AddAttribute(i->type, i->attachmentType, i->GetPointer());
        }

        IGsize numCells = srcMesh->GetNumberOfCells();
        auto qualityArray = DoubleArray::New();
        qualityArray->SetName("CellQuality");
        qualityArray->Resize(numCells);

        igIndex vhs[IGAME_CELL_MAX_SIZE] = {0};
        // 2. 每个Cell进行计算
        for (IGsize c = 0; c < numCells; c++) {
            IGCellType type = static_cast<IGCellType>(srcMesh->GetCellType(c));
            int vNum = srcMesh->GetCellPointIds(c, vhs);

            // 单元点集
            std::vector<Point> pts;
            pts.reserve(vNum);
            for (int i = 0; i < vNum; i++) {
                pts.push_back(srcMesh->GetPoints()->GetPoint(vhs[i]));
            }
            
            double val = m_UnsupportedValue;
            bool supported = false;
            // 根据真实单元类型，直接调用 VolumeMeshMetricsFilter 的静态数学函数！
            if (type == IG_TETRA && vNum == 4) {
                switch (m_QualityMetric) {
                    case QUALITY_EDGE_RATIO:
                        val = VolumeMeshMetricsFilter::ComputeTetEdgeRatio(pts);
                        supported = true;
                        break;
                    case QUALITY_VOLUME:
                        val = VolumeMeshMetricsFilter::ComputeTetVolume(pts);
                        supported = true;
                        break;
                    case QUALITY_ASPECT_RATIO:
                        val = VolumeMeshMetricsFilter::GetAspectRatioOfCell(pts);
                        supported = true;
                        break;
                    case QUALITY_JACOBIAN:
                        val = VolumeMeshMetricsFilter::GetJacobianOfCell(pts);
                        supported = true;
                        break;
                    case QUALITY_MIN_ANGLE:
                        val = VolumeMeshMetricsFilter::GetMinDihedralAngleOfTet(pts); // ← 原来 GetMinInternalAnglesOfCell
                        supported = true;
                        break;
                    case QUALITY_COLLAPSE_RATIO:
                        val = VolumeMeshMetricsFilter::GetCollapseRatioOfCell(pts);
                        supported = true;
                        break;
                    default:
                        break; // 该指标对四面体不适用
                }
            } else if (type == IG_HEXAHEDRON && vNum == 8) {
                switch (m_QualityMetric) {
                    case QUALITY_EDGE_RATIO:
                        val = VolumeMeshMetricsFilter::ComputeHexEdgeRatio(pts);
                        supported = true;
                        break;
                    case QUALITY_VOLUME:
                        val = VolumeMeshMetricsFilter::ComputeHexVolume(pts);
                        supported = true;
                        break;
                    case QUALITY_JACOBIAN:
                        val = VolumeMeshMetricsFilter::ComputeHexJacobian(pts);
                        supported = true;
                        break;
                    case QUALITY_SKEW:
                        val = VolumeMeshMetricsFilter::ComputeHexSkew(pts);
                        supported = true;
                        break;
                    case QUALITY_TAPER:
                        val = VolumeMeshMetricsFilter::ComputeHexTaper(pts);
                        supported = true;
                        break;
                    case QUALITY_STRETCH:
                        val = VolumeMeshMetricsFilter::ComputeHexStretch(pts);
                        supported = true;
                        break;
                    case QUALITY_DIAGONAL:
                        val = VolumeMeshMetricsFilter::ComputeHexDiagonal(pts);
                        supported = true;
                        break;
                    case QUALITY_MAX_EDGE_RATIO:
                        val = VolumeMeshMetricsFilter::ComputeHexMaxEdgeRatio(pts);
                        supported = true;
                        break;
                    default:
                        break; // 该指标对六面体不适用
                }
            }

            if (supported) {
                qualityArray->SetValue(c, val);
                m_SupportedCount++;
            } else {
                qualityArray->SetValue(c, m_UnsupportedValue);
                m_UnsupportedCount++;
            }
        }
        
        // 3. 加入新网格
        outMesh->GetAttributeSet()->AddAttribute(IG_SCALAR, IG_CELL, qualityArray);
        output = outMesh;
        return true;

    }
}

IGAME_NAMESPACE_END