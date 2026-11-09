// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DreamLocMetaDataSample.generated.h"

/** How the sample panel fits its box. */
UENUM()
enum class EDreamLocSampleFit : uint8
{
	Fill UMETA(DisplayName = "Fill the Box"),
	KeepAspect,
};

/**
 * The reflected type the meta-data gather test reads: a display name, tooltips, a short tooltip, categories and an
 * enum, as a plugin's Details panel would show them. The only type in its folder.
 */
UCLASS(meta = (DisplayName = "Sample Panel"))
class UDreamLocMetaDataSample : public UObject
{
	GENERATED_BODY()

public:
	/** How wide the panel is, in slate units. */
	UPROPERTY(EditAnywhere, Category = "Sample|Layout", meta = (DisplayName = "Panel Width"))
	float Width = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Sample|Layout")
	bool bShowTitle = false;

	/** How the panel fits its box when the box is not its shape. */
	UPROPERTY(EditAnywhere, Category = "Sample", meta = (ShortTooltip = "How it fits"))
	EDreamLocSampleFit Fit = EDreamLocSampleFit::Fill;
};
