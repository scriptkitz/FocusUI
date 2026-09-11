// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanel.h"
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
 * 全屏镂空指引遮罩（纯插件实现，不依赖任何引擎修改）。
 *
 * 形状判定（矩形/椭圆）在两端共用同一个 IsPointInHole：
 *
 * 桌面（Windows）——保持原方案：
 *   逐帧检测光标位置，洞内 → 本控件树退出命中，点击自然穿透到下层 UI；
 *   洞外 → 本控件可命中，鼠标事件一律吞掉。
 *
 * 移动（Android/iOS）：
 *   本控件始终可命中，触摸落下时用形状判定：
 *   洞外 → 吞掉；
 *   洞内 → 捕获真实手指，遮罩树临时退出命中（下一帧命中网格重建后生效），
 *   以"合成触摸"（独立指针索引）转发给下层控件；拖动/抬起全程代理转发，
 *   保证圆形/椭圆判定精确，不依赖任何近似遮挡条。
 *
 * 限制：某根手指在洞内拖动期间（遮罩命中已放开），新落下的其他手指不会被拦截。
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

	/**
	 * 更新镂空区域。
	 * @param LocalLeftTop 镂空框左上角，本控件本地坐标（UMG 视口单位，非物理像素；
	 *                     Android 上按 GetViewportSize 坐标系计算即可，勿用原始分辨率像素）。
	 *                     注意：其他控件的 GetLocalTopLeft 返回的是"相对其父控件"的坐标，
	 *                     与本控件本地坐标不一定同空间；跨层级定位请用 UpdateMaskForWidget。
	 * @param Size         镂空框尺寸（本地坐标）
	 * @param Type         镂空形状（矩形 / 椭圆）
	 */
	UFUNCTION(BlueprintCallable, Category = "FocusUI")
	void UpdateMask(FVector2D LocalLeftTop, FVector2D Size, EFocusShapeType Type,
		float Opacity = 0.7f, float LineWidth = 4.0f, bool bFlashing = false,
		UPARAM(meta = (HideAlphaChannel)) FLinearColor EdgeColor = FLinearColor(1, 0, 0));

	/**
	 * 便捷接口：直接以某个控件（任意层级）的几何区域作为镂空框。
	 * 只记录目标与样式，位置换算在 NativeTick 里用当帧就绪的几何完成：
	 * 与调用时机无关（先 AddToViewport 还是后 AddToViewport 都正确），
	 * 且目标控件移动/播放动画时镂空框实时跟随。
	 * @param TargetWidget 镂空框覆盖的目标控件
	 */
	UFUNCTION(BlueprintCallable, Category = "FocusUI")
	void UpdateMaskForWidget(UWidget* TargetWidget, EFocusShapeType Type,
		float Opacity = 0.7f, float LineWidth = 4.0f, bool bFlashing = false,
		UPARAM(meta = (HideAlphaChannel)) FLinearColor EdgeColor = FLinearColor(1, 0, 0));

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 能走到这些回调 = 命中在镂空区域之外（桌面由可见性控制，移动由形状判定控制），一律吞掉
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	virtual FReply NativeOnTouchMoved(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	virtual FReply NativeOnTouchEnded(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;

private:
	// ---- 镂空状态 ----
	UMaterialInstanceDynamic* DynamicMaskMaterial = nullptr; // 实例挂在 MaskImage 画刷上，GC 由画刷兜底

	// UpdateMaskForWidget 的目标控件（弱引用防悬垂）；tick 实时换算其几何。
	// 调用手动坐标版 UpdateMask 时置空以取消跟踪
	UPROPERTY(Transient)
	TWeakObjectPtr<UWidget> CurrentTargetWidget;

	bool bNeedUpdate = true;   // 首次 Tick 也刷一遍（旧实现移动端提前 return，材质参数从未更新）
	bool bBlocking = false;    // 当前是否拦截命中（切换可见性时避免重复 SetVisibility）
	float _fOpacity = 0.7f;
	float _fLineWidth = 4.0f;
	bool _bFlashing = false;
	FVector _EdgeColor = FVector(1, 0, 0);
	FVector2D LastLocalSize;
	FVector2D CurrentLocalLTPos; // 镂空框左上角（本地坐标，非中心）
	FVector2D CurrentLocalSize;
	EFocusShapeType CurrentType = EFocusShapeType::Rectangle;

	// ---- 移动端触摸代理（合成转发） ----
	struct FPendingProxyStart
	{
		int32 SyntheticIndex = 0;
		int32 UserIndex = 0;
		float Force = 1.0f;
		int32 DelayTicks = 0; // 命中网格按帧重建，须等遮罩隐藏后的重绘生效再派发
		FVector2D ScreenPos = FVector2D::ZeroVector;
	};
	static constexpr int32 SyntheticIndexBase = 100000; // 合成触摸指针索引起点，避开真实手指
	TMap<int32, int32> ProxyFingers;      // 真实手指索引 -> 合成触摸索引
	TSet<int32> StartedSynthetic;         // 已派发按下、可继续转发的合成索引
	TArray<FPendingProxyStart> PendingStarts; // 待派发的合成"按下"
	int32 NextSyntheticIndex = SyntheticIndexBase;

	bool IsSyntheticPointer(int32 PointerIndex) const { return PointerIndex >= SyntheticIndexBase; }
	bool IsPointInHole(const FVector2D& LocalPos) const;
	void UpdateWidget(const FGeometry& MyGeometry);
	void SetBlocking(bool bBlock);
	void DispatchPendingProxyStarts();
};
