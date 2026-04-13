// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "FocusUIMaskWidget.generated.h"



USTRUCT(BlueprintType)
struct FMaskRectData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial Mask")
	FVector2D Pos = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial Mask")
	FVector2D Siz = FVector2D::ZeroVector;

	// 构造函数
	FMaskRectData() {}
	FMaskRectData(float InX, float InY, float InW, float InH)
		: Pos(InX, InY), Siz(InW, InH) {
	}
	FMaskRectData(FVector2D pos, FVector2D size)
		: Pos(pos), Siz(size) {
	}
};

UENUM(BlueprintType)
enum class EFocusShapeType : uint8 { Rectangle, Ellipse };

/**
 * 
 */
UCLASS()
class FOCUSUI_API UFocusUIMaskWidget : public UUserWidget
{
	GENERATED_BODY()
	
public:
	UPROPERTY(meta = (BindWidget), BlueprintReadOnly, Category = "UI")
	UCanvasPanel* RootCanvas = nullptr;

	UPROPERTY(meta = (BindWidget), BlueprintReadOnly, Category = "UI")
	UImage* MaskImage = nullptr;
protected:
	
	bool LastFrameInHole = false;
	UMaterialInstanceDynamic* DynamicMaskMaterial = nullptr;
	
	virtual void NativeConstruct() override;

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// MouseButton处理按说不需要添加,但为了防止同一帧里切换Visibility时候也触发了鼠标事件情况.
	FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent);
	FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent);

	FReply NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	FReply NativeOnTouchMoved(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	FReply NativeOnTouchEnded(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
private:
	bool bNeedUpdate = false;

	float _fOpacity = 0.7f;
	float _fSoftness = 0.001f;
	bool _bFlashing = false;
	FVector _EdgeColor = FVector(1, 0, 0);
	FVector2D LastLocalSize;
	FVector2D CurrentLocalLTPos; // 左上角坐标不是中心坐标
	FVector2D CurrentLocalSize;
	EFocusShapeType CurrentType;

	FReply CheckAndHandleInput(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent);
	bool IsPointInHole(FVector2D MousePos, FVector2D Center, FVector2D Radius, EFocusShapeType Type);

	void UpdateWidget(const FGeometry& MyGeometry);

public:
	UFUNCTION(BlueprintCallable, Category = "FocusUI")
	void UpdateMask(FVector2D LocalLeftTop, FVector2D Size, EFocusShapeType Type, 
		float Opacity=0.7f, float Softness=0.1f, bool bFlashing=false, UPARAM(meta = (HideAlphaChannel)) FLinearColor EdgeColor = FLinearColor(1,0,0));
};
