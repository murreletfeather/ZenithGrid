#include "QRenderWindowStyle.h"
#include "MeasureManager.h"
#include "SelectManager.h"
#include <vtkObjectFactory.h>
#include <vtkRenderWindowInteractor.h>

vtkStandardNewMacro(QRenderWindowStyle);

void QRenderWindowStyle::SetClick()
{
	click_ = true;
}

void QRenderWindowStyle::SetSelectManager(SelectManager* select_manager)
{
	this->select_manager_ = select_manager;
}

void QRenderWindowStyle::SetMeasureManager(MeasureManager* measure_manager)
{
	this->measure_manager_ = measure_manager;
}

void QRenderWindowStyle::SetMeasureMode(bool on)
{
	this->measure_mode_ = on;
}

void QRenderWindowStyle::OnLeftButtonUp()
{
	if (click_) {
		click_ = false;
		int pos[2];
		this->GetInteractor()->GetEventPosition(pos);
		if (measure_mode_ && measure_manager_)
			measure_manager_->pick(pos[0], pos[1]);
		else if (select_manager_)
			select_manager_->select(pos[0], pos[1]);
	}

	vtkInteractorStyleTrackballCamera::OnLeftButtonUp();
}

void QRenderWindowStyle::OnMouseMove()
{
	vtkInteractorStyleTrackballCamera::OnMouseMove();

	// 仅在无拖拽（非旋转/平移等相机操作）时做测量动态预览
	if (measure_mode_ && measure_manager_ && this->State == VTKIS_NONE) {
		int pos[2];
		this->GetInteractor()->GetEventPosition(pos);
		measure_manager_->hover(pos[0], pos[1]);
		this->GetInteractor()->Render();
	}
}
