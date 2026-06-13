// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Tests/AutomationEditorCommon.h"
#include "UE5MCPEditorService.h"
#include "UE5MCPTestHelpers.h"
#include "UE5MCPTypes.h"

// Service-layer lifecycle tests: these are the machine checks for the public
// validation checklist rows M3 (empty selection), M5/M6 (stale context),
// M7 (play-mode refusal keeps the plan), and M11 (plan consumption).

namespace UE5MCPServiceTestUtils
{
	FUE5MCPEditorService& FreshService()
	{
		FUE5MCPEditorService& Service = FUE5MCPEditorService::Get();
		Service.ResetForTests();
		return Service;
	}

	TArray<AActor*> SelectNewActors(UWorld* World, int32 Count)
	{
		const TArray<AActor*> Actors = UE5MCPTests::SpawnTestActors(World, Count);
		GEditor->GetEditorSubsystem<UEditorActorSubsystem>()->SetSelectedLevelActors(Actors);
		return Actors;
	}

	FUE5MCPPlanRequest MakeBridgeFolderRequest(const TArray<AActor*>& Actors, UWorld* World, const TCHAR* Folder)
	{
		FUE5MCPPlanRequest Request;
		Request.SchemaVersion = 1;
		Request.Summary = TEXT("Bridge test plan");
		Request.bRequiresApproval = true;
		Request.bHasContextFingerprint = true;
		Request.Fingerprint.SceneName = World->GetName();

		FUE5MCPActionRequest Action;
		Action.Id = TEXT("bridge-a1");
		Action.ToolName = TEXT("set_actor_folder");
		Action.RiskString = TEXT("low_risk");
		Action.FolderPath = FName(Folder);
		Action.ProvidedParamKeys = { TEXT("folder_path") };
		for (const AActor* Actor : Actors)
		{
			Action.TargetPaths.Add(Actor->GetPathName());
		}

		// Fingerprint reflects the CURRENT selection (what the agent observed).
		const TArray<AActor*> Selected = GEditor->GetEditorSubsystem<UEditorActorSubsystem>()->GetSelectedLevelActors();
		for (const AActor* Actor : Selected)
		{
			Request.Fingerprint.SelectedActorPaths.Add(Actor->GetPathName());
		}
		Request.Actions.Add(MoveTemp(Action));
		return Request;
	}

	FUE5MCPPlanRequest MakeTransformRequest(const TArray<AActor*>& Actors, UWorld* World, const FVector& NewLocation)
	{
		FUE5MCPPlanRequest Request;
		Request.SchemaVersion = 1;
		Request.Summary = TEXT("Service transform test plan");
		Request.bRequiresApproval = true;
		Request.bHasContextFingerprint = true;
		Request.Fingerprint.SceneName = World->GetName();

		FUE5MCPActionRequest Action;
		Action.Id = TEXT("svc-t1");
		Action.ToolName = TEXT("set_actor_transform");
		Action.RiskString = TEXT("low_risk");
		Action.Transform.bHasLocation = true;
		Action.Transform.Location = NewLocation;
		Action.ProvidedParamKeys = { TEXT("location") };
		for (const AActor* Actor : Actors)
		{
			Action.TargetPaths.Add(Actor->GetPathName());
		}

		const TArray<AActor*> Selected = GEditor->GetEditorSubsystem<UEditorActorSubsystem>()->GetSelectedLevelActors();
		for (const AActor* Actor : Selected)
		{
			Request.Fingerprint.SelectedActorPaths.Add(Actor->GetPathName());
		}
		Request.Actions.Add(MoveTemp(Action));
		return Request;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPServiceEmptySelectionTest,
	"UE5MCP.Service.EmptySelectionProducesNoApprovablePlan", UE5MCPTests::KernelTestFlags)
bool FUE5MCPServiceEmptySelectionTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	FUE5MCPEditorService& Service = UE5MCPServiceTestUtils::FreshService();
	GEditor->GetEditorSubsystem<UEditorActorSubsystem>()->SelectNothing();

	const TSharedPtr<const FUE5MCPPlanRecord> Record = Service.GeneratePanelFolderPlan(FName(TEXT("UE5MCPTests/Nope")));
	TestFalse(TEXT("No record produced for empty selection"), Record.IsValid());
	TestFalse(TEXT("Approve is not available"), Service.IsApprovalAvailable());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPServiceStaleSelectionTest,
	"UE5MCP.Service.ApproveRefusedAfterSelectionChange", UE5MCPTests::KernelTestFlags)
bool FUE5MCPServiceStaleSelectionTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	FUE5MCPEditorService& Service = UE5MCPServiceTestUtils::FreshService();
	UE5MCPServiceTestUtils::SelectNewActors(World, 2);

	const TSharedPtr<const FUE5MCPPlanRecord> Record = Service.GeneratePanelFolderPlan(FName(TEXT("UE5MCPTests/Stale")));
	TestTrue(TEXT("Plan pending"), Record.IsValid() && Record->Status == EUE5MCPPlanStatus::PendingApproval);

	GEditor->GetEditorSubsystem<UEditorActorSubsystem>()->SelectNothing();

	const FUE5MCPApprovalOutcome Outcome = Service.ApproveCurrentPlan();
	TestFalse(TEXT("Approval refused"), Outcome.bExecuted);
	TestEqual(TEXT("Refusal code is stale_context"), Outcome.RefusalCode, FString(TEXT("stale_context")));
	TestFalse(TEXT("Plan no longer approvable (regenerate required)"), Service.IsApprovalAvailable());
	TestTrue(TEXT("Record marked stale"),
		Record.IsValid() && Record->Status == EUE5MCPPlanStatus::RefusedStale);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPServiceStaleWorldTest,
	"UE5MCP.Service.ApproveRefusedAfterWorldChange", UE5MCPTests::KernelTestFlags)
bool FUE5MCPServiceStaleWorldTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	FUE5MCPEditorService& Service = UE5MCPServiceTestUtils::FreshService();
	UE5MCPServiceTestUtils::SelectNewActors(World, 2);

	const TSharedPtr<const FUE5MCPPlanRecord> Record = Service.GeneratePanelFolderPlan(FName(TEXT("UE5MCPTests/StaleWorld")));
	TestTrue(TEXT("Plan pending"), Record.IsValid() && Record->Status == EUE5MCPPlanStatus::PendingApproval);

	UWorld* NewWorld = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("Second map created"), NewWorld);

	const FUE5MCPApprovalOutcome Outcome = Service.ApproveCurrentPlan();
	TestFalse(TEXT("Approval refused"), Outcome.bExecuted);
	TestEqual(TEXT("Refusal code is stale_context"), Outcome.RefusalCode, FString(TEXT("stale_context")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPServicePlanConsumedTest,
	"UE5MCP.Service.PlanConsumedAfterExecution", UE5MCPTests::KernelTestFlags)
bool FUE5MCPServicePlanConsumedTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	FUE5MCPEditorService& Service = UE5MCPServiceTestUtils::FreshService();
	const TArray<AActor*> Actors = UE5MCPServiceTestUtils::SelectNewActors(World, 2);

	const TSharedPtr<const FUE5MCPPlanRecord> Record = Service.GeneratePanelFolderPlan(FName(TEXT("UE5MCPTests/Consumed")));
	TestTrue(TEXT("Plan pending"), Record.IsValid());

	const FUE5MCPApprovalOutcome First = Service.ApproveCurrentPlan();
	TestTrue(TEXT("First approval executed"), First.bExecuted);
	TestEqual(TEXT("Folder applied"), Actors[0]->GetFolderPath(), FName(TEXT("UE5MCPTests/Consumed")));
	TestTrue(TEXT("Record terminal"), Record.IsValid() && Record->Status == EUE5MCPPlanStatus::Executed);

	const FUE5MCPApprovalOutcome Second = Service.ApproveCurrentPlan();
	TestFalse(TEXT("Second approval refused"), Second.bExecuted);
	TestEqual(TEXT("Refusal code is plan_consumed"), Second.RefusalCode, FString(TEXT("plan_consumed")));
	TestFalse(TEXT("Approve unavailable after consumption"), Service.IsApprovalAvailable());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPServicePlayModeKeepsPlanTest,
	"UE5MCP.Service.PlayModeRefusalKeepsPlanApprovable", UE5MCPTests::KernelTestFlags)
bool FUE5MCPServicePlayModeKeepsPlanTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	FUE5MCPEditorService& Service = UE5MCPServiceTestUtils::FreshService();
	UE5MCPServiceTestUtils::SelectNewActors(World, 1);

	const TSharedPtr<const FUE5MCPPlanRecord> Record = Service.GeneratePanelFolderPlan(FName(TEXT("UE5MCPTests/PlayMode")));
	TestTrue(TEXT("Plan pending"), Record.IsValid());

	{
		TGuardValue<TObjectPtr<UWorld>> PlayWorldGuard(GEditor->PlayWorld, World);
		const FUE5MCPApprovalOutcome Outcome = Service.ApproveCurrentPlan();
		TestFalse(TEXT("Refused during play mode"), Outcome.bExecuted);
		TestEqual(TEXT("Refusal code is play_mode_active"), Outcome.RefusalCode, FString(TEXT("play_mode_active")));
	}

	TestTrue(TEXT("Plan still approvable after play mode ends"), Service.IsApprovalAvailable());
	const FUE5MCPApprovalOutcome Retry = Service.ApproveCurrentPlan();
	TestTrue(TEXT("Approval succeeds once the blocker cleared"), Retry.bExecuted);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPServiceBridgeLifecycleTest,
	"UE5MCP.Service.BridgePlanPendingUntilPanelApproval", UE5MCPTests::KernelTestFlags)
bool FUE5MCPServiceBridgeLifecycleTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	FUE5MCPEditorService& Service = UE5MCPServiceTestUtils::FreshService();
	const TArray<AActor*> Actors = UE5MCPServiceTestUtils::SelectNewActors(World, 2);

	FString RefusalCode;
	const TSharedPtr<const FUE5MCPPlanRecord> Record = Service.SubmitPlanRequest(
		UE5MCPServiceTestUtils::MakeBridgeFolderRequest(Actors, World, TEXT("UE5MCPTests/FromBridge")),
		EUE5MCPPlanSource::Bridge, RefusalCode);

	TestTrue(TEXT("Bridge plan accepted as pending"),
		Record.IsValid() && Record->Status == EUE5MCPPlanStatus::PendingApproval);
	TestEqual(TEXT("Actors untouched before approval"), Actors[0]->GetFolderPath(), FName(NAME_None));

	// A second bridge plan while one pends must be refused.
	FString SecondRefusal;
	const TSharedPtr<const FUE5MCPPlanRecord> Second = Service.SubmitPlanRequest(
		UE5MCPServiceTestUtils::MakeBridgeFolderRequest(Actors, World, TEXT("UE5MCPTests/Second")),
		EUE5MCPPlanSource::Bridge, SecondRefusal);
	TestFalse(TEXT("Second bridge plan refused"), Second.IsValid());
	TestEqual(TEXT("Busy refusal code"), SecondRefusal, FString(TEXT("plan_pending")));

	// The human approves in the panel.
	const FUE5MCPApprovalOutcome Outcome = Service.ApproveCurrentPlan();
	TestTrue(TEXT("Human approval executed the bridge plan"), Outcome.bExecuted);
	TestEqual(TEXT("Mutation applied"), Actors[0]->GetFolderPath(), FName(TEXT("UE5MCPTests/FromBridge")));

	const TSharedPtr<const FUE5MCPPlanRecord> Polled = Service.FindPlanRecord(Record->PlanId);
	TestTrue(TEXT("Poller sees the terminal state"),
		Polled.IsValid() && Polled->Status == EUE5MCPPlanStatus::Executed);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPServiceSupersedeTest,
	"UE5MCP.Service.PanelGenerateSupersedesPendingBridgePlan", UE5MCPTests::KernelTestFlags)
bool FUE5MCPServiceSupersedeTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	FUE5MCPEditorService& Service = UE5MCPServiceTestUtils::FreshService();
	const TArray<AActor*> Actors = UE5MCPServiceTestUtils::SelectNewActors(World, 1);

	FString RefusalCode;
	const TSharedPtr<const FUE5MCPPlanRecord> BridgeRecord = Service.SubmitPlanRequest(
		UE5MCPServiceTestUtils::MakeBridgeFolderRequest(Actors, World, TEXT("UE5MCPTests/Bridge")),
		EUE5MCPPlanSource::Bridge, RefusalCode);
	TestTrue(TEXT("Bridge plan pending"), BridgeRecord.IsValid());

	const TSharedPtr<const FUE5MCPPlanRecord> PanelRecord = Service.GeneratePanelFolderPlan(FName(TEXT("UE5MCPTests/Panel")));
	TestTrue(TEXT("Panel plan pending"),
		PanelRecord.IsValid() && PanelRecord->Status == EUE5MCPPlanStatus::PendingApproval);
	TestTrue(TEXT("Bridge plan superseded (humans win)"),
		BridgeRecord.IsValid() && BridgeRecord->Status == EUE5MCPPlanStatus::Superseded);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPServiceTransformPendsAndAppliesTest,
	"UE5MCP.Service.TransformPlanPendsUntilApprovalThenApplies", UE5MCPTests::KernelTestFlags)
bool FUE5MCPServiceTransformPendsAndAppliesTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	FUE5MCPEditorService& Service = UE5MCPServiceTestUtils::FreshService();
	const TArray<AActor*> Actors = UE5MCPTests::SpawnTransformableTestActors(World, 2);
	GEditor->GetEditorSubsystem<UEditorActorSubsystem>()->SetSelectedLevelActors(Actors);

	const FVector Target(123.0, 456.0, 789.0);
	FString RefusalCode;
	const TSharedPtr<const FUE5MCPPlanRecord> Record = Service.SubmitPlanRequest(
		UE5MCPServiceTestUtils::MakeTransformRequest(Actors, World, Target), EUE5MCPPlanSource::Bridge, RefusalCode);

	TestTrue(TEXT("Transform plan accepted as pending"),
		Record.IsValid() && Record->Status == EUE5MCPPlanStatus::PendingApproval);
	TestFalse(TEXT("Actor not moved before approval"), Actors[0]->GetActorLocation().Equals(Target));

	const FUE5MCPApprovalOutcome Outcome = Service.ApproveCurrentPlan();
	TestTrue(TEXT("Human approval executed the transform plan"), Outcome.bExecuted);
	TestTrue(TEXT("Location applied after approval"), Actors[0]->GetActorLocation().Equals(Target));
	TestTrue(TEXT("Location applied to every target"), Actors[1]->GetActorLocation().Equals(Target));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPServiceTransformStaleTest,
	"UE5MCP.Service.TransformApproveRefusedAfterSelectionChange", UE5MCPTests::KernelTestFlags)
bool FUE5MCPServiceTransformStaleTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	FUE5MCPEditorService& Service = UE5MCPServiceTestUtils::FreshService();
	const TArray<AActor*> Actors = UE5MCPTests::SpawnTransformableTestActors(World, 2);
	GEditor->GetEditorSubsystem<UEditorActorSubsystem>()->SetSelectedLevelActors(Actors);

	const FVector Target(11.0, 22.0, 33.0);
	FString RefusalCode;
	const TSharedPtr<const FUE5MCPPlanRecord> Record = Service.SubmitPlanRequest(
		UE5MCPServiceTestUtils::MakeTransformRequest(Actors, World, Target), EUE5MCPPlanSource::Bridge, RefusalCode);
	TestTrue(TEXT("Transform plan pending"), Record.IsValid() && Record->Status == EUE5MCPPlanStatus::PendingApproval);

	// Selection changes after preview generation -> the stale-context guard must refuse.
	GEditor->GetEditorSubsystem<UEditorActorSubsystem>()->SelectNothing();

	const FUE5MCPApprovalOutcome Outcome = Service.ApproveCurrentPlan();
	TestFalse(TEXT("Approval refused after selection change"), Outcome.bExecuted);
	TestEqual(TEXT("Refusal code is stale_context"), Outcome.RefusalCode, FString(TEXT("stale_context")));
	TestFalse(TEXT("Actor not moved by the refused plan"), Actors[0]->GetActorLocation().Equals(Target));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPServiceExternalDisabledTest,
	"UE5MCP.Service.ExternalPlanRefusedWhenSettingDisabled", UE5MCPTests::KernelTestFlags)
bool FUE5MCPServiceExternalDisabledTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	FUE5MCPEditorService& Service = UE5MCPServiceTestUtils::FreshService();
	const TArray<AActor*> Actors = UE5MCPServiceTestUtils::SelectNewActors(World, 1);

	// Default-off: a mutating external plan must be refused outright.
	UE5MCPTests::FScopedExternalApprovalSettings Scoped(/*bAllowExternal=*/false);
	FString RefusalCode;
	const TSharedPtr<const FUE5MCPPlanRecord> Record = Service.SubmitExternalPlan(
		UE5MCPServiceTestUtils::MakeBridgeFolderRequest(Actors, World, TEXT("UE5MCPTests/ExternalOff")), RefusalCode);

	TestFalse(TEXT("No record while disabled"), Record.IsValid());
	TestEqual(TEXT("Refusal names the disabled gate"), RefusalCode, FString(TEXT("external_approval_disabled")));
	TestEqual(TEXT("Actor untouched"), Actors[0]->GetFolderPath(), FName(NAME_None));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPServiceExternalExecutesImmediatelyTest,
	"UE5MCP.Service.ExternalPlanExecutesWithoutPanelClickAndUndoes", UE5MCPTests::KernelTestFlags)
bool FUE5MCPServiceExternalExecutesImmediatelyTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	FUE5MCPEditorService& Service = UE5MCPServiceTestUtils::FreshService();
	const TArray<AActor*> Actors = UE5MCPServiceTestUtils::SelectNewActors(World, 2);

	UE5MCPTests::FScopedExternalApprovalSettings Scoped(/*bAllowExternal=*/true);
	FString RefusalCode;
	const TSharedPtr<const FUE5MCPPlanRecord> Record = Service.SubmitExternalPlan(
		UE5MCPServiceTestUtils::MakeBridgeFolderRequest(Actors, World, TEXT("UE5MCPTests/ExternalRun")), RefusalCode);

	// The in-session prompt was the approval: no pending slot, immediate execution.
	TestTrue(TEXT("External plan executed immediately"),
		Record.IsValid() && Record->Status == EUE5MCPPlanStatus::Executed);
	TestTrue(TEXT("Record carries the external-approval audit flag"),
		Record.IsValid() && Record->bExternalSessionApproval);
	TestFalse(TEXT("No pending slot occupied"), Service.IsApprovalAvailable());
	TestEqual(TEXT("Mutation applied"), Actors[0]->GetFolderPath(), FName(TEXT("UE5MCPTests/ExternalRun")));

	// Externally approved batches stay one standard undo step.
	TestTrue(TEXT("UndoTransaction performed an undo"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo restored the folder"), Actors[0]->GetFolderPath(), FName(NAME_None));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPServiceExternalStaleWorldTest,
	"UE5MCP.Service.ExternalPlanRefusedOnWorldMismatch", UE5MCPTests::KernelTestFlags)
bool FUE5MCPServiceExternalStaleWorldTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	FUE5MCPEditorService& Service = UE5MCPServiceTestUtils::FreshService();
	const TArray<AActor*> Actors = UE5MCPServiceTestUtils::SelectNewActors(World, 1);

	UE5MCPTests::FScopedExternalApprovalSettings Scoped(/*bAllowExternal=*/true);
	FUE5MCPPlanRequest Request = UE5MCPServiceTestUtils::MakeBridgeFolderRequest(Actors, World, TEXT("UE5MCPTests/StaleExternal"));
	Request.Fingerprint.SceneName = TEXT("SomeOtherWorld");

	FString RefusalCode;
	const TSharedPtr<const FUE5MCPPlanRecord> Record = Service.SubmitExternalPlan(Request, RefusalCode);

	TestTrue(TEXT("Record is terminal RefusedStale"),
		Record.IsValid() && Record->Status == EUE5MCPPlanStatus::RefusedStale);
	TestEqual(TEXT("Refusal code is stale_context"), RefusalCode, FString(TEXT("stale_context")));
	TestEqual(TEXT("Actor untouched"), Actors[0]->GetFolderPath(), FName(NAME_None));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPServicePendingPathRefusesDestructiveTest,
	"UE5MCP.Service.PendingPathRefusesDestructivePlans", UE5MCPTests::KernelTestFlags)
bool FUE5MCPServicePendingPathRefusesDestructiveTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	FUE5MCPEditorService& Service = UE5MCPServiceTestUtils::FreshService();
	const TArray<AActor*> Actors = UE5MCPServiceTestUtils::SelectNewActors(World, 1);

	FUE5MCPPlanRequest Request = UE5MCPServiceTestUtils::MakeBridgeFolderRequest(Actors, World, TEXT("unused"));
	Request.bRequiresSecondConfirmation = true;
	Request.Actions[0].ToolName = TEXT("delete_actor");
	Request.Actions[0].RiskString = TEXT("destructive");
	Request.Actions[0].FolderPath = NAME_None;
	Request.Actions[0].ProvidedParamKeys.Empty();

	// The single-click pending path must never stage a destructive plan, no matter
	// the source: delete only flows through the external always-prompt gate.
	FString RefusalCode;
	const TSharedPtr<const FUE5MCPPlanRecord> Record =
		Service.SubmitPlanRequest(Request, EUE5MCPPlanSource::Bridge, RefusalCode);

	TestTrue(TEXT("Destructive plan refused on the pending path"),
		Record.IsValid() && Record->Status == EUE5MCPPlanStatus::Invalid);
	TestEqual(TEXT("Refusal names the external gate"),
		RefusalCode, FString(TEXT("destructive_requires_external_gate")));
	TestFalse(TEXT("No pending slot occupied"), Service.IsApprovalAvailable());
	TestTrue(TEXT("Actor still alive"), IsValid(Actors[0]));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPServiceExternalDestructiveTest,
	"UE5MCP.Service.ExternalDestructiveExecutesAndBeltAndSuspendersPends", UE5MCPTests::KernelTestFlags)
bool FUE5MCPServiceExternalDestructiveTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	FUE5MCPEditorService& Service = UE5MCPServiceTestUtils::FreshService();

	const auto MakeDeleteRequest = [World](const TArray<AActor*>& Actors)
	{
		FUE5MCPPlanRequest Request;
		Request.SchemaVersion = 1;
		Request.Summary = TEXT("External destructive test");
		Request.bRequiresApproval = true;
		Request.bRequiresSecondConfirmation = true;
		Request.bHasContextFingerprint = true;
		Request.Fingerprint.SceneName = World->GetName();
		const TArray<AActor*> Selected = GEditor->GetEditorSubsystem<UEditorActorSubsystem>()->GetSelectedLevelActors();
		for (const AActor* Actor : Selected)
		{
			Request.Fingerprint.SelectedActorPaths.Add(Actor->GetPathName());
		}

		FUE5MCPActionRequest Action;
		Action.Id = TEXT("xd1");
		Action.ToolName = TEXT("delete_actor");
		Action.RiskString = TEXT("destructive");
		for (const AActor* Actor : Actors)
		{
			Action.TargetPaths.Add(Actor->GetPathName());
		}
		Request.Actions.Add(MoveTemp(Action));
		return Request;
	};

	// Default external mode: the in-session prompt (always shown for delete_actor)
	// is the gate; the delete executes immediately and stays undoable.
	{
		const TArray<AActor*> Doomed = UE5MCPTests::SpawnStaticMeshTestActors(World, 2, TEXT("UE5MCPXDel"));
		GEditor->GetEditorSubsystem<UEditorActorSubsystem>()->SelectNothing();
		UE5MCPTests::FScopedExternalApprovalSettings Scoped(/*bAllowExternal=*/true);

		FString RefusalCode;
		const TSharedPtr<const FUE5MCPPlanRecord> Record = Service.SubmitExternalPlan(MakeDeleteRequest(Doomed), RefusalCode);
		TestTrue(TEXT("External destructive plan executed"),
			Record.IsValid() && Record->Status == EUE5MCPPlanStatus::Executed);
		TestEqual(TEXT("Actors deleted"), UE5MCPTests::CountActorsLabeled(TEXT("UE5MCPXDel")), 0);

		TestTrue(TEXT("UndoTransaction performed an undo"), GEditor->UndoTransaction());
		TestEqual(TEXT("Undo restored the deleted actors"), UE5MCPTests::CountActorsLabeled(TEXT("UE5MCPXDel")), 2);
	}

	// Belt-and-suspenders mode: the destructive plan pends for the ADDITIONAL
	// in-editor click; the panel approval then executes it.
	{
		Service.ResetForTests();
		const TArray<AActor*> Doomed = UE5MCPTests::SpawnStaticMeshTestActors(World, 1, TEXT("UE5MCPXDel2"));
		GEditor->GetEditorSubsystem<UEditorActorSubsystem>()->SelectNothing();
		UE5MCPTests::FScopedExternalApprovalSettings Scoped(/*bAllowExternal=*/true, /*bInEditorConfirmDestructive=*/true);

		FString RefusalCode;
		const TSharedPtr<const FUE5MCPPlanRecord> Record = Service.SubmitExternalPlan(MakeDeleteRequest(Doomed), RefusalCode);
		TestTrue(TEXT("Destructive plan pends for the in-editor confirm"),
			Record.IsValid() && Record->Status == EUE5MCPPlanStatus::PendingApproval);
		TestEqual(TEXT("Nothing deleted before the confirm"), UE5MCPTests::CountActorsLabeled(TEXT("UE5MCPXDel2")), 1);

		const FUE5MCPApprovalOutcome Outcome = Service.ApproveCurrentPlan();
		TestTrue(TEXT("In-editor confirm executed the destructive plan"), Outcome.bExecuted);
		TestEqual(TEXT("Actor deleted after the confirm"), UE5MCPTests::CountActorsLabeled(TEXT("UE5MCPXDel2")), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPServicePreviewOnlyTest,
	"UE5MCP.Service.PreviewRequestNeverExecutes", UE5MCPTests::KernelTestFlags)
bool FUE5MCPServicePreviewOnlyTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	FUE5MCPEditorService& Service = UE5MCPServiceTestUtils::FreshService();
	const TArray<AActor*> Actors = UE5MCPServiceTestUtils::SelectNewActors(World, 2);

	FString RefusalCode;
	const TSharedPtr<const FUE5MCPPlanRecord> Record = Service.PreviewPlanRequest(
		UE5MCPServiceTestUtils::MakeBridgeFolderRequest(Actors, World, TEXT("UE5MCPTests/PreviewNever")),
		EUE5MCPPlanSource::Bridge, RefusalCode);

	TestTrue(TEXT("Preview record produced"),
		Record.IsValid() && Record->Status == EUE5MCPPlanStatus::PreviewedOnly);
	TestTrue(TEXT("Preview text built"),
		Record.IsValid() && Record->Plan.Actions.Num() == 1 &&
		Record->Plan.Actions[0].PreviewText.Contains(TEXT("set_actor_folder")));
	TestFalse(TEXT("No pending slot occupied"), Service.IsApprovalAvailable());
	TestEqual(TEXT("Nothing executed"), Actors[0]->GetFolderPath(), FName(NAME_None));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPServiceReadOnlyImmediateTest,
	"UE5MCP.Service.ReadOnlyPlanExecutesImmediately", UE5MCPTests::KernelTestFlags)
bool FUE5MCPServiceReadOnlyImmediateTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	FUE5MCPEditorService& Service = UE5MCPServiceTestUtils::FreshService();
	UE5MCPTests::SpawnTestActors(World, 3);

	FUE5MCPPlanRequest Request;
	Request.SchemaVersion = 1;
	Request.Summary = TEXT("Read-only find");
	FUE5MCPActionRequest Action;
	Action.Id = TEXT("ro-1");
	Action.ToolName = TEXT("find_actors");
	Action.RiskString = TEXT("read_only");
	Action.FindQuery.LabelContains = TEXT("UE5MCPTestActor");
	Action.ProvidedParamKeys = { TEXT("label_contains") };
	Request.Actions.Add(Action);

	FString RefusalCode;
	const TSharedPtr<const FUE5MCPPlanRecord> Record =
		Service.SubmitPlanRequest(Request, EUE5MCPPlanSource::Bridge, RefusalCode);

	TestTrue(TEXT("Read-only plan executed immediately"),
		Record.IsValid() && Record->Status == EUE5MCPPlanStatus::Executed);
	TestTrue(TEXT("Results attached"),
		Record.IsValid() && Record->Result.ActionResults.Num() == 1 &&
		Record->Result.ActionResults[0].FoundActors.Num() == 3);
	TestFalse(TEXT("No pending slot occupied"), Service.IsApprovalAvailable());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
