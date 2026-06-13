// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UE5MCPTypes.h"

/** Hand-rolled JSON (de)serialization for the typed plan envelope and results.
 *  The plugin's types are plain structs (no UHT reflection), so conversion is
 *  explicit field-by-field against the published format spec. Unknown top-level
 *  fields are ignored; unknown params are recorded for the validator to reject. */
namespace UE5MCPJson
{
	bool ParsePlanRequest(const FString& Body, FUE5MCPPlanRequest& OutRequest, TArray<FString>& OutErrors);

	FString SerializeContextPack(const FUE5MCPContextPack& Context);
	FString SerializeExecutionResult(const FUE5MCPExecutionResult& Result);
	FString SerializePlanRecord(const FUE5MCPPlanRecord& Record);
	FString SerializeError(const FString& MachineCode, const FString& Message);

	FString PlanStatusToString(EUE5MCPPlanStatus Status);
}
