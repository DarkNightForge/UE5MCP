// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "CoreMinimal.h"
#include "Editor.h"
#include "Engine/PointLight.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Tests/AutomationEditorCommon.h"
#include "UE5MCPActionExecutor.h"
#include "UE5MCPContextCollector.h"
#include "UE5MCPEditorService.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPReadLogsFiltersAndCapsTest,
	"UE5MCP.Tools.ReadLogsReturnsFilteredRecentLines", UE5MCPTests::KernelTestFlags)
bool FUE5MCPReadLogsFiltersAndCapsTest::RunTest(const FString& Parameters)
{
	// Seed the shared structured log with uniquely-marked lines so the assertions
	// hold regardless of whatever else the buffer already contains.
	FUE5MCPLog& Log = FUE5MCPEditorService::Get().GetLog();
	const FString Needle = TEXT("UE5MCPReadLogsMarker_4711");
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Log.Append(FString::Printf(TEXT("%s line %d"), *Needle, Index));
	}

	FUE5MCPResolvedAction Resolved;
	Resolved.Action.Id = TEXT("test-read-logs");
	Resolved.Action.Type = EUE5MCPActionType::ReadLogs;
	Resolved.Action.Risk = EUE5MCPRiskLevel::ReadOnly;
	Resolved.Action.ReadLogsQuery.Contains = Needle;
	Resolved.Action.ReadLogsQuery.MaxLines = 3;

	const FUE5MCPExecutionResult Result = FUE5MCPActionExecutor::ExecuteApprovedPlan(
		UE5MCPTests::WrapPlanForTest(Resolved));

	TestTrue(TEXT("Read-only read_logs executed"), Result.bSuccess);
	TestEqual(TEXT("One action result"), Result.ActionResults.Num(), 1);
	if (Result.ActionResults.Num() == 1)
	{
		const FUE5MCPActionResult& Action = Result.ActionResults[0];
		TestEqual(TEXT("Capped to the 3 most recent matching lines"), Action.LogLines.Num(), 3);
		// The cap drops the oldest matches and keeps chronological order (line 2,3,4).
		TestTrue(TEXT("Drops the oldest, keeps the newest matching lines"),
			Action.LogLines.Num() == 3
			&& Action.LogLines[0].Contains(TEXT("line 2"))
			&& Action.LogLines[2].Contains(TEXT("line 4")));
		bool bAllMatch = true;
		for (const FString& Line : Action.LogLines)
		{
			bAllMatch &= Line.Contains(Needle);
		}
		TestTrue(TEXT("Every returned line matches the filter"), bAllMatch);
	}
	TestTrue(TEXT("Message reports the read-only readback"),
		UE5MCPTests::LogLinesContain(Result.UserVisibleLogLines, TEXT("read_logs returned")));

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPGetActorPropertiesTest,
	"UE5MCP.Tools.GetActorPropertiesListsAllowlistedWithValues", UE5MCPTests::KernelTestFlags)
bool FUE5MCPGetActorPropertiesTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}

	APointLight* Light = World->SpawnActor<APointLight>();
	if (!TestNotNull(TEXT("Spawned a point light"), Light))
	{
		return false;
	}

	// --- Allowlisted, editable discovery on the point light component ---
	FUE5MCPResolvedAction Resolved;
	Resolved.Action.Id = TEXT("test-get-properties");
	Resolved.Action.Type = EUE5MCPActionType::GetActorProperties;
	Resolved.Action.Risk = EUE5MCPRiskLevel::ReadOnly;
	Resolved.Action.PropertyComponentClass = TEXT("/Script/Engine.PointLightComponent");
	Resolved.Action.GetPropertiesQuery.bAllowlistedOnly = true;
	Resolved.Action.GetPropertiesQuery.bEditableOnly = true;
	Resolved.Action.GetPropertiesQuery.MaxProperties = 50;
	Resolved.Action.TargetActors.Add(Light);

	const FUE5MCPExecutionResult Result =
		FUE5MCPActionExecutor::ExecuteApprovedPlan(UE5MCPTests::WrapPlanForTest(Resolved));
	TestTrue(TEXT("Read-only get_actor_properties executed"), Result.bSuccess);
	TestEqual(TEXT("One action result"), Result.ActionResults.Num(), 1);
	if (Result.ActionResults.Num() == 1)
	{
		const FUE5MCPActionResult& Action = Result.ActionResults[0];
		TestTrue(TEXT("Result carries properties"), Action.bHasProperties);
		TestTrue(TEXT("Inspected owner is the point light component"),
			Action.InspectedOwnerClass.Contains(TEXT("PointLightComponent")));

		const FUE5MCPPropertySummary* Intensity = Action.Properties.FindByPredicate(
			[](const FUE5MCPPropertySummary& P) { return P.Name == TEXT("Intensity"); });
		const FUE5MCPPropertySummary* LightColor = Action.Properties.FindByPredicate(
			[](const FUE5MCPPropertySummary& P) { return P.Name == TEXT("LightColor"); });

		if (TestNotNull(TEXT("Intensity is listed (allowlisted)"), Intensity))
		{
			TestTrue(TEXT("Intensity is flagged allowlisted"), Intensity->bAllowlisted);
			TestTrue(TEXT("Intensity is flagged editable"), Intensity->bEditable);
			TestEqual(TEXT("Intensity allowed_type is float"), Intensity->AllowedType, FString(TEXT("float")));
			TestTrue(TEXT("Intensity carries the configured range"), Intensity->bHasRange);
			TestTrue(TEXT("Intensity range max is the configured 1e6"), FMath::IsNearlyEqual(Intensity->RangeMax, 1000000.0, 0.01));
			TestFalse(TEXT("Intensity current_value is non-empty"), Intensity->CurrentValue.IsEmpty());
		}
		TestNotNull(TEXT("LightColor is listed (allowlisted)"), LightColor);

		// allowlisted_only must not leak a non-allowlisted property like AttenuationRadius.
		const bool bLeaked = Action.Properties.ContainsByPredicate(
			[](const FUE5MCPPropertySummary& P) { return P.Name == TEXT("AttenuationRadius"); });
		TestFalse(TEXT("Non-allowlisted property not leaked under allowlisted_only"), bLeaked);
	}

	// --- Ambiguous component: two of the same component class refuses, not guesses ---
	AActor* Multi = World->SpawnActor<AActor>();
	if (TestNotNull(TEXT("Spawned a bare actor"), Multi))
	{
		USceneComponent* Root = NewObject<USceneComponent>(Multi, TEXT("Root"), RF_Transactional);
		Multi->SetRootComponent(Root);
		Root->RegisterComponent();
		Multi->AddInstanceComponent(Root);
		for (int32 Index = 0; Index < 2; ++Index)
		{
			UPointLightComponent* Comp = NewObject<UPointLightComponent>(Multi, NAME_None, RF_Transactional);
			Comp->RegisterComponent();
			Multi->AddInstanceComponent(Comp);
		}

		FUE5MCPResolvedAction Ambiguous;
		Ambiguous.Action.Id = TEXT("test-get-properties-ambiguous");
		Ambiguous.Action.Type = EUE5MCPActionType::GetActorProperties;
		Ambiguous.Action.Risk = EUE5MCPRiskLevel::ReadOnly;
		Ambiguous.Action.PropertyComponentClass = TEXT("/Script/Engine.PointLightComponent");
		Ambiguous.Action.TargetActors.Add(Multi);

		const FUE5MCPExecutionResult AmbiguousResult =
			FUE5MCPActionExecutor::ExecuteApprovedPlan(UE5MCPTests::WrapPlanForTest(Ambiguous));
		TestFalse(TEXT("Ambiguous component inspection refused"), AmbiguousResult.bSuccess);
		if (AmbiguousResult.ActionResults.Num() == 1)
		{
			TestEqual(TEXT("Refusal code is ambiguous_component"),
				AmbiguousResult.ActionResults[0].RefusalCode, FString(TEXT("ambiguous_component")));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
