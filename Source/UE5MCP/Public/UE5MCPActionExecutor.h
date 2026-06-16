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

	/** Package-write policy (domains 12-13). Pure decision: given the on-disk +
	 *  source-control signals for a package, is a mutation that dirties it safe to
	 *  save? Not writable when the file is read-only on disk (the universal "save
	 *  will fail" signal — e.g. an unchecked-out Perforce file) or source control
	 *  reports it checked out by someone else. New/unsaved packages and writable
	 *  files are writable. Public + static so it is unit-testable. */
	static bool EvaluatePackageWritability(bool bFileExists, bool bFileReadOnly,
		bool bSourceControlEnabled, const FString& SourceControlState, FString& OutReason);

	/** Describe the writability of every package an action would dirty (target actors'
	 *  packages, plus the editor world for spawns). Sets bOutBlocked if any is not
	 *  writable under the policy. Used by both executor enforcement and the preview. */
	static FString DescribeActionPackages(const FUE5MCPResolvedAction& ResolvedAction, bool& bOutBlocked);

private:
	/** Returns true and fills OutResult with a package_not_writable refusal when the
	 *  package-write policy is enabled and this mutation would dirty an unwritable
	 *  package. Read-only/selection actions are never blocked. */
	static bool IsBlockedByPackageWritePolicy(const FUE5MCPResolvedAction& ResolvedAction, FUE5MCPActionResult& OutResult);
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
	static FUE5MCPActionResult ExecuteGetActorProperties(const FUE5MCPResolvedAction& ResolvedAction);
	static FUE5MCPActionResult ExecuteGetActorComponents(const FUE5MCPResolvedAction& ResolvedAction);
	static FUE5MCPActionResult ExecuteListCapabilities(const FUE5MCPResolvedAction& ResolvedAction);
	static FUE5MCPActionResult ExecuteSelectActors(const FUE5MCPResolvedAction& ResolvedAction);
	static FUE5MCPActionResult ExecuteDuplicateActorWithOffset(const FUE5MCPResolvedAction& ResolvedAction);
	static FUE5MCPActionResult ExecuteSpawnActorFromClass(const FUE5MCPResolvedAction& ResolvedAction);
	static FUE5MCPActionResult ExecuteDeleteActor(const FUE5MCPResolvedAction& ResolvedAction);
};
