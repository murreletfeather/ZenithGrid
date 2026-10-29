#ifndef SOLID_HIDE_FILTER_H
#define SOLID_HIDE_FILTER_H

/**
 * @file SolidHideFilter.h
 * @brief 类型保真的 vtkUnstructuredGrid 整单元移除过滤器（右键隐藏体功能使用）
 *
 * 通用整单元移除过滤器在复制单元属性时存在把整型数组（含拾取身份数组
 * vtkOriginalCellIds，vtkIdTypeArray）提升为 vtkFloatArray 的风险，导致下游
 * vtkIdTypeArray::SafeDownCast 失败、拾取 id 映射错乱。本过滤器改用
 * vtkDataSetAttributes::CopyAllocate(COPYTUPLE)+CopyData 复制单元属性，输出
 * 保持每个数组的原始数据类型。
 *
 * 点集不做压缩重排：输出与输入共享同一份点坐标与点属性（与面/边/体数据共享
 * 组件点集的既有模式一致），仅重建单元连通性；多面体（VTK_POLYHEDRON）的
 * 面拓扑经 GetFaceStream/InsertNextCell 原样保留。
 */

#include <vtkIdTypeArray.h>
#include <vtkUnstructuredGridAlgorithm.h>

class SolidHideFilter : public vtkUnstructuredGridAlgorithm {
public:
    static SolidHideFilter* New();
    vtkTypeMacro(SolidHideFilter, vtkUnstructuredGridAlgorithm);
    void PrintSelf(ostream& os, vtkIndent indent) override;

    //! @brief 待移除单元的 id 数组（输入 vtkUnstructuredGrid 的全局单元 id，无需排序）；nullptr/空数组表示不移除
    void SetHiddenCellIds(vtkIdTypeArray* ids);
    vtkGetObjectMacro(HiddenCellIds, vtkIdTypeArray);

protected:
    SolidHideFilter();
    ~SolidHideFilter() override;

    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

private:
    SolidHideFilter(const SolidHideFilter&) = delete;
    void operator=(const SolidHideFilter&) = delete;

    vtkIdTypeArray* HiddenCellIds {};
};

#endif // SOLID_HIDE_FILTER_H
