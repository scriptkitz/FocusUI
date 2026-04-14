// Fill out your copyright notice in the Description page of Project Settings.


#include "FocusUIMaskWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "Blueprint/WidgetLayoutLibrary.h"

UBorder* MakeBorder(UObject* Owner, float MaskOpacity)
{
    UBorder* NewBorder = NewObject<UBorder>(Owner);
    NewBorder->SetVisibility(ESlateVisibility::Collapsed); // 初始隐藏
	NewBorder->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, MaskOpacity)); //黑色遮罩
	return NewBorder;
}

void UFocusUIMaskWidget::NativeConstruct()
{
    Super::NativeConstruct();

    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (!RootCanvas) return;
    RootCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

    if (!MaskImage) return;
    MaskImage->SetColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
    MaskImage->SetVisibility(ESlateVisibility::HitTestInvisible);

	DynamicMaskMaterial = MaskImage->GetDynamicMaterial();

	UCanvasPanelSlot* BgSlot = Cast<UCanvasPanelSlot>(MaskImage->Slot);
    if (BgSlot)
    {
        BgSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f)); // 铺满全屏
        BgSlot->SetOffsets(FMargin(0, 0, 0, 0));
        BgSlot->SetZOrder(-1); // 放在最底层
    }
}

void UFocusUIMaskWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

#if PLATFORM_ANDROID || PLATFORM_IOS
	return; // 移动平台不处理鼠标事件
#endif

    if (!MaskImage) return;

    FVector2D LocalSize = MyGeometry.GetLocalSize();

    FVector2D AbsoluteMousePos = FSlateApplication::Get().GetCursorPos();
    FVector2D LocalMousePos = MyGeometry.AbsoluteToLocal(AbsoluteMousePos);
    bool bInside = IsPointInHole(LocalMousePos, CurrentLocalLTPos, CurrentLocalSize, CurrentType);

    if (LastFrameInHole != bInside)
    {
		LastFrameInHole = bInside;

        ESlateVisibility NewVisibility = bInside ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Visible;
        MaskImage->SetVisibility(NewVisibility);
    }

    if (!LastLocalSize.Equals(LocalSize))
    {
        LastLocalSize = LocalSize;
        bNeedUpdate = true;
    }

    if (bNeedUpdate)
    {
        UpdateWidget(MyGeometry);
        bNeedUpdate = false;
    }
}

FReply UFocusUIMaskWidget::CheckAndHandleInput(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
    FVector2D LocalTouchPos = InGeometry.AbsoluteToLocal(InGestureEvent.GetScreenSpacePosition());
    bool bIsInsideHole = IsPointInHole(LocalTouchPos, CurrentLocalLTPos, CurrentLocalSize, CurrentType);

	return bIsInsideHole ? FReply::Unhandled() : FReply::Handled();
}

FReply UFocusUIMaskWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return CheckAndHandleInput(InGeometry, InMouseEvent);
}

FReply UFocusUIMaskWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    return CheckAndHandleInput(InGeometry, InMouseEvent);
}

FReply UFocusUIMaskWidget::NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
	return CheckAndHandleInput(InGeometry, InGestureEvent);
}
FReply UFocusUIMaskWidget::NativeOnTouchMoved(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
    return CheckAndHandleInput(InGeometry, InGestureEvent);
}
FReply UFocusUIMaskWidget::NativeOnTouchEnded(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
    return CheckAndHandleInput(InGeometry, InGestureEvent);
}


bool UFocusUIMaskWidget::IsPointInHole(FVector2D MousePos, FVector2D LocalLeftTop, FVector2D Size, EFocusShapeType Type)
{
    FVector2D Center = LocalLeftTop + (Size * 0.5f);
	FVector2D HalfSize = Size * 0.5f;

    switch (Type)
    {
    case EFocusShapeType::Rectangle:
        return FMath::Abs(MousePos.X - Center.X) <= HalfSize.X &&
            FMath::Abs(MousePos.Y - Center.Y) <= HalfSize.Y;

    case EFocusShapeType::Ellipse:
        float dx = MousePos.X - Center.X;
        float dy = MousePos.Y - Center.Y;

        return (FMath::Square(dx) * FMath::Square(HalfSize.Y) +
            FMath::Square(dy) * FMath::Square(HalfSize.X)) <=
            (FMath::Square(HalfSize.X) * FMath::Square(HalfSize.Y));
    }
    return false;
}

void UFocusUIMaskWidget::UpdateMask(FVector2D LocalLeftTop, FVector2D Size, EFocusShapeType Type, float Opacity, float Softness, bool bFlashing, FLinearColor EdgeColor)
{
    _fOpacity = Opacity;
	_fSoftness = Softness;
    _bFlashing = bFlashing;
	_EdgeColor = FVector(EdgeColor.R, EdgeColor.G, EdgeColor.B);
    CurrentLocalLTPos = LocalLeftTop;
    CurrentLocalSize = Size;
    CurrentType = Type;

    bNeedUpdate = true;
}

void UFocusUIMaskWidget::UpdateWidget(const FGeometry& MyGeometry)
{
    FVector2D LocalFullSize = MyGeometry.GetLocalSize(); // 一般是全屏

    FVector2D HalfSize = CurrentLocalSize * 0.5;

    if (DynamicMaskMaterial)
    {
        DynamicMaskMaterial->SetScalarParameterValue("Type", static_cast<float>(CurrentType));
        DynamicMaskMaterial->SetScalarParameterValue("Alpha", _fOpacity);
        DynamicMaskMaterial->SetScalarParameterValue("Soft", _fSoftness);
        DynamicMaskMaterial->SetScalarParameterValue("Flash", _bFlashing);
        DynamicMaskMaterial->SetVectorParameterValue("EdgeColor", _EdgeColor);

        DynamicMaskMaterial->SetVectorParameterValue("Rect",
            FLinearColor(
                (CurrentLocalLTPos.X + HalfSize.X) / LocalFullSize.X,
                (CurrentLocalLTPos.Y + HalfSize.Y) / LocalFullSize.Y,
                HalfSize.X / LocalFullSize.X,
                HalfSize.Y / LocalFullSize.Y)
        );
    }
}
