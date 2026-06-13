// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UE5MCPTypes.h"

class AActor;

class FUE5MCPContextCollector
{
public:
	static FUE5MCPContextPack Collect(int32 MaxLoadedActors = 200);
	static FUE5MCPActorSummary SummarizeActor(AActor* Actor, bool bSelected);
};
