// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UE5MCPTypes.h"

struct FUE5MCPPlanValidationResult
{
	FUE5MCPValidatedPlan Plan;
	TArray<FString> Problems;

	bool IsValid() const { return Problems.IsEmpty(); }
};

/** Schema, policy, and target gates for incoming plan requests. Rule numbers
 *  R1..R10 match the published format spec; every problem string is prefixed
 *  with its rule. Both the panel and any external transport validate here —
 *  there is exactly one path to a FUE5MCPValidatedPlan. */
class FUE5MCPPlanValidator
{
public:
	static constexpr int32 SchemaVersion = 1;
	static constexpr int32 MaxTargetsPerAction = 200;
	static constexpr int32 MaxSpawnInstancesPerAction = 25;

	static FUE5MCPPlanValidationResult ValidateAndResolve(const FUE5MCPPlanRequest& Request);
};
