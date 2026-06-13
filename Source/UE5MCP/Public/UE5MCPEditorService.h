// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UE5MCPLog.h"
#include "UE5MCPTypes.h"

class FUE5MCPApprovalState;

struct FUE5MCPApprovalOutcome
{
	bool bExecuted = false;
	FString RefusalCode;
	FString Message;
};

/**
 * The tool host's policy core: one shared instance owned by the module, consumed
 * identically by the panel, any external transport, and the automation tests.
 * There is exactly one path from a plan request to execution, and it always runs
 * through validation, preview, the stale-context guard, and human approval for
 * mutations. Game-thread only.
 */
class FUE5MCPEditorService
{
public:
	FUE5MCPEditorService();
	~FUE5MCPEditorService();

	static FUE5MCPEditorService& Get();

	FUE5MCPContextPack CollectContext(int32 MaxLoadedActors = 200) const;

	/**
	 * Validates and stages a plan request. Read-only plans execute immediately and
	 * return a terminal record. Mutation plans occupy the single pending-approval
	 * slot. Returns null with OutRefusalCode=="plan_pending" when a Bridge
	 * submission collides with an existing pending plan; Panel submissions
	 * supersede the pending plan instead — humans win, machines wait.
	 */
	TSharedPtr<const FUE5MCPPlanRecord> SubmitPlanRequest(
		const FUE5MCPPlanRequest& Request, EUE5MCPPlanSource Source, FString& OutRefusalCode);

	/** Validates, resolves, and builds the typed preview WITHOUT executing anything and
	 *  WITHOUT occupying the approval slot. This is how an external client renders an
	 *  exact effect description before its user approves the mutating call in-session. */
	TSharedPtr<const FUE5MCPPlanRecord> PreviewPlanRequest(
		const FUE5MCPPlanRequest& Request, EUE5MCPPlanSource Source, FString& OutRefusalCode);

	/**
	 * The external-session approval path: the human already approved this exact call
	 * inline in the agent session (the MCP client's native tool-permission prompt), so
	 * no in-editor click is required. The plugin REMAINS the enforcement boundary:
	 * gated by UUE5MCPSettings::bAllowExternalSessionApproval (off by default), it
	 * re-validates schema/policy/allowlists, blocks during PIE, re-checks the context
	 * fingerprint, wraps the batch in one undoable transaction, and logs every action.
	 * A user-approved call that violates policy is still refused here.
	 * Destructive plans additionally pend for an in-editor confirm when
	 * bRequireInEditorConfirmForDestructive is set.
	 */
	TSharedPtr<const FUE5MCPPlanRecord> SubmitExternalPlan(
		const FUE5MCPPlanRequest& Request, FString& OutRefusalCode);

	/** The panel's folder flow, expressed as a plan request over the live selection. */
	TSharedPtr<const FUE5MCPPlanRecord> GeneratePanelFolderPlan(const FName FolderPath);

	/** The human approval click: re-collects context, runs the stale-context guard,
	 *  executes, and consumes the plan (one preview, one approval, one execution,
	 *  one undo step). Upfront executor refusals keep the plan approvable. */
	FUE5MCPApprovalOutcome ApproveCurrentPlan();

	void ClearCurrentPlan();

	TSharedPtr<const FUE5MCPPlanRecord> GetCurrentPlanRecord() const;
	TSharedPtr<const FUE5MCPPlanRecord> FindPlanRecord(const FString& PlanId) const;
	bool IsApprovalAvailable() const;

	FUE5MCPLog& GetLog() { return Log; }

	/** Broadcast after any plan-state transition; the panel refreshes from this. */
	FSimpleMulticastDelegate OnStateChanged;

	void ResetForTests();

private:
	TUniquePtr<FUE5MCPApprovalState> ApprovalState;
	FUE5MCPLog Log;
};
