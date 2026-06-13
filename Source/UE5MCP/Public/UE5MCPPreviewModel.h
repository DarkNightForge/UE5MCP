// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UE5MCPTypes.h"

/** Builds the human-readable preview rows shown before approval. The preview is
 *  generated from the typed action data, so what is shown is what executes. */
class FUE5MCPPreviewModel
{
public:
	static FString BuildPreviewText(const FUE5MCPResolvedAction& ResolvedAction);
};
