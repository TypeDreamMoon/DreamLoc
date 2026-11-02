// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocDiagnostics.h"

DEFINE_LOG_CATEGORY(LogDreamLoc);

FString FDreamLocDiagnostic::GetCodeName() const
{
	return FString::Printf(TEXT("DLOC%04d"), Code);
}

FString FDreamLocDiagnostic::Format(const FString& InFile) const
{
	const TCHAR* SeverityName = Severity == EDreamLocSeverity::Error ? TEXT("error")
		: Severity == EDreamLocSeverity::Warning ? TEXT("warning") : TEXT("info");
	FString Where = InFile;
	if (Line > 0)
	{
		Where += FString::Printf(TEXT("(%d,%d)"), Line, FMath::Max(Column, 1));
	}
	return FString::Printf(TEXT("%s: %s %s: %s"), *Where, SeverityName, *GetCodeName(), *Message);
}

int32 FDreamLocDiagnostic::CountErrors(TConstArrayView<FDreamLocDiagnostic> InDiagnostics)
{
	int32 Count = 0;
	for (const FDreamLocDiagnostic& Diagnostic : InDiagnostics)
	{
		Count += Diagnostic.Severity == EDreamLocSeverity::Error ? 1 : 0;
	}
	return Count;
}

int32 FDreamLocDiagnostic::CountWarnings(TConstArrayView<FDreamLocDiagnostic> InDiagnostics)
{
	int32 Count = 0;
	for (const FDreamLocDiagnostic& Diagnostic : InDiagnostics)
	{
		Count += Diagnostic.Severity == EDreamLocSeverity::Warning ? 1 : 0;
	}
	return Count;
}
