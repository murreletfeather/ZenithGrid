/**
 * @file MeshRepairHandler.h
 * @brief 网格修复功能处理器：基于 CGAL PMP 的补洞、自交检测与退化清理
 */
#ifndef MESH_REPAIR_HANDLER_H
#define MESH_REPAIR_HANDLER_H

#include "FeatureHandler.h"

namespace systems::feature {

/**
 * @brief 基于 CGAL Polygon Mesh Processing 的网格修复功能
 *
 * 支持三种操作：孔洞填补（三角化边界环）、自相交面检测、退化面清理。
 * 操作经 Combo 参数选择，结果以字符串形式返回。
 */
class MeshRepairHandler : public FeatureHandler {
public:
    /**
     * @brief 注册操作类型参数与功能菜单
     */
    void setup(FeatureRegistrar& reg) override;

    /**
     * @brief 执行所选修复操作并返回结果文本
     */
    std::any execute(FeatureContext& ctx) override;
};

} // namespace systems::feature

#endif // MESH_REPAIR_HANDLER_H
