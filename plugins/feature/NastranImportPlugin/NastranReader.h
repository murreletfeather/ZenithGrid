/**
 * @file NastranReader.h
 * @brief 读取 Nastran Bulk Data 中的节点与网格单元。
 */
#pragma once

#include <filesystem>
#include <memory>

#include "JobProgress.h"

struct MeshData;

namespace systems::feature {

/**
 * @brief 读取并校验完整网格，返回持有坐标与连接关系的组件数据。
 * @param path Nastran Bulk Data 文本文件。
 * @param report 任务进度与取消检查回调。
 * @return 单个共享节点索引的网格。
 */
std::unique_ptr<MeshData> readNastranMesh(const std::filesystem::path& path, const systems::job::ProgressFn& report);

}
