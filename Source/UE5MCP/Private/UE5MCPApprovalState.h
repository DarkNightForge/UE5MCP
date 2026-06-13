// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UE5MCPTypes.h"

/** Owns the approval/stale-preview lifecycle: exactly one pending-approval slot
 *  plus a FIFO-capped record list so clients can poll terminal states. All
 *  methods are game-thread only (enforced by the owning service). */
class FUE5MCPApprovalState
{
public:
	TSharedPtr<FUE5MCPPlanRecord> CreateRecord(
		FUE5MCPValidatedPlan&& Plan, EUE5MCPPlanSource Source,
		EUE5MCPPlanStatus InitialStatus, const FString& RefusalCode);

	TSharedPtr<FUE5MCPPlanRecord> GetCurrentRecord() const;
	TSharedPtr<const FUE5MCPPlanRecord> FindRecord(const FString& PlanId) const;
	bool HasPendingPlan() const;
	bool WasLastPlanConsumed() const { return bLastPlanConsumed; }

	void SupersedeCurrent();
	void ConsumeCurrent(EUE5MCPPlanStatus TerminalStatus);
	void RefuseCurrentStale();
	void Reset();

	/** The stale-preview guard: a plan is approvable only against the exact scene
	 *  and selected-actor set it was previewed in (compared via the plan's
	 *  ContextWorldName and sorted SelectedActorPathsAtGeneration). */
	static bool IsContextStillValid(
		const FUE5MCPValidatedPlan& Plan, const FUE5MCPContextPack& FreshContext, FString& OutReason);

private:
	static constexpr int32 MaxRecords = 16;

	FString CurrentPlanId;
	bool bLastPlanConsumed = false;
	TArray<TSharedPtr<FUE5MCPPlanRecord>> Records;
};
