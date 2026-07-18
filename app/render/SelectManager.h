#ifndef SELECT_MANAGER_H
#define SELECT_MANAGER_H
#include "Core.h"
#include "Selection.h"

#include <memory>
#include <string>
#include <vtkNew.h>
#include <vtkActor.h>

class vtkRenderer;
class MeshSelectManager;
class GeometrySelectManager;
class IVtkTools_ShapePicker;
class MeshActorManagerSelectOp;
class GeometryActorManagerSelectOp;
class ComponentSelectorHighlight;

class SelectManager {
public:
    SelectManager(vtkRenderer& renderer,
        MeshActorManagerSelectOp& mesh_op, GeometryActorManagerSelectOp& geom_op);
    ~SelectManager();
    void select(double posx, double posy);
    void setSelectMode(const std::string& select_mode);
    //! @brief 选择系统持有的几何拾取器（供测量等复用，全程序保持单一实例）
    IVtkTools_ShapePicker* geometryPicker() const;
    void clearSelection();
    std::unique_ptr<Selection> getSelection();

private:
    std::unique_ptr<MeshSelectManager> mesh_;
    std::unique_ptr<GeometrySelectManager> geom_;
    std::unique_ptr<ComponentSelectorHighlight> component_selector_;
    SelectMode select_mode_ { SelectMode::None };
    vtkRenderer* renderer_ {};
    vtkNew<vtkActor> highlight_actor_;
};

#endif
