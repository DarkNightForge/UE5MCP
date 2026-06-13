// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UE5MCPTypes.h"

class AActor;

/** Resolves typed-plan target paths to live editor-world actors and runs bounded
 *  find queries. Resolution accepts both full object paths (as reported by the
 *  context pack) and editor-world-relative paths. */
class FUE5MCPTargetResolver
{
public:
	static AActor* ResolveActorByPath(const FString& ActorPath);

	static TArray<TWeakObjectPtr<AActor>> ResolveActorPaths(
		const TArray<FString>& ActorPaths, TArray<FString>& OutMissingPaths);

	/** Filters loaded editor actors by the query. Deterministic order (label-sorted);
	 *  results are capped at the query's MaxResults (clamped to 200). */
	static TArray<AActor*> FindActors(const FUE5MCPFindActorsQuery& Query, bool& bOutTruncated);
};
