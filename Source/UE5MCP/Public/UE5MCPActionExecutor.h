// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UE5MCPTypes.h"

class FUE5MCPActionExecutor
{
public:
	/** Executes a previously previewed and user-approved typed plan. */
	static FUE5MCPExecutionResult ExecuteApprovedPlan(const FUE5MCPValidatedPlan& Plan);
	static bool IsEditorMutationBlocked(FString* OutReason = nullptr);

private:
	static bool HasMutations(const FUE5MCPValidatedPlan& Plan);
	static FUE5MCPActionResult ExecuteSetActorFolder(const FUE5MCPResolvedAction& ResolvedAction);
	static FUE5MCPActionResult ExecuteSetActorLabel(const FUE5MCPResolvedAction& ResolvedAction);
	static FUE5MCPActionResult ExecuteAddActorTags(const FUE5MCPResolvedAction& ResolvedAction);
	static FUE5MCPActionResult ExecuteRemoveActorTags(const FUE5MCPResolvedAction& ResolvedAction);
	static FUE5MCPActionResult ExecuteSetActorProperty(const FUE5MCPResolvedAction& ResolvedAction);
	static FUE5MCPActionResult ExecuteSetActorTransform(const FUE5MCPResolvedAction& ResolvedAction);
	static FUE5MCPActionResult ExecuteGetSelectionContext(const FUE5MCPResolvedAction& ResolvedAction);
	static FUE5MCPActionResult ExecuteFindActors(const FUE5MCPResolvedAction& ResolvedAction);
	static FUE5MCPActionResult ExecuteReadLogs(const FUE5MCPResolvedAction& ResolvedAction);
	static FUE5MCPActionResult ExecuteGetPackageStatus(const FUE5MCPResolvedAction& ResolvedAction);
	static FUE5MCPActionResult ExecuteSelectActors(const FUE5MCPResolvedAction& ResolvedAction);
	static FUE5MCPActionResult ExecuteDuplicateActorWithOffset(const FUE5MCPResolvedAction& ResolvedAction);
	static FUE5MCPActionResult ExecuteSpawnActorFromClass(const FUE5MCPResolvedAction& ResolvedAction);
	static FUE5MCPActionResult ExecuteDeleteActor(const FUE5MCPResolvedAction& ResolvedAction);
};
