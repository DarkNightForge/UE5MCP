// Copyright Epic Games, Inc. All Rights Reserved.

#include "UE5MCPEditorService.h"

#include "Editor.h"
#include "Misc/App.h"
#include "UE5MCP.h"
#include "UE5MCPActionExecutor.h"
#include "UE5MCPApprovalState.h"
#include "UE5MCPContextCollector.h"
#include "UE5MCPPlanValidator.h"
#include "UE5MCPSettings.h"

namespace
{
	bool PlanHasMutations(const FUE5MCPValidatedPlan& Plan)
	{
		for (const FUE5MCPResolvedAction& Resolved : Plan.Actions)
		{
			if (Resolved.Action.Risk != EUE5MCPRiskLevel::ReadOnly)
			{
				return true;
			}
		}
		return false;
	}

	bool PlanHasDestructive(const FUE5MCPValidatedPlan& Plan)
	{
		for (const FUE5MCPResolvedAction& Resolved : Plan.Actions)
		{
			if (Resolved.Action.Risk == EUE5MCPRiskLevel::Destructive)
			{
				return true;
			}
		}
		return false;
	}
}

FUE5MCPEditorService::FUE5MCPEditorService()
	: ApprovalState(MakeUnique<FUE5MCPApprovalState>())
{
}

FUE5MCPEditorService::~FUE5MCPEditorService() = default;

FUE5MCPEditorService& FUE5MCPEditorService::Get()
{
	return FUE5MCPModule::Get().GetService();
}

FUE5MCPContextPack FUE5MCPEditorService::CollectContext(int32 MaxLoadedActors) const
{
	check(IsInGameThread());
	return FUE5MCPContextCollector::Collect(MaxLoadedActors);
}

TSharedPtr<const FUE5MCPPlanRecord> FUE5MCPEditorService::SubmitPlanRequest(
	const FUE5MCPPlanRequest& Request, EUE5MCPPlanSource Source, FString& OutRefusalCode)
{
	check(IsInGameThread());
	OutRefusalCode.Empty();

	FUE5MCPPlanValidationResult Validation = FUE5MCPPlanValidator::ValidateAndResolve(Request);

	if (!Validation.IsValid())
	{
		for (const FString& Problem : Validation.Problems)
		{
			Log.Append(FString::Printf(TEXT("Plan rejected: %s"), *Problem));
		}
		const TSharedPtr<FUE5MCPPlanRecord> Record = ApprovalState->CreateRecord(
			MoveTemp(Validation.Plan), Source, EUE5MCPPlanStatus::Invalid, TEXT("invalid_plan"));
		OutRefusalCode = TEXT("invalid_plan");
		OnStateChanged.Broadcast();
		return Record;
	}

	// Read-only plans execute immediately: observation never needs approval and
	// never occupies the pending slot.
	if (!PlanHasMutations(Validation.Plan))
	{
		const FUE5MCPExecutionResult Result = FUE5MCPActionExecutor::ExecuteApprovedPlan(Validation.Plan);
		for (const FString& Line : Result.UserVisibleLogLines)
		{
			Log.Append(Line);
		}
		const TSharedPtr<FUE5MCPPlanRecord> Record = ApprovalState->CreateRecord(
			MoveTemp(Validation.Plan), Source,
			Result.bSuccess ? EUE5MCPPlanStatus::Executed : EUE5MCPPlanStatus::Failed, FString());
		Record->Result = Result;
		OnStateChanged.Broadcast();
		return Record;
	}

	// Destructive plans never execute via this single-click pending path. Their gate
	// is the external in-session permission prompt (which the client can never
	// allowlist) — optionally doubled with an in-editor confirm via
	// bRequireInEditorConfirmForDestructive on the SubmitExternalPlan path.
	if (PlanHasDestructive(Validation.Plan))
	{
		const TSharedPtr<FUE5MCPPlanRecord> Record = ApprovalState->CreateRecord(
			MoveTemp(Validation.Plan), Source, EUE5MCPPlanStatus::Invalid, TEXT("destructive_requires_external_gate"));
		OutRefusalCode = TEXT("destructive_requires_external_gate");
		Log.Append(TEXT("Plan refused: destructive actions require the external-session approval gate."));
		OnStateChanged.Broadcast();
		return Record;
	}

	if (ApprovalState->HasPendingPlan())
	{
		if (Source == EUE5MCPPlanSource::Bridge)
		{
			OutRefusalCode = TEXT("plan_pending");
			Log.Append(TEXT("External plan refused: another plan is already pending approval."));
			return nullptr;
		}
		ApprovalState->SupersedeCurrent();
		Log.Append(TEXT("Previous pending plan superseded by a new panel plan."));
	}

	const TSharedPtr<FUE5MCPPlanRecord> Record = ApprovalState->CreateRecord(
		MoveTemp(Validation.Plan), Source, EUE5MCPPlanStatus::PendingApproval, FString());
	for (const FUE5MCPResolvedAction& Resolved : Record->Plan.Actions)
	{
		Log.Append(FString::Printf(TEXT("Generated typed preview: %s"), *Resolved.PreviewText));
	}
	if (Source == EUE5MCPPlanSource::Bridge)
	{
		Log.Append(TEXT("External plan pending human approval in the panel."));
	}
	OnStateChanged.Broadcast();
	return Record;
}

TSharedPtr<const FUE5MCPPlanRecord> FUE5MCPEditorService::PreviewPlanRequest(
	const FUE5MCPPlanRequest& Request, EUE5MCPPlanSource Source, FString& OutRefusalCode)
{
	check(IsInGameThread());
	OutRefusalCode.Empty();

	FUE5MCPPlanValidationResult Validation = FUE5MCPPlanValidator::ValidateAndResolve(Request);
	if (!Validation.IsValid())
	{
		for (const FString& Problem : Validation.Problems)
		{
			Log.Append(FString::Printf(TEXT("Preview rejected: %s"), *Problem));
		}
		const TSharedPtr<FUE5MCPPlanRecord> Record = ApprovalState->CreateRecord(
			MoveTemp(Validation.Plan), Source, EUE5MCPPlanStatus::Invalid, TEXT("invalid_plan"));
		OutRefusalCode = TEXT("invalid_plan");
		OnStateChanged.Broadcast();
		return Record;
	}

	const TSharedPtr<FUE5MCPPlanRecord> Record = ApprovalState->CreateRecord(
		MoveTemp(Validation.Plan), Source, EUE5MCPPlanStatus::PreviewedOnly, FString());
	for (const FUE5MCPResolvedAction& Resolved : Record->Plan.Actions)
	{
		Log.Append(FString::Printf(TEXT("Preview-only (no execution): %s"), *Resolved.PreviewText));
	}
	OnStateChanged.Broadcast();
	return Record;
}

TSharedPtr<const FUE5MCPPlanRecord> FUE5MCPEditorService::SubmitExternalPlan(
	const FUE5MCPPlanRequest& Request, FString& OutRefusalCode)
{
	check(IsInGameThread());
	OutRefusalCode.Empty();

	const UUE5MCPSettings* Settings = GetDefault<UUE5MCPSettings>();
	if (!Settings->bAllowExternalSessionApproval)
	{
		OutRefusalCode = TEXT("external_approval_disabled");
		Log.Append(TEXT("External-session plan refused: external-session approval is disabled in UE5MCP project settings."));
		return nullptr;
	}

	FUE5MCPPlanValidationResult Validation = FUE5MCPPlanValidator::ValidateAndResolve(Request);
	if (!Validation.IsValid())
	{
		for (const FString& Problem : Validation.Problems)
		{
			Log.Append(FString::Printf(TEXT("External plan rejected: %s"), *Problem));
		}
		const TSharedPtr<FUE5MCPPlanRecord> Record = ApprovalState->CreateRecord(
			MoveTemp(Validation.Plan), EUE5MCPPlanSource::Bridge, EUE5MCPPlanStatus::Invalid, TEXT("invalid_plan"));
		Record->bExternalSessionApproval = true;
		OutRefusalCode = TEXT("invalid_plan");
		OnStateChanged.Broadcast();
		return Record;
	}

	// Read-only externally-submitted plans behave exactly like any other read-only
	// plan: observation executes immediately and never needs approval.
	if (!PlanHasMutations(Validation.Plan))
	{
		const FUE5MCPExecutionResult Result = FUE5MCPActionExecutor::ExecuteApprovedPlan(Validation.Plan);
		for (const FString& Line : Result.UserVisibleLogLines)
		{
			Log.Append(Line);
		}
		const TSharedPtr<FUE5MCPPlanRecord> Record = ApprovalState->CreateRecord(
			MoveTemp(Validation.Plan), EUE5MCPPlanSource::Bridge,
			Result.bSuccess ? EUE5MCPPlanStatus::Executed : EUE5MCPPlanStatus::Failed, FString());
		Record->Result = Result;
		Record->bExternalSessionApproval = true;
		OnStateChanged.Broadcast();
		return Record;
	}

	// Belt-and-suspenders (off by default): destructive plans can be configured to
	// ALSO require the in-editor approval click on top of the in-session prompt.
	if (PlanHasDestructive(Validation.Plan) && Settings->bRequireInEditorConfirmForDestructive)
	{
		if (ApprovalState->HasPendingPlan())
		{
			OutRefusalCode = TEXT("plan_pending");
			Log.Append(TEXT("External destructive plan refused: another plan is already pending approval."));
			return nullptr;
		}
		const TSharedPtr<FUE5MCPPlanRecord> Record = ApprovalState->CreateRecord(
			MoveTemp(Validation.Plan), EUE5MCPPlanSource::Bridge, EUE5MCPPlanStatus::PendingApproval, FString());
		Record->bExternalSessionApproval = true;
		for (const FUE5MCPResolvedAction& Resolved : Record->Plan.Actions)
		{
			Log.Append(FString::Printf(TEXT("Generated typed preview: %s"), *Resolved.PreviewText));
		}
		Log.Append(TEXT("External destructive plan pending the additional in-editor confirmation (belt-and-suspenders setting)."));
		OnStateChanged.Broadcast();
		return Record;
	}

	// The plugin retains final refusal authority: the same mutation guards run even
	// though the human already approved this call in the agent session.
	FString BlockReason;
	if (FUE5MCPActionExecutor::IsEditorMutationBlocked(&BlockReason))
	{
		OutRefusalCode = (GEditor && GEditor->PlayWorld) ? TEXT("play_mode_active") : TEXT("editor_unavailable");
		const TSharedPtr<FUE5MCPPlanRecord> Record = ApprovalState->CreateRecord(
			MoveTemp(Validation.Plan), EUE5MCPPlanSource::Bridge, EUE5MCPPlanStatus::Failed, OutRefusalCode);
		Record->bExternalSessionApproval = true;
		Log.Append(FString::Printf(TEXT("External plan refused: %s"), *BlockReason));
		OnStateChanged.Broadcast();
		return Record;
	}

	// Context-fingerprint guard: the world (and observed selection) the client built
	// this plan against must still be current at the moment of execution.
	const FUE5MCPContextPack FreshContext = CollectContext();
	FString StaleReason;
	if (!FUE5MCPApprovalState::IsContextStillValid(Validation.Plan, FreshContext, StaleReason))
	{
		OutRefusalCode = TEXT("stale_context");
		const TSharedPtr<FUE5MCPPlanRecord> Record = ApprovalState->CreateRecord(
			MoveTemp(Validation.Plan), EUE5MCPPlanSource::Bridge, EUE5MCPPlanStatus::RefusedStale, TEXT("stale_context"));
		Record->bExternalSessionApproval = true;
		Log.Append(FString::Printf(TEXT("External plan refused: %s Re-read context and resubmit."), *StaleReason));
		OnStateChanged.Broadcast();
		return Record;
	}

	const FUE5MCPExecutionResult Result = FUE5MCPActionExecutor::ExecuteApprovedPlan(Validation.Plan);
	for (const FString& Line : Result.UserVisibleLogLines)
	{
		Log.Append(Line);
	}

	if (Result.ActionResults.IsEmpty())
	{
		OutRefusalCode = Result.UpfrontRefusalCode.IsEmpty() ? TEXT("invalid_plan") : Result.UpfrontRefusalCode;
		const TSharedPtr<FUE5MCPPlanRecord> Record = ApprovalState->CreateRecord(
			MoveTemp(Validation.Plan), EUE5MCPPlanSource::Bridge, EUE5MCPPlanStatus::Failed, OutRefusalCode);
		Record->bExternalSessionApproval = true;
		OnStateChanged.Broadcast();
		return Record;
	}

	const TSharedPtr<FUE5MCPPlanRecord> Record = ApprovalState->CreateRecord(
		MoveTemp(Validation.Plan), EUE5MCPPlanSource::Bridge,
		Result.bSuccess ? EUE5MCPPlanStatus::Executed : EUE5MCPPlanStatus::Failed, FString());
	Record->Result = Result;
	Record->bExternalSessionApproval = true;
	Log.Append(FString::Printf(
		TEXT("[external-session approval] plan %s %s (%d action(s)). Use standard Undo to revert."),
		*Record->PlanId,
		Result.bSuccess ? TEXT("executed") : TEXT("finished with errors"),
		Result.ActionResults.Num()));
	OnStateChanged.Broadcast();
	return Record;
}

TSharedPtr<const FUE5MCPPlanRecord> FUE5MCPEditorService::GeneratePanelFolderPlan(const FName FolderPath)
{
	check(IsInGameThread());

	const FUE5MCPContextPack Context = CollectContext();
	for (const FString& Warning : Context.Warnings)
	{
		Log.Append(FString::Printf(TEXT("Warning: %s"), *Warning));
	}

	if (Context.SelectedActors.IsEmpty())
	{
		ApprovalState->SupersedeCurrent();
		Log.Append(TEXT("No selected actors. Select actors in the editor before generating a folder plan."));
		OnStateChanged.Broadcast();
		return nullptr;
	}

	FUE5MCPPlanRequest Request;
	Request.SchemaVersion = 1;
	Request.Summary = TEXT("Organize selected actors into an editor outliner folder.");
	Request.bRequiresApproval = true;
	Request.bHasContextFingerprint = true;
	Request.Fingerprint.SceneName = Context.WorldName;

	FUE5MCPActionRequest Action;
	Action.Id = TEXT("set-folder-selected-actors");
	Action.ToolName = TEXT("set_actor_folder");
	Action.RiskString = TEXT("low_risk");
	Action.FolderPath = FolderPath;
	Action.ProvidedParamKeys = { TEXT("folder_path") };
	for (const FUE5MCPActorSummary& Summary : Context.SelectedActors)
	{
		Action.TargetPaths.Add(Summary.ActorPath);
		Request.Fingerprint.SelectedActorPaths.Add(Summary.ActorPath);
	}
	Request.Actions.Add(MoveTemp(Action));

	FString UnusedRefusalCode;
	return SubmitPlanRequest(Request, EUE5MCPPlanSource::Panel, UnusedRefusalCode);
}

FUE5MCPApprovalOutcome FUE5MCPEditorService::ApproveCurrentPlan()
{
	check(IsInGameThread());
	FUE5MCPApprovalOutcome Outcome;

	const TSharedPtr<FUE5MCPPlanRecord> Record = ApprovalState->GetCurrentRecord();
	if (!Record.IsValid() || Record->Status != EUE5MCPPlanStatus::PendingApproval)
	{
		const bool bConsumed = ApprovalState->WasLastPlanConsumed();
		Outcome.RefusalCode = bConsumed ? TEXT("plan_consumed") : TEXT("invalid_plan");
		Outcome.Message = bConsumed
			? TEXT("Approve blocked: the plan already executed. Generate a new preview to run another action.")
			: TEXT("Approve blocked: no valid typed plan is ready.");
		Log.Append(Outcome.Message);
		return Outcome;
	}

	// Mutation guards run BEFORE the stale check: while play mode is active (or no
	// editor world exists), selection cannot be collected reliably, so the guard is
	// the truthful refusal reason. The plan stays pending until the blocker clears.
	FString BlockReason;
	if (FUE5MCPActionExecutor::IsEditorMutationBlocked(&BlockReason))
	{
		Outcome.RefusalCode = (GEditor && GEditor->PlayWorld) ? TEXT("play_mode_active") : TEXT("editor_unavailable");
		Outcome.Message = FString::Printf(TEXT("Approve blocked: %s"), *BlockReason);
		Log.Append(Outcome.Message);
		return Outcome;
	}

	// Stale-preview guard: re-collect context at the moment of the click.
	const FUE5MCPContextPack FreshContext = CollectContext();
	FString StaleReason;
	if (!FUE5MCPApprovalState::IsContextStillValid(Record->Plan, FreshContext, StaleReason))
	{
		ApprovalState->RefuseCurrentStale();
		Outcome.RefusalCode = TEXT("stale_context");
		Outcome.Message = FString::Printf(TEXT("Approve blocked: %s Regenerate the preview before approving."), *StaleReason);
		Log.Append(Outcome.Message);
		OnStateChanged.Broadcast();
		return Outcome;
	}

	const FUE5MCPExecutionResult Result = FUE5MCPActionExecutor::ExecuteApprovedPlan(Record->Plan);
	for (const FString& Line : Result.UserVisibleLogLines)
	{
		Log.Append(Line);
	}

	if (Result.ActionResults.IsEmpty())
	{
		// Upfront executor refusal (e.g. play mode active): nothing ran, so the plan
		// stays pending and becomes approvable again once the blocker clears.
		Outcome.RefusalCode = Result.UpfrontRefusalCode.IsEmpty() ? TEXT("invalid_plan") : Result.UpfrontRefusalCode;
		Outcome.Message = TEXT("Approved plan refused before execution; it remains pending until the blocker clears.");
		Log.Append(Outcome.Message);
		OnStateChanged.Broadcast();
		return Outcome;
	}

	Record->Result = Result;
	ApprovalState->ConsumeCurrent(Result.bSuccess ? EUE5MCPPlanStatus::Executed : EUE5MCPPlanStatus::Failed);
	Log.Append(Result.bSuccess
		? TEXT("Approved plan completed. Use standard Undo to revert the transaction.")
		: TEXT("Approved plan finished with errors."));
	Log.Append(TEXT("Plan consumed after execution. Generate a new preview to run another action."));

	Outcome.bExecuted = true;
	Outcome.Message = TEXT("Plan executed.");
	OnStateChanged.Broadcast();
	return Outcome;
}

void FUE5MCPEditorService::ClearCurrentPlan()
{
	check(IsInGameThread());
	ApprovalState->SupersedeCurrent();
	Log.Append(TEXT("Cleared current preview."));
	OnStateChanged.Broadcast();
}

TSharedPtr<const FUE5MCPPlanRecord> FUE5MCPEditorService::GetCurrentPlanRecord() const
{
	return ApprovalState->GetCurrentRecord();
}

TSharedPtr<const FUE5MCPPlanRecord> FUE5MCPEditorService::FindPlanRecord(const FString& PlanId) const
{
	return ApprovalState->FindRecord(PlanId);
}

bool FUE5MCPEditorService::IsApprovalAvailable() const
{
	const TSharedPtr<const FUE5MCPPlanRecord> Record = GetCurrentPlanRecord();
	return Record.IsValid()
		&& Record->Status == EUE5MCPPlanStatus::PendingApproval
		&& Record->Plan.bIsValid
		&& !Record->Plan.Actions.IsEmpty();
}

void FUE5MCPEditorService::ResetForTests()
{
	check(IsInGameThread());
	ApprovalState->Reset();
	OnStateChanged.Broadcast();
}
