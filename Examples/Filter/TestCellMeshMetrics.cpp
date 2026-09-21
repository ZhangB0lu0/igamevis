#include <Core/iGameScene.h>
#include <MeshMetrics/iGameCellMeshMetricsFilter.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <iGameDrawObject.h>
#include <iGameFileIO.h>
#include <iGameInteractor.h>
#include <iGameRenderWindow.h>
#include <iGameUnstructuredMesh.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

using CellQualityMetric = iGame::CellMeshMetricsFilter::CellQualityMetric;

// 智能模型路径查找（无论在根目录还是构建目录都能自动找到）
std::string FindModelPath(const std::string& modelName) {
    std::vector<std::string> searchPaths = {"./Models/" + modelName, "./Examples/Models/" + modelName,
                                            "../Examples/Models/" + modelName, "../../Examples/Models/" + modelName};
    for (const auto& path: searchPaths) {
        if (std::filesystem::exists(path)) { return path; }
    }
    return "./Models/" + modelName;
}

// 步骤 1 辅助：打印多块树结构
void PrintTreeStructure(iGame::DataObject::Pointer node, int depth = 0) {
    if (!node) return;
    std::string indent(depth * 4, ' ');
    std::string name = node->GetName().empty() ? "(未命名)" : node->GetName();
    if (node->HasSubDataObject()) {
        std::cout << indent << "├── 装配组: [" << name << "] (包含 " << node->GetNumberOfSubDataObjects()
                  << " 个子块)\n";
        for (auto it = node->SubDataObjectIteratorBegin(); it != node->SubDataObjectIteratorEnd(); ++it) {
            PrintTreeStructure(it->second, depth + 1);
        }
    } else {
        auto mesh = iGame::DynamicCast<iGame::UnstructuredMesh>(node);
        std::cout << indent << "└── 实体网格: [" << name << "]"
                  << (mesh ? " (单元数 = " + std::to_string(mesh->GetNumberOfCells()) + ")" : "") << "\n";
    }
}

// ============================================================
// 11 个统一指标 + ParaView 标准答案（由 vtkCellQuality 实测得到）
// 标准答案来源：ParaView 6.1.1 / VTK，cell_metric_assembly_pv.vtm
// ============================================================
struct MetricCase {
    const char* label;    // 中文名 (English)
    const char* paraName; // ParaView 下拉框名称
    CellQualityMetric metric;
    const char* expectTet; // 期望值（四面体块）
    const char* expectHex; // 期望值（六面体块）
};

const std::vector<MetricCase> kMetricCases = {
        {"边长比 (Edge Ratio)", "Edge Ratio", CellQualityMetric::QUALITY_EDGE_RATIO, "1, 1.73205", "1, 1"},
        {"单元体积 (Volume)", "Volume", CellQualityMetric::QUALITY_VOLUME, "0.117851, 0.117851", "1, 1"},
        {"纵横比 (Aspect Ratio)", "Aspect Ratio", CellQualityMetric::QUALITY_ASPECT_RATIO, "1, 2.07313", "-1, -1"},
        {"雅可比行列式 (Jacobian)", "Jacobian", CellQualityMetric::QUALITY_JACOBIAN, "0.707107, 0.707107", "1, 1"},
        {"歪斜度 (Skew)", "Skew", CellQualityMetric::QUALITY_SKEW, "-1, -1", "0, 0"},
        {"最小内角 (Minimum Angle)", "Minimum Angle", CellQualityMetric::QUALITY_MIN_ANGLE, "70.5288, 35.2644",
         "-1, -1"},
        {"锥度 (Taper)", "Taper", CellQualityMetric::QUALITY_TAPER, "-1, -1", "0, 0"},
        {"伸展度 (Stretch)", "Stretch", CellQualityMetric::QUALITY_STRETCH, "-1, -1", "1, 1"},
        {"对角线比值 (Diagonal)", "Diagonal", CellQualityMetric::QUALITY_DIAGONAL, "-1, -1", "1, 1"},
        {"最大长宽比 (Max Edge Ratio)", "Maximum Edge Ratio", CellQualityMetric::QUALITY_MAX_EDGE_RATIO, "-1, -1",
         "1, 1"},
        {"塌陷率 (Collapse Ratio)", "Collapse Ratio", CellQualityMetric::QUALITY_COLLAPSE_RATIO, "0.816496, 0.288675",
         "-1, -1"},
};

// 单个子块的评估结果
struct BlockResult {
    std::string name;
    std::vector<double> values; // 原始值（含不支持值）
    int validCount = 0;
    int skippedCount = 0;
};

// 从输出树中收集各子块的 CellQuality 数组
std::vector<BlockResult> CollectBlockResults(iGame::DataObject::Pointer root, double unsupportedValue) {
    std::vector<BlockResult> results;
    if (!root || !root->HasSubDataObject()) { return results; }

    for (auto it = root->SubDataObjectIteratorBegin(); it != root->SubDataObjectIteratorEnd(); ++it) {
        auto mesh = it->second;
        BlockResult br;
        br.name = mesh ? mesh->GetName() : "(null)";
        auto attrSet = mesh ? mesh->GetAttributeSet() : nullptr;
        if (attrSet) {
            for (int i = 0; i < (int) attrSet->GetNumberOfAttributes(); ++i) {
                auto& attr = attrSet->GetAttribute(i);
                if (attr.isDeleted || !attr.pointer) { continue; }
                if (attr.pointer->GetName() != "CellQuality") { continue; }
                auto arr = attr.pointer;
                for (IGsize c = 0; c < (IGsize) arr->GetNumberOfElements(); ++c) {
                    double v = arr->GetValue(c);
                    br.values.push_back(v);
                    if (std::fabs(v - unsupportedValue) < 1e-9) {
                        ++br.skippedCount;
                    } else {
                        ++br.validCount;
                    }
                }
                break;
            }
        }
        results.push_back(br);
    }
    return results;
}

// 与 ParaView 一致的 %.6g 风格格式化
std::string FormatValues(const std::vector<double>& vals) {
    if (vals.empty()) { return "(无数组)"; }
    std::ostringstream oss;
    oss << std::setprecision(6) << std::defaultfloat;
    for (size_t i = 0; i < vals.size(); ++i) {
        if (i) { oss << ", "; }
        oss << vals[i];
    }
    return oss.str();
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(65001); // Windows 控制台 UTF-8 支持
#endif
    std::cout << std::unitbuf;

    std::cout << "\n==============================================================================\n";
    std::cout << "  【iGameVis】Cell Quality 单元质量评估 —— 与 ParaView 逐值对照测试\n";
    std::cout << "  说明：iGameVis 与 ParaView 使用同一份几何，结果应逐单元完全一致；\n";
    std::cout << "        该指标不适用的单元填入「不支持值」，不计入范围统计。\n";
    std::cout << "==============================================================================\n";

    auto scene = iGame::Scene::New();

    // ========== 步骤 1：读取 .vtm 多块模型 ==========
    std::cout << "\n[步骤 1] 读取多块模型 (.vtm)...\n";
    const std::string vtmPath = FindModelPath("cell_metric_assembly.vtm");
    std::cout << "  iGameVis 侧模型: " << vtmPath << "\n";
    std::cout << "  ParaView 侧模型: Examples/Models/cell_metric_assembly_pv.vtm\n";
    auto multiBlockObj = iGame::FileIO::ReadFile(vtmPath);
    if (!multiBlockObj) {
        std::cerr << "  [错误] 读取多块模型失败: " << vtmPath << "\n";
        return -1;
    }
    std::cout << "  读取成功，多块树结构:\n";
    PrintTreeStructure(multiBlockObj);

    // ========== 步骤 2：循环 11 个统一指标，与 ParaView 标准答案逐值对比 ==========
    std::cout << "\n[步骤 2] 逐个指标评估，并与 ParaView 标准答案逐块、逐单元对照...\n";
    const double kUnsupportedValue = -1.0;
    int passCount = 0;
    int failCount = 0;
    std::vector<std::string> recap; // 每个指标一行，供收尾汇总展示

    // 定宽填充：便于录像时与 ParaView 的 SpreadSheet 逐行对照
    auto pad = [](const std::string& s, size_t w) {
        return s.size() >= w ? s : s + std::string(w - s.size(), ' ');
    };
    const std::string line(96, '-');

    std::cout << "\n  ── ParaView 侧复现方法 ──────────────────────────────────────────────\n";
    std::cout << "    1) File -> Open   Examples/Models/cell_metric_assembly_pv.vtm\n";
    std::cout << "    2) Filters -> Alphabetical -> Cell Quality\n";
    std::cout << "    3) Quality Measure 选择下表中对应的英文名，点击 Apply\n";
    std::cout << "    4) 切到 SpreadSheet View，查看 \"CellQuality\" 列（每块 2 个单元）\n";
    std::cout << "  ─────────────────────────────────────────────────────────────────────\n";

    int metricIndex = 0;
    for (const auto& mc: kMetricCases) {
        ++metricIndex;
        const size_t total = kMetricCases.size();

        std::cout << "\n" << line << "\n";
        std::cout << " [ " << metricIndex << " / " << total << " ]  " << mc.label << "\n";
        std::cout << "             ParaView Quality Measure = \"" << mc.paraName << "\"\n";
        std::cout << line << "\n";

        auto filter = iGame::CellMeshMetricsFilter::New();
        filter->setMetric(mc.metric);
        filter->setUnsupportedValue(kUnsupportedValue);
        filter->SetInput(0, multiBlockObj);
        if (!filter->Execute()) {
            std::cout << "   [FAIL] 执行失败\n";
            ++failCount;
            recap.push_back("[FAIL] " + std::string(mc.label) + "  (执行失败)");
            continue;
        }
        auto outputObj = filter->GetOutput(0);
        if (!outputObj) {
            std::cout << "   [FAIL] 输出为空\n";
            ++failCount;
            recap.push_back("[FAIL] " + std::string(mc.label) + "  (输出为空)");
            continue;
        }

        auto blocks = CollectBlockResults(outputObj, kUnsupportedValue);

        const char* expects[2] = {mc.expectTet, mc.expectHex};
        bool metricOk = true;
        for (int b = 0; b < 2; ++b) {
            std::string bname = (b < (int) blocks.size()) ? blocks[b].name : std::string("(缺块)");
            std::string actual = (b < (int) blocks.size()) ? FormatValues(blocks[b].values) : std::string("(缺块)");
            bool ok = (actual == expects[b]);
            if (!ok) { metricOk = false; }

            std::cout << "   " << pad(bname, 20) << "iGameVis = " << pad(actual, 26)
                      << "|  ParaView = " << pad(expects[b], 26)
                      << (ok ? "[PASS]" : "[FAIL]  <<< 不一致") << "\n";
        }
        std::cout << "   " << pad("", 20) << "统计: 成功计算 = " << filter->GetSupportedCount()
                  << " 个, 跳过不适用 = " << filter->GetUnsupportedCount()
                  << " 个 (不支持值 = " << kUnsupportedValue << ")\n";

        metricOk ? ++passCount : ++failCount;
        recap.push_back(std::string(metricOk ? "[PASS] " : "[FAIL] ") + mc.label +
                        "  (ParaView: " + mc.paraName + ")");
    }

    // ========== 步骤 3：汇总 ==========
    std::cout << "\n" << std::string(96, '=') << "\n";
    std::cout << " 【汇总】iGameVis  CellQuality  vs  ParaView  Cell Quality\n";
    std::cout << std::string(96, '=') << "\n";
    for (const auto& r: recap) { std::cout << "   " << r << "\n"; }
    std::cout << line << "\n";
    std::cout << "   与 ParaView 一致 : " << passCount << " / " << kMetricCases.size() << " 个指标\n";
    std::cout << "   不一致 / 失败    : " << failCount << " 个指标\n";
    std::cout << std::string(96, '=') << "\n";

    // ========== 步骤 4：可视化演示（用 Aspect Ratio 的结果着色） ==========
    std::cout << "\n[步骤 4] 用「纵横比 (Aspect Ratio)」结果做 3D 伪彩演示...\n";
    auto demoFilter = iGame::CellMeshMetricsFilter::New();
    demoFilter->setMetric(CellQualityMetric::QUALITY_ASPECT_RATIO);
    demoFilter->setUnsupportedValue(kUnsupportedValue);
    demoFilter->SetInput(0, multiBlockObj);
    if (demoFilter->Execute()) {
        auto demoOut = demoFilter->GetOutput(0);
        if (demoOut && demoOut->HasSubDataObject()) {
            for (auto it = demoOut->SubDataObjectIteratorBegin(); it != demoOut->SubDataObjectIteratorEnd(); ++it) {
                auto drawObj = iGame::DynamicCast<iGame::DrawObject>(it->second);
                if (!drawObj) { continue; }
                auto attrSet = it->second->GetAttributeSet();
                int qualityIndex = -1;
                for (int i = 0; i < (int) attrSet->GetNumberOfAttributes(); ++i) {
                    auto& attr = attrSet->GetAttribute(i);
                    if (!attr.isDeleted && attr.pointer && attr.pointer->GetName() == "CellQuality") {
                        qualityIndex = i;
                        break;
                    }
                }
                drawObj->SetViewStyle(IG_SURFACE);
                drawObj->AddViewStyle(IG_WIREFRAME);
                drawObj->ConvertToDrawableData();
                if (qualityIndex >= 0) { drawObj->ViewCloudPicture(scene.GetPointer(), qualityIndex); }
                scene->AddModel(it->second);

                auto colorBar = scene->GetColorBar2DActor();
                if (colorBar && drawObj->GetColorMapper()) {
                    colorBar->SetColorMapper(drawObj->GetColorMapper());
                    colorBar->SetTitle("CellQuality");
                    scene->SetColorBarVisible(true);
                }
            }
            std::cout << "  已着色，色条标题 = CellQuality\n";
        }
    }

    std::cout << "\n正在拉起 3D 渲染窗口（左键旋转，滚轮缩放）...\n";
    auto window = iGame::RenderWindow::New();
    window->SetSize(1280, 720);
    window->SetScene(scene);

    auto interactor = iGame::Interactor::New();
    interactor->Initialize(scene);
    interactor->CreateDefaultStyle();
    window->SetInteractor(interactor);

    window->Show();
    return 0;
}
