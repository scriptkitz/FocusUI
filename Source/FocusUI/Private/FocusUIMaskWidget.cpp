// Fill out your copyright notice in the Description page of Project Settings.


#include "FocusUIMaskWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "Framework/Application/SlateApplication.h"

void UFocusUIMaskWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 画布与背景图自身永不参与命中；拦截由本控件（Visible 时）承担
	if (RootCanvas)
	{
		RootCanvas->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	if (MaskImage)
	{
		MaskImage->SetColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
		MaskImage->SetVisibility(ESlateVisibility::HitTestInvisible); // 只画不挡

		UCanvasPanelSlot* BgSlot = Cast<UCanvasPanelSlot>(MaskImage->Slot);
		if (BgSlot)
		{
			BgSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f)); // 铺满全屏
			BgSlot->SetOffsets(FMargin(0, 0, 0, 0));
			BgSlot->SetZOrder(-1); // 放在最底层
		}

		DynamicMaskMaterial = MaskImage->GetDynamicMaterial();
	}

	SetBlocking(true); // 默认全屏拦截（未设置镂空时洞大小为 0 → 全灰全拦截）
	bNeedUpdate = true;
}

void UFocusUIMaskWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	const FVector2D LocalSize = MyGeometry.GetLocalSize();
	if (LocalSize.X <= KINDA_SMALL_NUMBER || LocalSize.Y <= KINDA_SMALL_NUMBER)
	{
		return; // 几何未就绪，保留 bNeedUpdate 下帧再刷
	}

	// UpdateMaskForWidget 跟踪：用当帧已就绪的 MyGeometry 把目标控件的绝对坐标
	// 换算成本控件本地坐标。与调用时机无关（遮罩可能尚未 AddToViewport），
	// 且目标控件移动/播放动画时镂空框实时跟随
	if (CurrentTargetWidget.IsValid())
	{
		const FGeometry TargetGeo = CurrentTargetWidget->GetCachedGeometry();
		const FVector2D TargetSize = TargetGeo.GetLocalSize();
		if (TargetSize.X > KINDA_SMALL_NUMBER && TargetSize.Y > KINDA_SMALL_NUMBER)
		{
			const FVector2D LocalLT = MyGeometry.AbsoluteToLocal(TargetGeo.LocalToAbsolute(FVector2D::ZeroVector));
			const FVector2D LocalRB = MyGeometry.AbsoluteToLocal(TargetGeo.LocalToAbsolute(TargetSize));
			const FVector2D NewPos = LocalLT;
			const FVector2D NewSize = LocalRB - LocalLT;
			if (!CurrentLocalLTPos.Equals(NewPos, 0.1f) || !CurrentLocalSize.Equals(NewSize, 0.1f))
			{
				CurrentLocalLTPos = NewPos;
				CurrentLocalSize = NewSize;
				bNeedUpdate = true;
			}
		}
	}

	if (!LastLocalSize.Equals(LocalSize, KINDA_SMALL_NUMBER))
	{
		LastLocalSize = LocalSize;
		bNeedUpdate = true;
	}

	// 材质参数（镂空位置/形状/透明度等）在所有平台统一在此刷新
	// （旧实现移动端提前 return，材质 Rect 从未更新 → Android 镂空区域显示不正确）
	if (bNeedUpdate)
	{
		UpdateWidget(MyGeometry);
		bNeedUpdate = false;
	}

#if PLATFORM_ANDROID || PLATFORM_IOS
	// 移动端：派发已排队、且命中网格已重建的合成触摸按下
	DispatchPendingProxyStarts();
#else
	// 桌面：逐帧光标检测 + 精确形状判定，切换本控件命中可见性（保持原方案）
	// 洞内 → 整棵树退出命中，点击自然穿透；洞外 → 本控件可命中并吞掉事件
	const FVector2D MouseLocal = MyGeometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
	SetBlocking(!IsPointInHole(MouseLocal));
#endif
}

bool UFocusUIMaskWidget::IsPointInHole(const FVector2D& LocalPos) const
{
	const FVector2D Center = CurrentLocalLTPos + (CurrentLocalSize * 0.5f);
	const FVector2D HalfSize = CurrentLocalSize * 0.5f;

	switch (CurrentType)
	{
	case EFocusShapeType::Rectangle:
		return FMath::Abs(LocalPos.X - Center.X) <= HalfSize.X &&
			FMath::Abs(LocalPos.Y - Center.Y) <= HalfSize.Y;

	case EFocusShapeType::Ellipse:
	{
		const float dx = LocalPos.X - Center.X;
		const float dy = LocalPos.Y - Center.Y;
		return (FMath::Square(dx) * FMath::Square(HalfSize.Y) +
			FMath::Square(dy) * FMath::Square(HalfSize.X)) <=
			(FMath::Square(HalfSize.X) * FMath::Square(HalfSize.Y));
	}
	}
	return false;
}

void UFocusUIMaskWidget::UpdateWidget(const FGeometry& MyGeometry)
{
	const FVector2D LocalFullSize = MyGeometry.GetLocalSize(); // 一般为全屏
	const FVector2D HalfSize = CurrentLocalSize * 0.5f;

	if (DynamicMaskMaterial)
	{
		DynamicMaskMaterial->SetScalarParameterValue("Type", static_cast<float>(CurrentType));
		DynamicMaskMaterial->SetScalarParameterValue("Alpha", _fOpacity);
		DynamicMaskMaterial->SetScalarParameterValue("LineWidth", _fLineWidth);
		DynamicMaskMaterial->SetScalarParameterValue("Flash", _bFlashing);
		DynamicMaskMaterial->SetVectorParameterValue("EdgeColor", _EdgeColor);
		DynamicMaskMaterial->SetVectorParameterValue("Rect",
			FLinearColor(
				(CurrentLocalLTPos.X + HalfSize.X) / LocalFullSize.X,
				(CurrentLocalLTPos.Y + HalfSize.Y) / LocalFullSize.Y,
				HalfSize.X / LocalFullSize.X,
				HalfSize.Y / LocalFullSize.Y));

		// 控件像素尺寸（材质参数不存在时写入为无害 no-op）。
		// 供材质把 Soft/边缘高亮换算成"屏幕像素等宽"，避免洞越大/越扁边缘越拉伸
		DynamicMaskMaterial->SetVectorParameterValue("MaskFullPx",
			FLinearColor(LocalFullSize.X, LocalFullSize.Y, 0.0f, 0.0f));
	}
}

void UFocusUIMaskWidget::SetBlocking(bool bBlock)
{
	if (bBlocking == bBlock)
	{
		return;
	}
	bBlocking = bBlock;

	// 拦截：本控件可命中；放开：整棵遮罩树退出命中（子控件均为 HitTestInvisible，渲染不受影响）
	SetVisibility(bBlock ? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible);
}

void UFocusUIMaskWidget::DispatchPendingProxyStarts()
{
	for (int32 i = PendingStarts.Num() - 1; i >= 0; --i)
	{
		FPendingProxyStart& Pending = PendingStarts[i];
		if (--Pending.DelayTicks > 0)
		{
			continue; // 等遮罩隐藏后的重绘生效、命中网格重建
		}

		// 与引擎 OnTouchStarted 的构造方式一致：UserIndex/Force 沿用真实事件
		FPointerEvent TouchStart(
			Pending.UserIndex,
			Pending.SyntheticIndex,
			Pending.ScreenPos,
			Pending.ScreenPos,
			Pending.Force,
			true /*bPressLeftMouseButton*/);

		FSlateApplication::Get().ProcessTouchStartedEvent(nullptr, TouchStart);
		StartedSynthetic.Add(Pending.SyntheticIndex);
		PendingStarts.RemoveAt(i);
	}
}

FReply UFocusUIMaskWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// 桌面：命中本控件 = 光标在镂空区域之外。吞掉并捕获，
	// 保证整个按键周期（移动/抬起）都留在遮罩内，不会漏到下层 UI
	return FReply::Handled().CaptureMouse(GetCachedWidget().ToSharedRef());
}

FReply UFocusUIMaskWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// 必须显式释放捕获：UE4 里鼠标捕获不会自动清除，
	// 不释放的话后续所有点击都会继续路由到遮罩（表现为按钮"弹不起来"）
	return FReply::Handled().ReleaseMouseCapture();
}

FReply UFocusUIMaskWidget::NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
#if PLATFORM_ANDROID || PLATFORM_IOS
	const int32 RealIndex = static_cast<int32>(InGestureEvent.GetPointerIndex());
	if (IsSyntheticPointer(RealIndex))
	{
		// 合成触摸又命中遮罩（理论上不应发生）：吞掉即可
		return FReply::Handled();
	}

	if (!IsPointInHole(InGeometry.AbsoluteToLocal(InGestureEvent.GetScreenSpacePosition())))
	{
		// 洞外：拦截并捕获，整根手指的移动/抬起都留在遮罩内
		return FReply::Handled().CaptureMouse(GetCachedWidget().ToSharedRef());
	}

	// 洞内：捕获真实手指，遮罩树退出命中；等命中网格重建后，以合成触摸转发给下层控件
	const int32 SyntheticIndex = NextSyntheticIndex++;
	ProxyFingers.Add(RealIndex, SyntheticIndex);

	FPendingProxyStart Pending;
	Pending.SyntheticIndex = SyntheticIndex;
	Pending.UserIndex = InGestureEvent.GetUserIndex();
	Pending.Force = InGestureEvent.GetTouchForce();
	Pending.ScreenPos = InGestureEvent.GetScreenSpacePosition();
	Pending.DelayTicks = 2; // 至少跨一次重绘（命中网格按帧重建）
	PendingStarts.Add(Pending);

	SetBlocking(false);
	return FReply::Handled().CaptureMouse(GetCachedWidget().ToSharedRef());
#else
	// 桌面：命中本控件 = 光标在洞外（可见性由 NativeTick 控制），吞掉
	return FReply::Handled();
#endif
}

FReply UFocusUIMaskWidget::NativeOnTouchMoved(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
#if PLATFORM_ANDROID || PLATFORM_IOS
	const int32 RealIndex = static_cast<int32>(InGestureEvent.GetPointerIndex());
	if (const int32* SyntheticIndex = ProxyFingers.Find(RealIndex))
	{
		if (StartedSynthetic.Contains(*SyntheticIndex))
		{
			FPointerEvent TouchMove(
				InGestureEvent.GetUserIndex(),
				*SyntheticIndex,
				InGestureEvent.GetScreenSpacePosition(),
				InGestureEvent.GetLastScreenSpacePosition(),
				InGestureEvent.GetTouchForce(),
				true /*bPressLeftMouseButton*/);
			FSlateApplication::Get().ProcessTouchMovedEvent(TouchMove);
		}
		// 合成按下尚未派发（排队中）时，丢弃这次移动
	}
	return FReply::Handled();
#else
	return FReply::Handled();
#endif
}

FReply UFocusUIMaskWidget::NativeOnTouchEnded(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
#if PLATFORM_ANDROID || PLATFORM_IOS
	const int32 RealIndex = static_cast<int32>(InGestureEvent.GetPointerIndex());
	if (const int32* SyntheticIndex = ProxyFingers.Find(RealIndex))
	{
		if (StartedSynthetic.Contains(*SyntheticIndex))
		{
			FPointerEvent TouchEnd(
				InGestureEvent.GetUserIndex(),
				*SyntheticIndex,
				InGestureEvent.GetScreenSpacePosition(),
				InGestureEvent.GetScreenSpacePosition(),
				0.0f /*InForce*/,
				true /*bPressLeftMouseButton*/);
			FSlateApplication::Get().ProcessTouchEndedEvent(TouchEnd);
			StartedSynthetic.Remove(*SyntheticIndex);
		}
		else
		{
			// 手指抬得比合成按下还快：取消排队，本次轻触作废
			const int32 CancelledIndex = *SyntheticIndex;
			PendingStarts.RemoveAll([CancelledIndex](const FPendingProxyStart& Pending)
			{
				return Pending.SyntheticIndex == CancelledIndex;
			});
		}

		ProxyFingers.Remove(RealIndex);
		if (ProxyFingers.Num() == 0)
		{
			SetBlocking(true); // 代理的手指全部抬起，恢复拦截
		}
	}
	// 触摸指针引擎会在结束时自动失效捕获；这里显式释放作为兜底，避免捕获残留
	return FReply::Handled().ReleaseMouseCapture();
#else
	return FReply::Handled().ReleaseMouseCapture();
#endif
}

void UFocusUIMaskWidget::UpdateMask(FVector2D LocalLeftTop, FVector2D Size, EFocusShapeType Type, float Opacity, float LineWidth, bool bFlashing, FLinearColor EdgeColor)
{
	_fOpacity = Opacity;
	_fLineWidth = LineWidth;
	_bFlashing = bFlashing;
	_EdgeColor = FVector(EdgeColor.R, EdgeColor.G, EdgeColor.B);
	CurrentLocalLTPos = LocalLeftTop;
	CurrentLocalSize = Size;
	CurrentType = Type;

	CurrentTargetWidget = nullptr; // 手动坐标：取消 UpdateMaskForWidget 的跟踪

	bNeedUpdate = true;
}

void UFocusUIMaskWidget::UpdateMaskForWidget(UWidget* TargetWidget, EFocusShapeType Type, float Opacity, float LineWidth, bool bFlashing, FLinearColor EdgeColor)
{
	if (!TargetWidget)
	{
		return;
	}

	// 只记录目标与样式，不在此处换算坐标：
	// 调用时遮罩可能尚未 AddToViewport / 未完成首帧布局，GetCachedGeometry() 是空几何，
	// 立即换算会跑飞；位置换算统一放到 NativeTick 用当帧就绪的 MyGeometry 完成
	CurrentTargetWidget = TargetWidget;
	_fOpacity = Opacity;
	_fLineWidth = LineWidth;
	_bFlashing = bFlashing;
	_EdgeColor = FVector(EdgeColor.R, EdgeColor.G, EdgeColor.B);
	CurrentType = Type;

	bNeedUpdate = true;
}
