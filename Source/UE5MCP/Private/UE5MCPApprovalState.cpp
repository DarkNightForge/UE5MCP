// Copyright Epic Games, Inc. All Rights Reserved.

#include "UE5MCPApprovalState.h"

#include "Misc/Guid.h"

TSharedPtr<FUE5MCPPlanRecord> FUE5MCPApprovalState::CreateRecord(
	FUE5MCPValidatedPlan&& Plan, EUE5MCPPlanSource Source,
	EUE5MCPPlanStatus InitialStatus, const FString& RefusalCode)
{
	TSharedPtr<FUE5MCPPlanRecord> Record = MakeShared<FUE5MCPPlanRecord>();
	Record->PlanId = FGuid::NewGuid().ToString(EGuidFormats::DigitsLower);
	Record->Source = Source;
	Record->Status = InitialStatus;
	Record->RefusalCode = RefusalCode;
	Record->Plan = MoveTemp(Plan);
	Record->CreatedUtc = FDateTime::UtcNow();

	Records.Add(Record);
	if (Records.Num() > MaxRecords)
	{
		Records.RemoveAt(0);
	}

	if (InitialStatus == EUE5MCPPlanStatus::PendingApproval)
	{
		CurrentPlanId = Record->PlanId;
		bLastPlanConsumed = false;
	}
	return Record;
}

TSharedPtr<FUE5MCPPlanRecord> FUE5MCPApprovalState::GetCurrentRecord() const
{
	if (CurrentPlanId.IsEmpty())
	{
		return nullptr;
	}
	for (const TSharedPtr<FUE5MCPPlanRecord>& Record : Records)
	{
		if (Record->PlanId == CurrentPlanId)
		{
			return Record;
		}
	}
	return nullptr;
}

TSharedPtr<const FUE5MCPPlanRecord> FUE5MCPApprovalState::FindRecord(const FString& PlanId) const
{
	for (const TSharedPtr<FUE5MCPPlanRecord>& Record : Records)
	{
		if (Record->PlanId == PlanId)
		{
			return Record;
		}
	}
	return nullptr;
}

bool FUE5MCPApprovalState::HasPendingPlan() const
{
	const TSharedPtr<FUE5MCPPlanRecord> Record = GetCurrentRecord();
	return Record.IsValid() && Record->Status == EUE5MCPPlanStatus::PendingApproval;
}

void FUE5MCPApprovalState::SupersedeCurrent()
{
	if (const TSharedPtr<FUE5MCPPlanRecord> Record = GetCurrentRecord())
	{
		if (Record->Status == EUE5MCPPlanStatus::PendingApproval)
		{
			Record->Status = EUE5MCPPlanStatus::Superseded;
			Record->RefusalCode = TEXT("superseded");
		}
	}
	CurrentPlanId.Empty();
}

void FUE5MCPApprovalState::ConsumeCurrent(EUE5MCPPlanStatus TerminalStatus)
{
	if (const TSharedPtr<FUE5MCPPlanRecord> Record = GetCurrentRecord())
	{
		Record->Status = TerminalStatus;
	}
	CurrentPlanId.Empty();
	bLastPlanConsumed = true;
}

void FUE5MCPApprovalState::RefuseCurrentStale()
{
	if (const TSharedPtr<FUE5MCPPlanRecord> Record = GetCurrentRecord())
	{
		Record->Status = EUE5MCPPlanStatus::RefusedStale;
		Record->RefusalCode = TEXT("stale_context");
	}
	CurrentPlanId.Empty();
}

void FUE5MCPApprovalState::Reset()
{
	Records.Empty();
	CurrentPlanId.Empty();
	bLastPlanConsumed = false;
}

bool FUE5MCPApprovalState::IsContextStillValid(
	const FUE5MCPValidatedPlan& Plan, const FUE5MCPContextPack& FreshContext, FString& OutReason)
{
	if (Plan.ContextWorldName != FreshContext.WorldName)
	{
		OutReason = FString::Printf(TEXT("Editor world changed from '%s' to '%s'."),
			*Plan.ContextWorldName, *FreshContext.WorldName);
		return false;
	}

	TArray<FString> CurrentSelectedActorPaths;
	for (const FUE5MCPActorSummary& Summary : FreshContext.SelectedActors)
	{
		CurrentSelectedActorPaths.Add(Summary.ActorPath);
	}
	CurrentSelectedActorPaths.Sort();

	if (CurrentSelectedActorPaths != Plan.SelectedActorPathsAtGeneration)
	{
		OutReason = TEXT("Selected actors changed after preview generation.");
		return false;
	}

	return true;
}
