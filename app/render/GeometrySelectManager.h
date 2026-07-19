#ifndef GEOMETRY_SELECT_MANAGER_H
#define GEOMETRY_SELECT_MANAGER_H
#include "Core.h"
#include "Selection.h"
#include "GeometrySelectorHighlight.h"
#include "GeometryActorSelectOp.h"

#include <memory>
#include <unordered_map>
#include <vtkNew.h>
#include <vtkSmartPointer.h>
#include <vtkActor.h>

class vtkRenderer;
class vtkPartitionedDataSet;
class vtkCompositePolyDataMapper;
class GeometryActorManagerSelectOp;

class GeometrySelectManager {
public:
    GeometrySelectManager(vtkRenderer& renderer, vtkActor& highlight_actor, GeometryActorManagerSelectOp& op);

    void select(double posx, double posy);
    void setSelectMode(SelectMode select_mode);
    void clearSelection();
    std::unique_ptr<Selection> getSelection();

    //! @brief 共享的几何拾取器：选择系统与测量等功能复用同一实例。同一形状在多个
    //! IVtkTools_ShapePicker 实例上激活同一选择模式时，仅最先激活的实例能正常拾取
    IVtkTools_ShapePicker* picker() const;

private:
    GeometrySelectorHighlight* getOrCreateSelector(Index component_id);

    GeometryActorManagerSelectOp* op_;
    SelectMode select_mode_ { SelectMode::None };
    vtkRenderer* renderer_;
    vtkSmartPointer<IVtkTools_ShapePicker> picker_;

    vtkActor* highlight_actor_ {};
    vtkSmartPointer<vtkPartitionedDataSet> highlight_data_;
    vtkSmartPointer<vtkCompositePolyDataMapper> highlight_mapper_;

    std::unordered_map<Index, std::unique_ptr<GeometrySelectorHighlight>> component_selectors_;
};

#endif
