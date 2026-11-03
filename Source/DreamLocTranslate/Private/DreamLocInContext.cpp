// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocInContext.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Debugging/SlateDebugging.h"
#include "DreamLocTextLocator.h"
#include "DreamLocTranslateModule.h"
#include "DreamLocTranslateTypes.h"
#include "Features/IModularFeatures.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "HAL/IConsoleManager.h"
#include "Internationalization/TextNamespaceUtil.h"
#include "Layout/WidgetPath.h"
#include "Rendering/SlateRenderer.h"
#include "SDreamLocTranslateWindow.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DreamLocInContext"

namespace DreamLocInContextLocal
{
	TAutoConsoleVariable<int32> CVarInContext(TEXT("DreamLoc.InContext"), 1,
		TEXT("Alt over a text in the editor outlines it; Alt+click opens the DreamLoc translation window at it. 0: off."));

	const FName TextBlockType(TEXT("STextBlock"));
	const FName RichTextBlockType(TEXT("SRichTextBlock"));

	TOptional<DreamLocInContext::FFound> Highlighted;
	int32 HighlightUser = INDEX_NONE;
	FString Hint;
	FVector2D HintAt = FVector2D::ZeroVector;
	TWeakPtr<SWindow> HintWindow;
	double HintUntil = 0.0;
	TFunction<void(const DreamLocInContext::FFound&)> OnPick;
	FDelegateHandle PaintHandle;

	bool IsEnabled()
	{
		return CVarInContext.GetValueOnGameThread() != 0;
	}

	/** Alt alone: with Ctrl or Shift it is another tool's. */
	bool IsPickModifier(const FInputEvent& InEvent)
	{
		return InEvent.IsAltDown() && !InEvent.IsControlDown() && !InEvent.IsShiftDown() && !InEvent.IsCommandDown();
	}

	const FText& TextOf(const TSharedRef<SWidget>& InWidget)
	{
		return InWidget->GetType() == RichTextBlockType ? StaticCastSharedRef<SRichTextBlock>(InWidget)->GetText() : StaticCastSharedRef<STextBlock>(InWidget)->GetText();
	}

	/**
	 * The deepest text widget under the point, from InWidget down: a text block is often not hit-testable itself (UMG's
	 * are not by default), so the path under the mouse ends above it.
	 */
	void FindText(const TSharedRef<SWidget>& InWidget, const FGeometry& InGeometry, const FVector2f& InPoint, TSharedPtr<SWidget>& OutText, FGeometry& OutGeometry)
	{
		if (!InGeometry.IsUnderLocation(InPoint))
		{
			return;
		}
		const FName Type = InWidget->GetType();
		if (Type == TextBlockType || Type == RichTextBlockType)
		{
			OutText = InWidget;
			OutGeometry = InGeometry;
		}
		FArrangedChildren Children(EVisibility::Visible);
		InWidget->ArrangeChildren(InGeometry, Children);
		// The one painted last is on top.
		for (int32 Index = Children.Num() - 1; Index >= 0; --Index)
		{
			const TSharedPtr<SWidget> Before = OutText;
			FindText(Children[Index].Widget, Children[Index].Geometry, InPoint, OutText, OutGeometry);
			if (OutText != Before)
			{
				return;
			}
		}
	}

	void ShowHint(const FString& InHint, const DreamLocInContext::FFound& InWhere)
	{
		Hint = InHint;
		HintAt = InWhere.Rect.bIsValid ? InWhere.Rect.Min : (FSlateApplication::IsInitialized() ? FVector2D(FSlateApplication::Get().GetCursorPos()) : FVector2D::ZeroVector);
		HintWindow = InWhere.Window;
		HintUntil = FPlatformTime::Seconds() + 4.0;
		UE_LOG(LogDreamLocTranslate, Display, TEXT("In-context: %s"), *InHint);
	}

#if WITH_SLATE_DEBUGGING
	/** A label with a dark plate behind it, at a point of the window, in its local units. */
	void DrawLabel(FSlateWindowElementList& InOut, int32 InLayerId, const FGeometry& InGeometry, const FVector2f& InAt, const FString& InText, const FLinearColor& InColor)
	{
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9);
		const FVector2f Size = FVector2f(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(InText, Font)) + FVector2f(8.0f, 4.0f);
		FSlateDrawElement::MakeBox(InOut, InLayerId, InGeometry.ToPaintGeometry(Size, FSlateLayoutTransform(InAt)), FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),
			ESlateDrawEffect::None, FLinearColor(0.02f, 0.02f, 0.02f, 0.9f));
		FSlateDrawElement::MakeText(InOut, InLayerId + 1, InGeometry.ToPaintGeometry(Size, FSlateLayoutTransform(InAt + FVector2f(4.0f, 2.0f))), InText, Font,
			ESlateDrawEffect::None, InColor);
	}

	/** The outline and its label, and the hint, drawn on top of the window they are in. */
	void Paint(const FPaintArgs& InArgs, const FGeometry& InGeometry, FSlateWindowElementList& InOut, int32& InOutLayerId)
	{
		const SWindow* Painted = InOut.GetPaintWindow();
		if (Painted == nullptr)
		{
			return;
		}
		const FVector2f Origin = FVector2f(Painted->GetPositionInScreen());
		auto ToLocal = [&InGeometry, &Origin](const FVector2D& InDesktop) -> FVector2f { return FVector2f(InGeometry.AbsoluteToLocal(FVector2f(InDesktop) - Origin)); };
		if (Highlighted.IsSet() && Highlighted->Window.Pin().Get() == Painted && Highlighted->Rect.bIsValid)
		{
			static const FSlateRoundedBoxBrush Outline(FLinearColor::Transparent, 2.0f, FLinearColor(0.95f, 0.72f, 0.25f), 2.0f);
			const FVector2f Min = ToLocal(Highlighted->Rect.Min);
			const FVector2f Max = ToLocal(Highlighted->Rect.Max);
			FSlateDrawElement::MakeBox(InOut, ++InOutLayerId, InGeometry.ToPaintGeometry(Max - Min + FVector2f(4.0f, 4.0f), FSlateLayoutTransform(Min - FVector2f(2.0f, 2.0f))), &Outline);
			const FString Label = Highlighted->HasKey() ? Highlighted->Namespace + TEXT(" / ") + Highlighted->Key : Highlighted->WhyNot;
			DrawLabel(InOut, ++InOutLayerId, InGeometry, Min - FVector2f(2.0f, 20.0f), Label, Highlighted->HasKey() ? FLinearColor(0.95f, 0.85f, 0.6f) : FLinearColor(0.75f, 0.75f, 0.75f));
			++InOutLayerId;
		}
		if (!Hint.IsEmpty() && FPlatformTime::Seconds() < HintUntil && HintWindow.Pin().Get() == Painted)
		{
			DrawLabel(InOut, ++InOutLayerId, InGeometry, ToLocal(HintAt) + FVector2f(0.0f, -24.0f), Hint, FLinearColor(1.0f, 0.6f, 0.5f));
			++InOutLayerId;
		}
	}
#endif

	class FProcessor : public IInputProcessor
	{
	public:
		virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override
		{
			if (!Hint.IsEmpty() && FPlatformTime::Seconds() >= HintUntil)
			{
				Hint.Reset();
			}
			// Alt let go while another application had the keyboard: no key-up came here.
			if (Highlighted.IsSet() && HighlightUser == SlateApp.GetUserIndexForKeyboard() && !SlateApp.GetModifierKeys().IsAltDown())
			{
				Highlighted.Reset();
			}
		}

		virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
		{
			if (IsEnabled() && (InKeyEvent.GetKey() == EKeys::LeftAlt || InKeyEvent.GetKey() == EKeys::RightAlt) && !InKeyEvent.IsControlDown() && !InKeyEvent.IsShiftDown())
			{
				if (const TSharedPtr<FSlateUser> User = SlateApp.GetUser(InKeyEvent.GetUserIndex()))
				{
					Highlight(DreamLocInContext::FindAt(FVector2D(User->GetCursorPosition()), InKeyEvent.GetUserIndex()), InKeyEvent.GetUserIndex());
				}
			}
			return false;
		}

		virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
		{
			if (InKeyEvent.GetKey() == EKeys::LeftAlt || InKeyEvent.GetKey() == EKeys::RightAlt)
			{
				Highlighted.Reset();
			}
			return false;
		}

		virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
		{
			if (IsEnabled() && IsPickModifier(MouseEvent))
			{
				Highlight(DreamLocInContext::FindAt(FVector2D(MouseEvent.GetScreenSpacePosition()), MouseEvent.GetUserIndex()), MouseEvent.GetUserIndex());
			}
			else if (Highlighted.IsSet() && HighlightUser == int32(MouseEvent.GetUserIndex()))
			{
				Highlighted.Reset();
			}
			return false;
		}

		virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
		{
			if (!IsEnabled() || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !IsPickModifier(MouseEvent))
			{
				return false;
			}
			// Only on a text: Alt+drag elsewhere is the viewport's (orbiting the camera).
			const DreamLocInContext::FFound Found = DreamLocInContext::FindAt(FVector2D(MouseEvent.GetScreenSpacePosition()), MouseEvent.GetUserIndex());
			if (!Found.bText)
			{
				return false;
			}
			SwallowUpOf = int32(MouseEvent.GetUserIndex());
			if (OnPick)
			{
				OnPick(Found);
			}
			else
			{
				DreamLocInContext::Pick(Found);
			}
			return true;
		}

		virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
		{
			if (SwallowUpOf == int32(MouseEvent.GetUserIndex()) && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
			{
				SwallowUpOf = INDEX_NONE;
				return true;
			}
			return false;
		}

		virtual const TCHAR* GetDebugName() const override { return TEXT("DreamLocInContext"); }

	private:
		static void Highlight(const DreamLocInContext::FFound& InFound, int32 InUser)
		{
			if (InFound.bText)
			{
				Highlighted = InFound;
				HighlightUser = InUser;
			}
			else
			{
				Highlighted.Reset();
			}
		}

		/** The button-up of a press taken here: taken too, so no widget sees half a click. */
		int32 SwallowUpOf = INDEX_NONE;
	};

	TSharedPtr<FProcessor> Processor;
}

DreamLocInContext::FFound DreamLocInContext::Describe(const FText& InText)
{
	FFound Out;
	Out.bText = true;
	const TOptional<FString> Key = FTextInspector::GetKey(InText);
	if (Key.IsSet() && !Key->IsEmpty())
	{
		Out.Namespace = TextNamespaceUtil::StripPackageNamespace(FTextInspector::GetNamespace(InText).Get(FString()));
		Out.Key = Key.GetValue();
		return Out;
	}
	if (InText.IsEmpty())
	{
		Out.WhyNot = LOCTEXT("Empty", "An empty text: nothing to translate").ToString();
	}
	else if (InText.IsInitializedFromString())
	{
		Out.WhyNot = LOCTEXT("FromString", "Made with FText::FromString: not localizable (LOCTEXT, NSLOCTEXT or a text property gives it a key)").ToString();
	}
	else if (InText.IsCultureInvariant())
	{
		Out.WhyNot = LOCTEXT("Invariant", "Culture invariant (FText::AsCultureInvariant): not localizable").ToString();
	}
	else
	{
		Out.WhyNot = LOCTEXT("Generated", "Built from other texts or numbers (FText::Format, AsNumber...): the texts it is built from have the keys").ToString();
	}
	return Out;
}

DreamLocInContext::FFound DreamLocInContext::FindAt(const FVector2D& InScreenPosition, int32 InUserIndex)
{
	using namespace DreamLocInContextLocal;
	FFound Out;
	if (!FSlateApplication::IsInitialized())
	{
		return Out;
	}
	FSlateApplication& App = FSlateApplication::Get();
	const FWidgetPath Path = App.LocateWindowUnderMouse(InScreenPosition, App.GetInteractiveTopLevelWindows(), /*bIgnoreEnabledStatus*/ true, InUserIndex);
	if (!Path.IsValid())
	{
		return Out;
	}
	TArray<TSharedRef<SWidget>> Widgets;
	for (int32 Index = 0; Index < Path.Widgets.Num(); ++Index)
	{
		Widgets.Add(Path.Widgets[Index].Widget);
	}
	for (IDreamLocTextLocator* Locator : IModularFeatures::Get().GetModularFeatureImplementations<IDreamLocTextLocator>(IDreamLocTextLocator::GetModularFeatureName()))
	{
		FDreamLocLocatedText Located;
		if (Locator != nullptr && Locator->LocateText(Widgets, InScreenPosition, Located))
		{
			Out.bText = true;
			Out.Namespace = TextNamespaceUtil::StripPackageNamespace(Located.Namespace);
			Out.Key = Located.Key;
			Out.WhyNot = Located.WhyNot;
			Out.Rect = Located.Rect;
			Out.Window = Path.GetWindow();
			return Out;
		}
	}
	TSharedPtr<SWidget> Text;
	FGeometry Geometry;
	FindText(Path.Widgets[0].Widget, Path.Widgets[0].Geometry, FVector2f(InScreenPosition), Text, Geometry);
	if (Text.IsValid())
	{
		Out = Describe(TextOf(Text.ToSharedRef()));
		const FVector2D Min = FVector2D(Geometry.GetAbsolutePosition());
		Out.Rect = FBox2D(Min, Min + FVector2D(Geometry.GetAbsoluteSize()));
		Out.Window = Path.GetWindow();
	}
	return Out;
}

TOptional<DreamLocInContext::FFound> DreamLocInContext::GetHighlighted()
{
	return DreamLocInContextLocal::Highlighted;
}

FString DreamLocInContext::GetHint()
{
	using namespace DreamLocInContextLocal;
	return FPlatformTime::Seconds() < HintUntil ? Hint : FString();
}

void DreamLocInContext::SetOnPick(TFunction<void(const FFound&)> InOnPick)
{
	DreamLocInContextLocal::OnPick = MoveTemp(InOnPick);
}

void DreamLocInContext::Pick(const FFound& InFound)
{
	using namespace DreamLocInContextLocal;
	if (!InFound.HasKey())
	{
		ShowHint(InFound.WhyNot, InFound);
		return;
	}
	const TSharedPtr<SDreamLocTranslateWindow> Window = DreamLocTranslateWindow::Open();
	if (!Window.IsValid())
	{
		ShowHint(LOCTEXT("NoWindow", "The translation window did not open").ToString(), InFound);
		return;
	}
	if (!Window->Select(InFound.Namespace, InFound.Key))
	{
		ShowHint(FText::Format(LOCTEXT("NotInDLoc", "{0} / {1} is not in DLoc/{2}: dloc gather (or sync) brings the project's texts there"),
			FText::FromString(InFound.Namespace), FText::FromString(InFound.Key), FText::FromString(Window->GetCulture())).ToString(), InFound);
	}
}

void DreamLocInContext::Register()
{
	using namespace DreamLocInContextLocal;
	if (Processor.IsValid() || !FSlateApplication::IsInitialized())
	{
		return;
	}
	Processor = MakeShared<FProcessor>();
	FSlateApplication::Get().RegisterInputPreProcessor(Processor);
#if WITH_SLATE_DEBUGGING
	PaintHandle = FSlateDebugging::PaintDebugElements.AddStatic(&DreamLocInContextLocal::Paint);
#endif
}

void DreamLocInContext::Unregister()
{
	using namespace DreamLocInContextLocal;
#if WITH_SLATE_DEBUGGING
	FSlateDebugging::PaintDebugElements.Remove(PaintHandle);
#endif
	PaintHandle.Reset();
	if (Processor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(Processor);
	}
	Processor.Reset();
	Highlighted.Reset();
	OnPick = nullptr;
}

#undef LOCTEXT_NAMESPACE
