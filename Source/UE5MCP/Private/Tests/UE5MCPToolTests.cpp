// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Tests/AutomationEditorCommon.h"
#include "UE5MCPActionExecutor.h"
#include "UE5MCPContextCollector.h"
#include "UE5MCPTargetResolver.h"
#include "UE5MCPTestHelpers.h"
#include "UE5MCPTypes.h"

namespace UE5MCPToolTestUtils
{
	FUE5MCPResolvedAction MakeFindAction(const FUE5MCPFindActorsQuery& Query)
	{
		FUE5MCPResolvedAction Resolved;
		Resolved.Action.Id = TEXT("test-find");
		Resolved.Action.Type = EUE5MCPActionType::FindActors;
		Resolved.Action.Risk = EUE5MCPRiskLevel::ReadOnly;
		Resolved.Action.FindQuery = Query;
		return Resolved;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPResolverRoundTripTest,
	"UE5MCP.Resolver.ResolvesContextActorPathsBackToActors", UE5MCPTests::KernelTestFlags)
bool FUE5MCPResolverRoundTripTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}

	const TArray<AActor*> Actors = UE5MCPTests::SpawnTestActors(World, 3);

	// Full object paths, exactly as the context pack reports them.
	for (AActor* Actor : Actors)
	{
		AActor* Resolved = FUE5MCPTargetResolver::ResolveActorByPath(Actor->GetPathName());
		TestEqual(TEXT("Full path resolves to the same actor"), Resolved, Actor);
	}

	// Editor-world-relative form ("PersistentLevel.Name").
	const FString WorldPrefix = World->GetPathName() + TEXT(":");
	for (AActor* Actor : Actors)
	{
		FString RelativePath = Actor->GetPathName();
		TestTrue(TEXT("Actor path carries the world prefix"), RelativePath.StartsWith(WorldPrefix));
		RelativePath.RightChopInline(WorldPrefix.Len());
		AActor* Resolved = FUE5MCPTargetResolver::ResolveActorByPath(RelativePath);
		TestEqual(TEXT("World-relative path resolves to the same actor"), Resolved, Actor);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPResolverMissingPathTest,
	"UE5MCP.Resolver.ReportsMissingActorPaths", UE5MCPTests::KernelTestFlags)
bool FUE5MCPResolverMissingPathTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}

	const TArray<AActor*> Actors = UE5MCPTests::SpawnTestActors(World, 1);

	TArray<FString> MissingPaths;
	const TArray<TWeakObjectPtr<AActor>> Resolved = FUE5MCPTargetResolver::ResolveActorPaths(
		{ Actors[0]->GetPathName(), TEXT("PersistentLevel.DoesNotExist_99") }, MissingPaths);

	TestEqual(TEXT("One path resolved"), Resolved.Num(), 1);
	TestEqual(TEXT("One path reported missing"), MissingPaths.Num(), 1);
	TestTrue(TEXT("Missing list names the bogus path"),
		MissingPaths.Num() == 1 && MissingPaths[0].Contains(TEXT("DoesNotExist_99")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPFindActorsByLabelTest,
	"UE5MCP.Tools.FindActorsFiltersByLabelSubstring", UE5MCPTests::KernelTestFlags)
bool FUE5MCPFindActorsByLabelTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	UE5MCPTests::SpawnTestActors(World, 5);

	FUE5MCPFindActorsQuery Query;
	Query.LabelContains = TEXT("ue5mcptestactor_1");	// case-insensitive
	bool bTruncated = false;
	TArray<AActor*> Found = FUE5MCPTargetResolver::FindActors(Query, bTruncated);
	TestEqual(TEXT("Exact label substring matches one actor"), Found.Num(), 1);

	Query.LabelContains = TEXT("UE5MCPTestActor");
	Found = FUE5MCPTargetResolver::FindActors(Query, bTruncated);
	TestEqual(TEXT("Shared label prefix matches all five"), Found.Num(), 5);
	TestFalse(TEXT("No truncation under the cap"), bTruncated);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPFindActorsByFacetsTest,
	"UE5MCP.Tools.FindActorsFiltersByClassTagFolderSelected", UE5MCPTests::KernelTestFlags)
bool FUE5MCPFindActorsByFacetsTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	const TArray<AActor*> Actors = UE5MCPTests::SpawnTestActors(World, 3);

	Actors[0]->Tags.Add(FName(TEXT("ue5mcp_test_tag")));
	Actors[1]->SetFolderPath(FName(TEXT("UE5MCPTests/FindMe")));
	UEditorActorSubsystem* ActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
	ActorSubsystem->SetSelectedLevelActors({ Actors[2] });

	bool bTruncated = false;

	FUE5MCPFindActorsQuery TagQuery;
	TagQuery.Tag = FName(TEXT("ue5mcp_test_tag"));
	TestEqual(TEXT("Tag filter matches one actor"),
		FUE5MCPTargetResolver::FindActors(TagQuery, bTruncated).Num(), 1);

	FUE5MCPFindActorsQuery FolderQuery;
	FolderQuery.FolderPath = FName(TEXT("UE5MCPTests"));
	TestEqual(TEXT("Folder prefix filter matches one actor"),
		FUE5MCPTargetResolver::FindActors(FolderQuery, bTruncated).Num(), 1);

	FUE5MCPFindActorsQuery SelectedQuery;
	SelectedQuery.bSelectedOnly = true;
	const TArray<AActor*> SelectedFound = FUE5MCPTargetResolver::FindActors(SelectedQuery, bTruncated);
	TestEqual(TEXT("Selected-only filter matches one actor"), SelectedFound.Num(), 1);
	TestTrue(TEXT("Selected-only filter found the selected actor"),
		SelectedFound.Num() == 1 && SelectedFound[0] == Actors[2]);

	FUE5MCPFindActorsQuery ClassQuery;
	ClassQuery.ClassPath = TEXT("/Script/Engine.Actor");
	ClassQuery.LabelContains = TEXT("UE5MCPTestActor");
	TestEqual(TEXT("Class filter (exact class) matches the three plain actors"),
		FUE5MCPTargetResolver::FindActors(ClassQuery, bTruncated).Num(), 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPFindActorsCapTest,
	"UE5MCP.Tools.FindActorsCapsResultsAndWarns", UE5MCPTests::KernelTestFlags)
bool FUE5MCPFindActorsCapTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	UE5MCPTests::SpawnTestActors(World, 6);

	FUE5MCPFindActorsQuery Query;
	Query.LabelContains = TEXT("UE5MCPTestActor");
	Query.MaxResults = 3;

	const FUE5MCPExecutionResult Result = FUE5MCPActionExecutor::ExecuteApprovedPlan(
		UE5MCPTests::WrapPlanForTest(UE5MCPToolTestUtils::MakeFindAction(Query)));

	TestTrue(TEXT("Read-only plan executed"), Result.bSuccess);
	TestEqual(TEXT("One action result"), Result.ActionResults.Num(), 1);
	if (Result.ActionResults.Num() == 1)
	{
		TestEqual(TEXT("Results capped at MaxResults"), Result.ActionResults[0].FoundActors.Num(), 3);
	}
	TestTrue(TEXT("Truncation reported in the message"),
		UE5MCPTests::LogLinesContain(Result.UserVisibleLogLines, TEXT("truncated")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPGetSelectionContextToolTest,
	"UE5MCP.Tools.GetSelectionContextReturnsSelectionSummaries", UE5MCPTests::KernelTestFlags)
bool FUE5MCPGetSelectionContextToolTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	const TArray<AActor*> Actors = UE5MCPTests::SpawnTestActors(World, 3);
	GEditor->GetEditorSubsystem<UEditorActorSubsystem>()->SetSelectedLevelActors({ Actors[0], Actors[1] });

	FUE5MCPResolvedAction Resolved;
	Resolved.Action.Id = TEXT("test-context");
	Resolved.Action.Type = EUE5MCPActionType::GetSelectionContext;
	Resolved.Action.Risk = EUE5MCPRiskLevel::ReadOnly;

	const FUE5MCPExecutionResult Result = FUE5MCPActionExecutor::ExecuteApprovedPlan(
		UE5MCPTests::WrapPlanForTest(Resolved));

	TestTrue(TEXT("Read-only plan executed"), Result.bSuccess);
	TestEqual(TEXT("One action result"), Result.ActionResults.Num(), 1);
	if (Result.ActionResults.Num() == 1)
	{
		TestEqual(TEXT("Two selected actor summaries returned"), Result.ActionResults[0].FoundActors.Num(), 2);
	}
	TestTrue(TEXT("Message reports the selection count"),
		UE5MCPTests::LogLinesContain(Result.UserVisibleLogLines, TEXT("2 selected actor(s)")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPSelectActorsToolTest,
	"UE5MCP.Tools.SelectActorsAppliesSelectionAfterApproval", UE5MCPTests::KernelTestFlags)
bool FUE5MCPSelectActorsToolTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	const TArray<AActor*> Actors = UE5MCPTests::SpawnTestActors(World, 3);
	UEditorActorSubsystem* ActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
	ActorSubsystem->SelectNothing();

	FUE5MCPResolvedAction Resolved;
	Resolved.Action.Id = TEXT("test-select");
	Resolved.Action.Type = EUE5MCPActionType::SelectActors;
	Resolved.Action.Risk = EUE5MCPRiskLevel::LowMutation;
	Resolved.Action.TargetActors.Add(Actors[0]);
	Resolved.Action.TargetActors.Add(Actors[2]);

	const FUE5MCPExecutionResult Result = FUE5MCPActionExecutor::ExecuteApprovedPlan(
		UE5MCPTests::WrapPlanForTest(Resolved));

	TestTrue(TEXT("Selection plan executed"), Result.bSuccess);
	TestEqual(TEXT("Two actors reported selected"),
		Result.ActionResults.Num() == 1 ? Result.ActionResults[0].ChangedCount : -1, 2);

	const TArray<AActor*> NowSelected = ActorSubsystem->GetSelectedLevelActors();
	TestEqual(TEXT("Editor selection holds two actors"), NowSelected.Num(), 2);
	TestTrue(TEXT("Editor selection holds the requested actors"),
		NowSelected.Contains(Actors[0]) && NowSelected.Contains(Actors[2]));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPSelectActorsPlayModeTest,
	"UE5MCP.Tools.SelectActorsBlockedDuringPlayMode", UE5MCPTests::KernelTestFlags)
bool FUE5MCPSelectActorsPlayModeTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	const TArray<AActor*> Actors = UE5MCPTests::SpawnTestActors(World, 1);

	FUE5MCPResolvedAction Resolved;
	Resolved.Action.Id = TEXT("test-select-blocked");
	Resolved.Action.Type = EUE5MCPActionType::SelectActors;
	Resolved.Action.Risk = EUE5MCPRiskLevel::LowMutation;
	Resolved.Action.TargetActors.Add(Actors[0]);

	{
		TGuardValue<TObjectPtr<UWorld>> PlayWorldGuard(GEditor->PlayWorld, World);
		const FUE5MCPExecutionResult Result = FUE5MCPActionExecutor::ExecuteApprovedPlan(
			UE5MCPTests::WrapPlanForTest(Resolved));
		TestFalse(TEXT("Selection mutation refused during play mode"), Result.bSuccess);
		TestTrue(TEXT("Refused upfront (nothing executed)"), Result.ActionResults.IsEmpty());
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
