// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocTextDiff.h"

namespace DreamLocTextDiffLocal
{
	/** Chinese, Japanese, Korean: compared character by character. */
	bool IsIdeographic(TCHAR InChar)
	{
		return (InChar >= 0x3040 && InChar <= 0x30FF) || (InChar >= 0x3400 && InChar <= 0x9FFF) || (InChar >= 0xAC00 && InChar <= 0xD7AF)
			|| (InChar >= 0xF900 && InChar <= 0xFAFF) || (InChar >= 0xFF00 && InChar <= 0xFFEF) || (InChar >= 0x3000 && InChar <= 0x303F);
	}

	/** The pieces compared: a placeholder whole, a word (letters and digits together), else one character. */
	TArray<FString> Tokens(const FString& InText)
	{
		TArray<FString> Out;
		for (const DreamLocChips::FPiece& Piece : DreamLocChips::Split(InText))
		{
			if (Piece.IsChip())
			{
				Out.Add(InText.Mid(Piece.Start, Piece.Length));
				continue;
			}
			for (int32 Index = Piece.Start; Index < Piece.End();)
			{
				const TCHAR Char = InText[Index];
				if (!IsIdeographic(Char) && FChar::IsAlnum(Char))
				{
					const int32 Begin = Index;
					while (Index < Piece.End() && !IsIdeographic(InText[Index]) && FChar::IsAlnum(InText[Index]))
					{
						++Index;
					}
					Out.Add(InText.Mid(Begin, Index - Begin));
				}
				else
				{
					Out.Add(FString::Chr(Char));
					++Index;
				}
			}
		}
		return Out;
	}

	void Push(TArray<DreamLocTextDiff::FPart>& OutParts, const FString& InText, EDreamLocDiffMark InMark)
	{
		if (OutParts.Num() > 0 && OutParts.Last().Mark == InMark)
		{
			OutParts.Last().Text += InText;
		}
		else
		{
			OutParts.Add({ InText, InMark });
		}
	}
}

TArray<DreamLocTextDiff::FPart> DreamLocTextDiff::Diff(const FString& InOld, const FString& InNew)
{
	using namespace DreamLocTextDiffLocal;
	TArray<FPart> Out;
	const TArray<FString> A = Tokens(InOld);
	const TArray<FString> B = Tokens(InNew);
	const int32 N = A.Num();
	const int32 M = B.Num();
	// Longer than any line of a game: shown as replaced whole rather than compared.
	if (int64(N) * int64(M) > 4000000)
	{
		Push(Out, InOld, EDreamLocDiffMark::Removed);
		Push(Out, InNew, EDreamLocDiffMark::Added);
		return Out;
	}
	// The longest common subsequence, from the end: Length[i][j] for A[i..], B[j..].
	TArray<int32> Length;
	Length.SetNumZeroed((N + 1) * (M + 1));
	auto At = [M](int32 I, int32 J) { return I * (M + 1) + J; };
	for (int32 I = N - 1; I >= 0; --I)
	{
		for (int32 J = M - 1; J >= 0; --J)
		{
			Length[At(I, J)] = A[I].Equals(B[J], ESearchCase::CaseSensitive) ? Length[At(I + 1, J + 1)] + 1 : FMath::Max(Length[At(I + 1, J)], Length[At(I, J + 1)]);
		}
	}
	int32 I = 0;
	int32 J = 0;
	while (I < N && J < M)
	{
		if (A[I].Equals(B[J], ESearchCase::CaseSensitive))
		{
			Push(Out, A[I++], EDreamLocDiffMark::Same);
			++J;
		}
		else if (Length[At(I + 1, J)] >= Length[At(I, J + 1)])
		{
			Push(Out, A[I++], EDreamLocDiffMark::Removed);
		}
		else
		{
			Push(Out, B[J++], EDreamLocDiffMark::Added);
		}
	}
	while (I < N)
	{
		Push(Out, A[I++], EDreamLocDiffMark::Removed);
	}
	while (J < M)
	{
		Push(Out, B[J++], EDreamLocDiffMark::Added);
	}
	return Out;
}

FString DreamLocTextDiff::Describe(TConstArrayView<FPart> InParts)
{
	FString Out;
	for (const FPart& Part : InParts)
	{
		Out += Part.Mark == EDreamLocDiffMark::Removed ? TEXT("-(") + Part.Text + TEXT(")") : Part.Mark == EDreamLocDiffMark::Added ? TEXT("+(") + Part.Text + TEXT(")") : Part.Text;
	}
	return Out;
}
