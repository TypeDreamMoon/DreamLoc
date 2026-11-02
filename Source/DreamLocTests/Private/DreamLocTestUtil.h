// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamLocDiagnostics.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

namespace DreamLocTestUtil
{
	/** A fresh folder under Saved/DreamLocTests/, absolute, without a trailing slash. */
	inline FString MakeTempDirectory(const TCHAR* InName)
	{
		FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("DreamLocTests")
			/ FString::Printf(TEXT("%s-%s"), InName, *FGuid::NewGuid().ToString(EGuidFormats::Short)));
		FPaths::NormalizeDirectoryName(Directory);
		IFileManager::Get().MakeDirectory(*Directory, /*Tree*/ true);
		return Directory;
	}

	inline bool WriteText(const FString& InPath, const FString& InText)
	{
		return FFileHelper::SaveStringToFile(InText, *InPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	inline void DeleteDirectory(const FString& InDirectory)
	{
		IFileManager::Get().DeleteDirectory(*InDirectory, /*RequireExists*/ false, /*Tree*/ true);
	}

	/** "DLOC1001@3:5" per diagnostic, for comparing what was reported against what was expected. */
	inline FString Describe(TConstArrayView<FDreamLocDiagnostic> InDiagnostics)
	{
		TArray<FString> Parts;
		for (const FDreamLocDiagnostic& Diagnostic : InDiagnostics)
		{
			Parts.Add(FString::Printf(TEXT("%s@%d:%d"), *Diagnostic.GetCodeName(), Diagnostic.Line, Diagnostic.Column));
		}
		return FString::Join(Parts, TEXT(" "));
	}
}
