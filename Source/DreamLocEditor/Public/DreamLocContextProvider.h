// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Features/IModularFeature.h"

/** A translation to lay out where the game shows it. */
struct FDreamLocFitRequest
{
	FString Namespace;
	FString Key;
	/** The entry's `at`: `DUI/UI/Settings.dui#Footer/Apply` -- the source file, relative to its owner, and where in it. */
	FString At;
	/** The folder `at` is relative to: the project's, or a plugin's. */
	FString OwnerDirectory;
	FString Culture;
	/** The translation. */
	FString Text;
};

/** What the source tool found when it laid the translation out. */
struct FDreamLocFitResult
{
	/** False when the tool could not find where the text is shown (not compiled, no such node...): nothing is said. */
	bool bMeasured = false;
	/** The translation does not fit: cut by the text's overflow policy, or laid out past its box. */
	bool bOverflows = false;
	/** What the text needs, and the box it has, in the tool's units. */
	FVector2f Needed = FVector2f::ZeroVector;
	FVector2f Box = FVector2f::ZeroVector;
	/** The characters no face of the text's font has, each once. */
	TArray<uint32> MissingGlyphs;
	/** Where it was laid out, for the message: `Text Apply (DefaultFont_DistanceField, 20)`. */
	FString Where;
};

/**
 * What a tool that shows texts -- DreamGUI -- tells DreamLoc about them: whether a translation fits where it is shown, and
 * whether its font has every character. Registered as a modular feature under GetModularFeatureName(); `dloc check` asks
 * every provider that handles an entry's source file (DLOC4001 / DLOC4002).
 */
class IDreamLocContextProvider : public IModularFeature
{
public:
	static FName GetModularFeatureName()
	{
		static const FName Name(TEXT("DreamLocContextProvider"));
		return Name;
	}

	virtual ~IDreamLocContextProvider() = default;

	/** Whether this provider shows the texts of that source file (as an `at` names it: `DUI/UI/Settings.dui`). */
	virtual bool HandlesSource(const FString& InSource) const = 0;

	/** Lays the translation out where the game shows it. False when it cannot; OutResult.bMeasured says so too. */
	virtual bool MeasureTranslation(const FDreamLocFitRequest& InRequest, FDreamLocFitResult& OutResult) = 0;
};
