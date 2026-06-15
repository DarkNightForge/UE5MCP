// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/StaticMeshComponent.h"
#include "CoreMinimal.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Tests/AutomationEditorCommon.h"
#include "UE5MCPActionExecutor.h"
#include "UE5MCPContextCollector.h"
#include "UE5MCPTestHelpers.h"
#include "UE5MCPTypes.h"

// Headless-safe editor tests for the UE5MCP first-proof safety kernel.
// Run with:
//   UnrealEditor-Cmd <project>.uproject -ExecCmds="Automation RunTests UE5MCP;Quit" \
//     -unattended -nullrhi -nosplash -ReportExportPath=<dir>
// Results are authoritative in the project's Saved/Logs/<project>.log and the
// exported index.json — captured stdout can truncate when the trace server forks.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPExecutorSetFolderUndoRedoTest,
	"UE5MCP.Executor.SetFolderAppliesThenUndoRedoReverts", UE5MCPTests::KernelTestFlags)
bool FUE5MCPExecutorSetFolderUndoRedoTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}

	const TArray<AActor*> Actors = UE5MCPTests::SpawnTestActors(World, 3);
	TestEqual(TEXT("Spawned three test actors"), Actors.Num(), 3);

	const FName TargetFolder(TEXT("UE5MCPTests/Organized"));
	const FUE5MCPExecutionResult Result =
		FUE5MCPActionExecutor::ExecuteApprovedPlan(UE5MCPTests::BuildSetFolderPlan(Actors, TargetFolder));

	TestTrue(TEXT("Execution succeeded"), Result.bSuccess);
	TestEqual(TEXT("One action result"), Result.ActionResults.Num(), 1);
	for (const AActor* Actor : Actors)
	{
		TestEqual(TEXT("Actor moved to the target folder"), Actor->GetFolderPath(), TargetFolder);
	}

	// The approved batch must revert as a single standard undo step (checklist M9).
	TestTrue(TEXT("UndoTransaction performed an undo"), GEditor->UndoTransaction());
	for (const AActor* Actor : Actors)
	{
		TestEqual(TEXT("Undo restored the original folder"), Actor->GetFolderPath(), FName(NAME_None));
	}

	// And redo must re-apply it (checklist M10).
	TestTrue(TEXT("RedoTransaction performed a redo"), GEditor->RedoTransaction());
	for (const AActor* Actor : Actors)
	{
		TestEqual(TEXT("Redo re-applied the folder"), Actor->GetFolderPath(), TargetFolder);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPExecutorSetLabelUndoRedoTest,
	"UE5MCP.Executor.SetLabelAppliesThenUndoRedoReverts", UE5MCPTests::KernelTestFlags)
bool FUE5MCPExecutorSetLabelUndoRedoTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}

	const TArray<AActor*> Actors = UE5MCPTests::SpawnStaticMeshTestActors(World, 1, TEXT("UE5MCPLabelSource"));
	TestEqual(TEXT("Spawned one test actor"), Actors.Num(), 1);
	const FString OriginalLabel = Actors[0]->GetActorLabel();

	const FString NewLabel(TEXT("Hero Spawn Point"));
	const FUE5MCPExecutionResult Result =
		FUE5MCPActionExecutor::ExecuteApprovedPlan(UE5MCPTests::BuildSetLabelPlan(Actors, NewLabel));

	TestTrue(TEXT("Execution succeeded"), Result.bSuccess);
	TestEqual(TEXT("Actor carries the new label"), Actors[0]->GetActorLabel(), NewLabel);

	// The approved batch must revert as a single standard undo step (checklist M9).
	TestTrue(TEXT("UndoTransaction performed an undo"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo restored the original label"), Actors[0]->GetActorLabel(), OriginalLabel);

	// And redo must re-apply it (checklist M10).
	TestTrue(TEXT("RedoTransaction performed a redo"), GEditor->RedoTransaction());
	TestEqual(TEXT("Redo re-applied the new label"), Actors[0]->GetActorLabel(), NewLabel);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPExecutorAddRemoveTagsUndoTest,
	"UE5MCP.Executor.AddThenRemoveTagsWithUndo", UE5MCPTests::KernelTestFlags)
bool FUE5MCPExecutorAddRemoveTagsUndoTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}

	const TArray<AActor*> Actors = UE5MCPTests::SpawnStaticMeshTestActors(World, 2, TEXT("UE5MCPTagSource"));
	TestEqual(TEXT("Spawned two test actors"), Actors.Num(), 2);

	const TArray<FName> Tags = { FName(TEXT("Rock")), FName(TEXT("Cleanup")) };

	// --- Add the tags ---
	const FUE5MCPExecutionResult AddResult =
		FUE5MCPActionExecutor::ExecuteApprovedPlan(UE5MCPTests::BuildTagsPlan(Actors, EUE5MCPActionType::AddActorTags, Tags));
	TestTrue(TEXT("Add succeeded"), AddResult.bSuccess);
	for (const AActor* Actor : Actors)
	{
		TestTrue(TEXT("Actor has the 'Rock' tag"), Actor->Tags.Contains(FName(TEXT("Rock"))));
		TestTrue(TEXT("Actor has the 'Cleanup' tag"), Actor->Tags.Contains(FName(TEXT("Cleanup"))));
	}

	// --- One undo reverts the whole add batch as a single step (checklist M9) ---
	TestTrue(TEXT("UndoTransaction performed an undo"), GEditor->UndoTransaction());
	for (const AActor* Actor : Actors)
	{
		TestFalse(TEXT("Undo removed the 'Rock' tag"), Actor->Tags.Contains(FName(TEXT("Rock"))));
		TestFalse(TEXT("Undo removed the 'Cleanup' tag"), Actor->Tags.Contains(FName(TEXT("Cleanup"))));
	}

	// --- Redo re-applies it (checklist M10) ---
	TestTrue(TEXT("RedoTransaction performed a redo"), GEditor->RedoTransaction());
	for (const AActor* Actor : Actors)
	{
		TestTrue(TEXT("Redo restored the 'Rock' tag"), Actor->Tags.Contains(FName(TEXT("Rock"))));
	}

	// --- Re-adding the same tags is idempotent: success, but nothing changes ---
	const FUE5MCPExecutionResult ReAddResult =
		FUE5MCPActionExecutor::ExecuteApprovedPlan(UE5MCPTests::BuildTagsPlan(Actors, EUE5MCPActionType::AddActorTags, Tags));
	TestTrue(TEXT("Idempotent re-add still reports success"), ReAddResult.bSuccess);
	if (ReAddResult.ActionResults.Num() == 1)
	{
		TestEqual(TEXT("Idempotent re-add mutated nothing"), ReAddResult.ActionResults[0].ChangedCount, 0);
	}

	// --- Remove one tag; the other is preserved ---
	const TArray<FName> RemoveTags = { FName(TEXT("Cleanup")) };
	const FUE5MCPExecutionResult RemoveResult =
		FUE5MCPActionExecutor::ExecuteApprovedPlan(UE5MCPTests::BuildTagsPlan(Actors, EUE5MCPActionType::RemoveActorTags, RemoveTags));
	TestTrue(TEXT("Remove succeeded"), RemoveResult.bSuccess);
	for (const AActor* Actor : Actors)
	{
		TestFalse(TEXT("'Cleanup' tag removed"), Actor->Tags.Contains(FName(TEXT("Cleanup"))));
		TestTrue(TEXT("'Rock' tag preserved"), Actor->Tags.Contains(FName(TEXT("Rock"))));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPExecutorRejectsUnvalidatedPlanTest,
	"UE5MCP.Executor.RejectsUnvalidatedPlan", UE5MCPTests::KernelTestFlags)
bool FUE5MCPExecutorRejectsUnvalidatedPlanTest::RunTest(const FString& Parameters)
{
	const FUE5MCPExecutionResult Result = FUE5MCPActionExecutor::ExecuteApprovedPlan(FUE5MCPValidatedPlan());

	TestFalse(TEXT("Unvalidated plan did not succeed"), Result.bSuccess);
	TestTrue(TEXT("Nothing executed (refused upfront)"), Result.ActionResults.IsEmpty());
	TestTrue(TEXT("Refusal reason logged"),
		UE5MCPTests::LogLinesContain(Result.UserVisibleLogLines, TEXT("plan was not validated")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPExecutorRejectsEmptyFolderPathTest,
	"UE5MCP.Executor.RejectsEmptyFolderPath", UE5MCPTests::KernelTestFlags)
bool FUE5MCPExecutorRejectsEmptyFolderPathTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}

	const TArray<AActor*> Actors = UE5MCPTests::SpawnTestActors(World, 1);
	const FUE5MCPExecutionResult Result =
		FUE5MCPActionExecutor::ExecuteApprovedPlan(UE5MCPTests::BuildSetFolderPlan(Actors, NAME_None));

	TestFalse(TEXT("Empty folder path did not succeed"), Result.bSuccess);
	TestEqual(TEXT("Action ran and reported a per-action rejection"), Result.ActionResults.Num(), 1);
	TestTrue(TEXT("Rejection names the empty folder path"),
		UE5MCPTests::LogLinesContain(Result.UserVisibleLogLines, TEXT("folder path was empty")));
	TestEqual(TEXT("Actor folder unchanged"), Actors[0]->GetFolderPath(), FName(NAME_None));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPExecutorRejectsUnimplementedActionTest,
	"UE5MCP.Executor.RejectsUnimplementedActionTypes", UE5MCPTests::KernelTestFlags)
bool FUE5MCPExecutorRejectsUnimplementedActionTest::RunTest(const FString& Parameters)
{
	FUE5MCPResolvedAction Resolved;
	Resolved.Action.Id = TEXT("test-unknown-action");
	// Synthetic out-of-range action type: the executor must reject anything outside
	// the implemented allowlist via its default case.
	Resolved.Action.Type = static_cast<EUE5MCPActionType>(250);
	Resolved.Action.Risk = EUE5MCPRiskLevel::ReadOnly;

	FUE5MCPValidatedPlan Plan;
	Plan.Actions.Add(Resolved);
	Plan.bIsValid = true;

	const FUE5MCPExecutionResult Result = FUE5MCPActionExecutor::ExecuteApprovedPlan(Plan);

	TestFalse(TEXT("Unimplemented action did not succeed"), Result.bSuccess);
	TestEqual(TEXT("One per-action rejection"), Result.ActionResults.Num(), 1);
	TestTrue(TEXT("Rejection says the action is not implemented"),
		UE5MCPTests::LogLinesContain(Result.UserVisibleLogLines, TEXT("not implemented")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPExecutorBlocksMutationDuringPlayModeTest,
	"UE5MCP.Executor.BlocksMutationDuringPlayMode", UE5MCPTests::KernelTestFlags)
bool FUE5MCPExecutorBlocksMutationDuringPlayModeTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}

	const TArray<AActor*> Actors = UE5MCPTests::SpawnTestActors(World, 1);
	const FUE5MCPValidatedPlan Plan =
		UE5MCPTests::BuildSetFolderPlan(Actors, FName(TEXT("UE5MCPTests/ShouldNeverApply")));

	{
		// Simulate an active play session for the synchronous guard check only. Scoped
		// guard restores PlayWorld before control returns to the engine tick; holding a
		// fake PlayWorld across frames would desynchronize PIE-dependent systems.
		TGuardValue<TObjectPtr<UWorld>> PlayWorldGuard(GEditor->PlayWorld, World);

		FString BlockReason;
		TestTrue(TEXT("Mutation guard reports blocked"), FUE5MCPActionExecutor::IsEditorMutationBlocked(&BlockReason));
		TestTrue(TEXT("Block reason names PIE/SIE"), BlockReason.Contains(TEXT("PIE/SIE")));

		const FUE5MCPExecutionResult Result = FUE5MCPActionExecutor::ExecuteApprovedPlan(Plan);
		TestFalse(TEXT("Plan did not succeed during play mode"), Result.bSuccess);
		TestTrue(TEXT("Nothing executed (refused upfront, plan stays approvable)"), Result.ActionResults.IsEmpty());
		TestTrue(TEXT("Refusal logged"),
			UE5MCPTests::LogLinesContain(Result.UserVisibleLogLines, TEXT("Rejected:")));
	}

	TestFalse(TEXT("Mutation guard clears after play mode ends"), FUE5MCPActionExecutor::IsEditorMutationBlocked());
	TestEqual(TEXT("Actor folder untouched by the blocked plan"), Actors[0]->GetFolderPath(), FName(NAME_None));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPExecutorSetTransformUndoRedoTest,
	"UE5MCP.Executor.SetTransformAppliesThenUndoRedoReverts", UE5MCPTests::KernelTestFlags)
bool FUE5MCPExecutorSetTransformUndoRedoTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}

	const TArray<AActor*> Actors = UE5MCPTests::SpawnTransformableTestActors(World, 2);
	TestEqual(TEXT("Spawned two transformable actors"), Actors.Num(), 2);

	TArray<FTransform> Originals;
	for (const AActor* Actor : Actors)
	{
		Originals.Add(Actor->GetActorTransform());
	}

	const FVector TargetLocation(100.0, 200.0, 300.0);
	const FVector TargetScale(2.0, 2.0, 2.0);
	FUE5MCPTransformDelta Delta;
	Delta.bHasLocation = true;
	Delta.Location = TargetLocation;
	Delta.bHasScale = true;
	Delta.Scale = TargetScale;

	const FUE5MCPExecutionResult Result =
		FUE5MCPActionExecutor::ExecuteApprovedPlan(UE5MCPTests::BuildSetTransformPlan(Actors, Delta));

	TestTrue(TEXT("Execution succeeded"), Result.bSuccess);
	TestEqual(TEXT("One action result"), Result.ActionResults.Num(), 1);
	if (Result.ActionResults.Num() == 1)
	{
		TestEqual(TEXT("Both actors changed"), Result.ActionResults[0].ChangedCount, 2);
		// Per-actor structured results carry the post-mutation transform.
		TestEqual(TEXT("Per-actor results returned for both actors"), Result.ActionResults[0].FoundActors.Num(), 2);
	}
	for (const AActor* Actor : Actors)
	{
		TestTrue(TEXT("Location applied"), Actor->GetActorLocation().Equals(TargetLocation));
		TestTrue(TEXT("Scale applied"), Actor->GetActorScale3D().Equals(TargetScale));
	}

	// The approved batch must revert as a single standard undo step (checklist M9).
	TestTrue(TEXT("UndoTransaction performed an undo"), GEditor->UndoTransaction());
	for (int32 Index = 0; Index < Actors.Num(); ++Index)
	{
		TestTrue(TEXT("Undo restored the original location"),
			Actors[Index]->GetActorLocation().Equals(Originals[Index].GetLocation()));
		TestTrue(TEXT("Undo restored the original scale"),
			Actors[Index]->GetActorScale3D().Equals(Originals[Index].GetScale3D()));
	}

	// And redo must re-apply it (checklist M10).
	TestTrue(TEXT("RedoTransaction performed a redo"), GEditor->RedoTransaction());
	for (const AActor* Actor : Actors)
	{
		TestTrue(TEXT("Redo re-applied the location"), Actor->GetActorLocation().Equals(TargetLocation));
		TestTrue(TEXT("Redo re-applied the scale"), Actor->GetActorScale3D().Equals(TargetScale));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPExecutorRejectsNoOpTransformTest,
	"UE5MCP.Executor.RejectsNoOpTransform", UE5MCPTests::KernelTestFlags)
bool FUE5MCPExecutorRejectsNoOpTransformTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}

	const TArray<AActor*> Actors = UE5MCPTests::SpawnTransformableTestActors(World, 1);
	const FTransform Original = Actors[0]->GetActorTransform();

	// A transform delta with no fields set is a no-op and must be refused.
	const FUE5MCPTransformDelta EmptyDelta;
	const FUE5MCPExecutionResult Result =
		FUE5MCPActionExecutor::ExecuteApprovedPlan(UE5MCPTests::BuildSetTransformPlan(Actors, EmptyDelta));

	TestFalse(TEXT("No-op transform did not succeed"), Result.bSuccess);
	TestEqual(TEXT("Action ran and reported a per-action rejection"), Result.ActionResults.Num(), 1);
	TestTrue(TEXT("Rejection names the missing transform fields"),
		UE5MCPTests::LogLinesContain(Result.UserVisibleLogLines, TEXT("no transform fields")));
	TestTrue(TEXT("Actor transform unchanged"), Actors[0]->GetActorTransform().Equals(Original));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPExecutorBlocksTransformDuringPlayModeTest,
	"UE5MCP.Executor.BlocksTransformDuringPlayMode", UE5MCPTests::KernelTestFlags)
bool FUE5MCPExecutorBlocksTransformDuringPlayModeTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}

	const TArray<AActor*> Actors = UE5MCPTests::SpawnTransformableTestActors(World, 1);
	const FTransform Original = Actors[0]->GetActorTransform();

	FUE5MCPTransformDelta Delta;
	Delta.bHasLocation = true;
	Delta.Location = FVector(50.0, 50.0, 50.0);
	const FUE5MCPValidatedPlan Plan = UE5MCPTests::BuildSetTransformPlan(Actors, Delta);

	{
		TGuardValue<TObjectPtr<UWorld>> PlayWorldGuard(GEditor->PlayWorld, World);
		const FUE5MCPExecutionResult Result = FUE5MCPActionExecutor::ExecuteApprovedPlan(Plan);
		TestFalse(TEXT("Transform refused during play mode"), Result.bSuccess);
		TestTrue(TEXT("Nothing executed (refused upfront)"), Result.ActionResults.IsEmpty());
	}

	TestTrue(TEXT("Transform untouched by the blocked plan"), Actors[0]->GetActorTransform().Equals(Original));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPExecutorDuplicateUndoTest,
	"UE5MCP.Executor.DuplicateAppliesThenUndoRemovesCopies", UE5MCPTests::KernelTestFlags)
bool FUE5MCPExecutorDuplicateUndoTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}

	const TArray<AActor*> Sources = UE5MCPTests::SpawnStaticMeshTestActors(World, 2, TEXT("UE5MCPDupSource"));
	Sources[0]->SetActorLocation(FVector(0.0, 0.0, 0.0));
	Sources[1]->SetActorLocation(FVector(0.0, 100.0, 0.0));
	const int32 CountBefore = UE5MCPTests::CountActorsLabeled(TEXT("UE5MCPDupSource"));
	TestEqual(TEXT("Two source actors present"), CountBefore, 2);

	const FVector Offset(250.0, 0.0, 0.0);
	const FUE5MCPExecutionResult Result =
		FUE5MCPActionExecutor::ExecuteApprovedPlan(UE5MCPTests::BuildDuplicatePlan(Sources, Offset));

	TestTrue(TEXT("Execution succeeded"), Result.bSuccess);
	if (TestEqual(TEXT("One action result"), Result.ActionResults.Num(), 1))
	{
		TestEqual(TEXT("Two duplicates created"), Result.ActionResults[0].ChangedCount, 2);
		TestEqual(TEXT("Structured results carry the new copies"), Result.ActionResults[0].FoundActors.Num(), 2);
		// The duplicates land at source location + offset.
		for (const FUE5MCPActorSummary& Summary : Result.ActionResults[0].FoundActors)
		{
			TestTrue(TEXT("Duplicate offset applied"),
				Summary.Transform.GetLocation().X > 200.0);
		}
	}
	TestEqual(TEXT("Level now holds the copies"), UE5MCPTests::CountActorsLabeled(TEXT("UE5MCPDupSource")), 4);

	// The whole approved batch (including the subsystem's nested transaction) must
	// revert as ONE standard undo step.
	TestTrue(TEXT("UndoTransaction performed an undo"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo removed both copies"), UE5MCPTests::CountActorsLabeled(TEXT("UE5MCPDupSource")), 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPExecutorSpawnUndoTest,
	"UE5MCP.Executor.SpawnAllowlistedClassThenUndoRemoves", UE5MCPTests::KernelTestFlags)
bool FUE5MCPExecutorSpawnUndoTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}

	TArray<FUE5MCPSpawnInstance> Instances;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FUE5MCPSpawnInstance Instance;
		Instance.Location = FVector(Index * 200.0, 0.0, 50.0);
		Instance.Scale = FVector(1.0, 1.0, 2.0);
		Instances.Add(Instance);
	}

	const FUE5MCPExecutionResult Result = FUE5MCPActionExecutor::ExecuteApprovedPlan(
		UE5MCPTests::BuildSpawnPlan(TEXT("/Script/Engine.StaticMeshActor"), Instances,
			TEXT("/Engine/BasicShapes/Cube.Cube"), TEXT("UE5MCPSpawned")));

	TestTrue(TEXT("Execution succeeded"), Result.bSuccess);
	if (TestEqual(TEXT("One action result"), Result.ActionResults.Num(), 1))
	{
		TestEqual(TEXT("Three actors spawned"), Result.ActionResults[0].ChangedCount, 3);
		TestEqual(TEXT("Structured results carry the spawned actors"), Result.ActionResults[0].FoundActors.Num(), 3);
	}
	TestEqual(TEXT("Spawned actors present and labeled"), UE5MCPTests::CountActorsLabeled(TEXT("UE5MCPSpawned")), 3);

	// Verify placement, scale, and the allowlisted mesh actually landed.
	UEditorActorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
	int32 VerifiedMeshes = 0;
	for (AActor* Actor : Subsystem->GetAllLevelActors())
	{
		if (!IsValid(Actor) || !Actor->GetActorLabel().Contains(TEXT("UE5MCPSpawned")))
		{
			continue;
		}
		TestTrue(TEXT("Spawn scale applied"), Actor->GetActorScale3D().Equals(FVector(1.0, 1.0, 2.0)));
		if (const AStaticMeshActor* MeshActor = Cast<AStaticMeshActor>(Actor))
		{
			const UStaticMeshComponent* Component = MeshActor->GetStaticMeshComponent();
			if (Component && Component->GetStaticMesh() &&
				Component->GetStaticMesh()->GetPathName() == TEXT("/Engine/BasicShapes/Cube.Cube"))
			{
				++VerifiedMeshes;
			}
		}
	}
	TestEqual(TEXT("Allowlisted mesh assigned to every spawn"), VerifiedMeshes, 3);

	// One undo step removes the whole spawned batch.
	TestTrue(TEXT("UndoTransaction performed an undo"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo removed every spawned actor"), UE5MCPTests::CountActorsLabeled(TEXT("UE5MCPSpawned")), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPExecutorSpawnRefusesNonAllowlistedTest,
	"UE5MCP.Executor.SpawnRefusesNonAllowlistedClassAndMesh", UE5MCPTests::KernelTestFlags)
bool FUE5MCPExecutorSpawnRefusesNonAllowlistedTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}

	FUE5MCPSpawnInstance Instance;
	Instance.Location = FVector::ZeroVector;

	// Class outside the allowlist: refused by the executor's defence-in-depth check
	// even though this hand-built plan skipped the validator.
	{
		const FUE5MCPExecutionResult Result = FUE5MCPActionExecutor::ExecuteApprovedPlan(
			UE5MCPTests::BuildSpawnPlan(TEXT("/Script/Engine.DirectionalLight"), { Instance }));
		TestFalse(TEXT("Non-allowlisted class did not succeed"), Result.bSuccess);
		TestEqual(TEXT("One per-action refusal"), Result.ActionResults.Num(), 1);
		if (Result.ActionResults.Num() == 1)
		{
			TestEqual(TEXT("Refusal code names the allowlist"),
				Result.ActionResults[0].RefusalCode, FString(TEXT("class_not_allowlisted")));
		}
	}

	// Allowlisted class but non-allowlisted mesh: also refused.
	{
		const FUE5MCPExecutionResult Result = FUE5MCPActionExecutor::ExecuteApprovedPlan(
			UE5MCPTests::BuildSpawnPlan(TEXT("/Script/Engine.StaticMeshActor"), { Instance },
				TEXT("/Engine/EngineMeshes/Sphere.Sphere")));
		TestFalse(TEXT("Non-allowlisted mesh did not succeed"), Result.bSuccess);
		if (TestEqual(TEXT("One per-action refusal"), Result.ActionResults.Num(), 1))
		{
			TestEqual(TEXT("Refusal code names the mesh allowlist"),
				Result.ActionResults[0].RefusalCode, FString(TEXT("mesh_not_allowlisted")));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPExecutorDeleteUndoTest,
	"UE5MCP.Executor.DeleteRemovesThenUndoRestores", UE5MCPTests::KernelTestFlags)
bool FUE5MCPExecutorDeleteUndoTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}

	const TArray<AActor*> Actors = UE5MCPTests::SpawnStaticMeshTestActors(World, 2, TEXT("UE5MCPDoomed"));
	TestEqual(TEXT("Two doomed actors present"), UE5MCPTests::CountActorsLabeled(TEXT("UE5MCPDoomed")), 2);

	const FUE5MCPExecutionResult Result =
		FUE5MCPActionExecutor::ExecuteApprovedPlan(UE5MCPTests::BuildDeletePlan(Actors));

	TestTrue(TEXT("Execution succeeded"), Result.bSuccess);
	if (TestEqual(TEXT("One action result"), Result.ActionResults.Num(), 1))
	{
		TestEqual(TEXT("Both actors destroyed"), Result.ActionResults[0].ChangedCount, 2);
		// The audit summary recorded what was deleted BEFORE destruction.
		TestEqual(TEXT("Audit records the deleted actors"), Result.ActionResults[0].FoundActors.Num(), 2);
	}
	TestEqual(TEXT("Actors gone after delete"), UE5MCPTests::CountActorsLabeled(TEXT("UE5MCPDoomed")), 0);

	// THE destructive-tier requirement: one standard undo step resurrects the batch.
	TestTrue(TEXT("UndoTransaction performed an undo"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo restored both deleted actors"), UE5MCPTests::CountActorsLabeled(TEXT("UE5MCPDoomed")), 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPCollectorCapsLoadedActorsTest,
	"UE5MCP.Collector.CapsLoadedActorsAndWarns", UE5MCPTests::KernelTestFlags)
bool FUE5MCPCollectorCapsLoadedActorsTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}

	UE5MCPTests::SpawnTestActors(World, 6);

	const FUE5MCPContextPack Capped = FUE5MCPContextCollector::Collect(/*MaxLoadedActors=*/3);
	TestEqual(TEXT("Loaded actor list capped at the limit"), Capped.LoadedActorsCapped.Num(), 3);
	TestTrue(TEXT("Cap warning emitted"),
		UE5MCPTests::LogLinesContain(Capped.Warnings, TEXT("capped at 3")));

	const FUE5MCPContextPack Uncapped = FUE5MCPContextCollector::Collect();
	TestFalse(TEXT("No cap warning under the default limit"),
		UE5MCPTests::LogLinesContain(Uncapped.Warnings, TEXT("capped at")));
	TestTrue(TEXT("All six spawned actors visible under the default limit"),
		Uncapped.LoadedActorsCapped.Num() >= 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPCollectorReportsSelectionTest,
	"UE5MCP.Collector.ReportsSelectionAndWorld", UE5MCPTests::KernelTestFlags)
bool FUE5MCPCollectorReportsSelectionTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}

	UEditorActorSubsystem* ActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
	if (!TestNotNull(TEXT("EditorActorSubsystem available"), ActorSubsystem))
	{
		return false;
	}

	const TArray<AActor*> Actors = UE5MCPTests::SpawnTestActors(World, 3);
	ActorSubsystem->SetSelectedLevelActors({ Actors[0], Actors[1] });

	const FUE5MCPContextPack Context = FUE5MCPContextCollector::Collect();
	TestEqual(TEXT("Context reports the editor world"), Context.WorldName, World->GetName());
	TestEqual(TEXT("Exactly the two selected actors reported"), Context.SelectedActors.Num(), 2);
	for (const FUE5MCPActorSummary& Summary : Context.SelectedActors)
	{
		TestTrue(TEXT("Selected summary is flagged selected"), Summary.bSelected);
		TestTrue(TEXT("Selected summary maps to a selected test actor"),
			Summary.ActorPath == Actors[0]->GetPathName() || Summary.ActorPath == Actors[1]->GetPathName());
	}

	ActorSubsystem->SelectNothing();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
