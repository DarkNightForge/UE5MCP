// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "UE5MCPPlanValidator.h"
#include "UE5MCPTestHelpers.h"
#include "UE5MCPTypes.h"

namespace UE5MCPValidatorTestUtils
{
	// A structurally valid folder-move request; individual tests break one rule at a time.
	FUE5MCPPlanRequest MakeFolderRequest(const TArray<FString>& TargetPaths, const FString& SceneName)
	{
		FUE5MCPPlanRequest Request;
		Request.SchemaVersion = 1;
		Request.Summary = TEXT("Validator test plan");
		Request.bRequiresApproval = true;
		Request.bHasContextFingerprint = true;
		Request.Fingerprint.SceneName = SceneName;
		Request.Fingerprint.SelectedActorPaths = TargetPaths;

		FUE5MCPActionRequest Action;
		Action.Id = TEXT("a1");
		Action.ToolName = TEXT("set_actor_folder");
		Action.RiskString = TEXT("low_risk");
		Action.TargetPaths = TargetPaths;
		Action.FolderPath = FName(TEXT("UE5MCPTests/Validated"));
		Action.ProvidedParamKeys = { TEXT("folder_path") };
		Request.Actions.Add(Action);
		return Request;
	}

	// A structurally valid set_actor_transform request; bWithLocation toggles whether
	// any transform field is present (off = the no-op case the validator must refuse).
	FUE5MCPPlanRequest MakeTransformRequest(const TArray<FString>& TargetPaths, const FString& SceneName, bool bWithLocation)
	{
		FUE5MCPPlanRequest Request;
		Request.SchemaVersion = 1;
		Request.Summary = TEXT("Validator transform test plan");
		Request.bRequiresApproval = true;
		Request.bHasContextFingerprint = true;
		Request.Fingerprint.SceneName = SceneName;
		Request.Fingerprint.SelectedActorPaths = TargetPaths;

		FUE5MCPActionRequest Action;
		Action.Id = TEXT("t1");
		Action.ToolName = TEXT("set_actor_transform");
		Action.RiskString = TEXT("low_risk");
		Action.TargetPaths = TargetPaths;
		if (bWithLocation)
		{
			Action.Transform.bHasLocation = true;
			Action.Transform.Location = FVector(10.0, 20.0, 30.0);
			Action.ProvidedParamKeys = { TEXT("location") };
		}
		Request.Actions.Add(Action);
		return Request;
	}

	bool HasRule(const FUE5MCPPlanValidationResult& Result, const TCHAR* RulePrefix)
	{
		for (const FString& Problem : Result.Problems)
		{
			if (Problem.StartsWith(RulePrefix))
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPValidatorRulesTest,
	"UE5MCP.Validator.EnforcesFormatRules", UE5MCPTests::KernelTestFlags)
bool FUE5MCPValidatorRulesTest::RunTest(const FString& Parameters)
{
	using namespace UE5MCPValidatorTestUtils;

	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	const TArray<AActor*> Actors = UE5MCPTests::SpawnTestActors(World, 2);
	TArray<FString> Paths;
	for (const AActor* Actor : Actors)
	{
		Paths.Add(Actor->GetPathName());
	}
	const FString Scene = World->GetName();

	// R1: wrong schema version.
	{
		FUE5MCPPlanRequest Request = MakeFolderRequest(Paths, Scene);
		Request.SchemaVersion = 99;
		TestTrue(TEXT("R1 unknown schema version"), HasRule(FUE5MCPPlanValidator::ValidateAndResolve(Request), TEXT("R1")));
	}
	// R2: empty actions / duplicate ids.
	{
		FUE5MCPPlanRequest Request = MakeFolderRequest(Paths, Scene);
		Request.Actions.Empty();
		TestTrue(TEXT("R2 empty actions"), HasRule(FUE5MCPPlanValidator::ValidateAndResolve(Request), TEXT("R2")));

		Request = MakeFolderRequest(Paths, Scene);
		const FUE5MCPActionRequest DuplicateAction = Request.Actions[0];
		Request.Actions.Add(DuplicateAction);
		TestTrue(TEXT("R2 duplicate ids"), HasRule(FUE5MCPPlanValidator::ValidateAndResolve(Request), TEXT("R2")));
	}
	// R3: unknown tool.
	{
		FUE5MCPPlanRequest Request = MakeFolderRequest(Paths, Scene);
		Request.Actions[0].ToolName = TEXT("run_script");
		TestTrue(TEXT("R3 unknown tool"), HasRule(FUE5MCPPlanValidator::ValidateAndResolve(Request), TEXT("R3")));
	}
	// R4: risk mismatch (client cannot downgrade risk).
	{
		FUE5MCPPlanRequest Request = MakeFolderRequest(Paths, Scene);
		Request.Actions[0].RiskString = TEXT("read_only");
		TestTrue(TEXT("R4 risk mismatch"), HasRule(FUE5MCPPlanValidator::ValidateAndResolve(Request), TEXT("R4")));
	}
	// R5: mutation without approval flag.
	{
		FUE5MCPPlanRequest Request = MakeFolderRequest(Paths, Scene);
		Request.bRequiresApproval = false;
		TestTrue(TEXT("R5 mutation without approval"), HasRule(FUE5MCPPlanValidator::ValidateAndResolve(Request), TEXT("R5")));
	}
	// R6: empty mutation targets; unresolvable target.
	{
		FUE5MCPPlanRequest Request = MakeFolderRequest({}, Scene);
		TestTrue(TEXT("R6 empty targets"), HasRule(FUE5MCPPlanValidator::ValidateAndResolve(Request), TEXT("R6")));

		Request = MakeFolderRequest({ TEXT("PersistentLevel.Nope_42") }, Scene);
		TestTrue(TEXT("R6 unresolvable target"), HasRule(FUE5MCPPlanValidator::ValidateAndResolve(Request), TEXT("R6")));
	}
	// R8: mutation without context fingerprint.
	{
		FUE5MCPPlanRequest Request = MakeFolderRequest(Paths, Scene);
		Request.bHasContextFingerprint = false;
		Request.Fingerprint = FUE5MCPContextFingerprint();
		TestTrue(TEXT("R8 missing fingerprint"), HasRule(FUE5MCPPlanValidator::ValidateAndResolve(Request), TEXT("R8")));
	}
	// R9: unknown param; missing required folder_path.
	{
		FUE5MCPPlanRequest Request = MakeFolderRequest(Paths, Scene);
		Request.Actions[0].ProvidedParamKeys.Add(TEXT("mode"));
		TestTrue(TEXT("R9 unknown param"), HasRule(FUE5MCPPlanValidator::ValidateAndResolve(Request), TEXT("R9")));

		Request = MakeFolderRequest(Paths, Scene);
		Request.Actions[0].FolderPath = NAME_None;
		TestTrue(TEXT("R9 missing required param"), HasRule(FUE5MCPPlanValidator::ValidateAndResolve(Request), TEXT("R9")));
	}
	// R10: over the target cap.
	{
		FUE5MCPPlanRequest Request = MakeFolderRequest(Paths, Scene);
		Request.Actions[0].TargetPaths.Empty();
		for (int32 Index = 0; Index < 201; ++Index)
		{
			Request.Actions[0].TargetPaths.Add(FString::Printf(TEXT("PersistentLevel.Fake_%d"), Index));
		}
		TestTrue(TEXT("R10 over target cap"), HasRule(FUE5MCPPlanValidator::ValidateAndResolve(Request), TEXT("R10")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPValidatorSpawnAndDestructiveRulesTest,
	"UE5MCP.Validator.SpawnPolicyAndDestructiveRules", UE5MCPTests::KernelTestFlags)
bool FUE5MCPValidatorSpawnAndDestructiveRulesTest::RunTest(const FString& Parameters)
{
	using namespace UE5MCPValidatorTestUtils;

	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	const TArray<AActor*> Actors = UE5MCPTests::SpawnTestActors(World, 1);
	const FString Scene = World->GetName();

	const auto MakeSpawnRequest = [&Scene](const FString& ClassPath, int32 InstanceCount)
	{
		FUE5MCPPlanRequest Request;
		Request.SchemaVersion = 1;
		Request.Summary = TEXT("Validator spawn test plan");
		Request.bRequiresApproval = true;
		Request.bHasContextFingerprint = true;
		Request.Fingerprint.SceneName = Scene;

		FUE5MCPActionRequest Action;
		Action.Id = TEXT("s1");
		Action.ToolName = TEXT("spawn_actor_from_class");
		Action.RiskString = TEXT("low_risk");
		Action.SpawnClassPath = ClassPath;
		for (int32 Index = 0; Index < InstanceCount; ++Index)
		{
			FUE5MCPSpawnInstance Instance;
			Instance.Location = FVector(Index * 100.0, 0.0, 0.0);
			Action.SpawnInstances.Add(Instance);
		}
		Action.ProvidedParamKeys = { TEXT("class_path"), TEXT("transforms") };
		Request.Actions.Add(MoveTemp(Action));
		return Request;
	};

	// A valid allowlisted spawn passes and previews every placement.
	{
		const FUE5MCPPlanValidationResult Result =
			FUE5MCPPlanValidator::ValidateAndResolve(MakeSpawnRequest(TEXT("/Script/Engine.StaticMeshActor"), 2));
		TestTrue(TEXT("Allowlisted spawn accepted"), Result.IsValid());
		if (Result.Plan.Actions.Num() == 1)
		{
			TestTrue(TEXT("Preview names the class"),
				Result.Plan.Actions[0].PreviewText.Contains(TEXT("StaticMeshActor")));
		}
	}
	// R11: class outside the allowlist refused.
	{
		const FUE5MCPPlanValidationResult Result =
			FUE5MCPPlanValidator::ValidateAndResolve(MakeSpawnRequest(TEXT("/Script/Engine.DirectionalLight"), 1));
		TestFalse(TEXT("Non-allowlisted class rejected"), Result.IsValid());
		TestTrue(TEXT("R11 fires for the class allowlist"), HasRule(Result, TEXT("R11")));
	}
	// R11: non-allowlisted mesh, and mesh on a non-StaticMeshActor class.
	{
		FUE5MCPPlanRequest Request = MakeSpawnRequest(TEXT("/Script/Engine.StaticMeshActor"), 1);
		Request.Actions[0].SpawnMeshPath = TEXT("/Game/Evil/Mesh.Mesh");
		Request.Actions[0].ProvidedParamKeys.Add(TEXT("static_mesh"));
		TestTrue(TEXT("R11 fires for the mesh allowlist"),
			HasRule(FUE5MCPPlanValidator::ValidateAndResolve(Request), TEXT("R11")));

		Request = MakeSpawnRequest(TEXT("/Script/Engine.PointLight"), 1);
		Request.Actions[0].SpawnMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
		Request.Actions[0].ProvidedParamKeys.Add(TEXT("static_mesh"));
		TestTrue(TEXT("R11 fires for mesh on a non-mesh class"),
			HasRule(FUE5MCPPlanValidator::ValidateAndResolve(Request), TEXT("R11")));
	}
	// R10: spawn instance cap.
	{
		const FUE5MCPPlanValidationResult Result = FUE5MCPPlanValidator::ValidateAndResolve(
			MakeSpawnRequest(TEXT("/Script/Engine.StaticMeshActor"), FUE5MCPPlanValidator::MaxSpawnInstancesPerAction + 1));
		TestTrue(TEXT("R10 fires over the spawn cap"), HasRule(Result, TEXT("R10")));
	}
	// R6: spawn does not accept targets.
	{
		FUE5MCPPlanRequest Request = MakeSpawnRequest(TEXT("/Script/Engine.StaticMeshActor"), 1);
		Request.Actions[0].TargetPaths.Add(Actors[0]->GetPathName());
		TestTrue(TEXT("R6 fires for targets on spawn"),
			HasRule(FUE5MCPPlanValidator::ValidateAndResolve(Request), TEXT("R6")));
	}

	const auto MakeDeleteRequest = [&Scene, &Actors](bool bSecondConfirmation)
	{
		FUE5MCPPlanRequest Request;
		Request.SchemaVersion = 1;
		Request.Summary = TEXT("Validator delete test plan");
		Request.bRequiresApproval = true;
		Request.bRequiresSecondConfirmation = bSecondConfirmation;
		Request.bHasContextFingerprint = true;
		Request.Fingerprint.SceneName = Scene;

		FUE5MCPActionRequest Action;
		Action.Id = TEXT("d1");
		Action.ToolName = TEXT("delete_actor");
		Action.RiskString = TEXT("destructive");
		Action.TargetPaths.Add(Actors[0]->GetPathName());
		Request.Actions.Add(MoveTemp(Action));
		return Request;
	};

	// R7: destructive without the second-confirmation flag is refused...
	{
		const FUE5MCPPlanValidationResult Result =
			FUE5MCPPlanValidator::ValidateAndResolve(MakeDeleteRequest(/*bSecondConfirmation=*/false));
		TestFalse(TEXT("Destructive without second confirmation rejected"), Result.IsValid());
		TestTrue(TEXT("R7 fires"), HasRule(Result, TEXT("R7")));
	}
	// ...but WITH it, the destructive plan is now schema-valid (the execution gate
	// moved to the service: external-session approval only).
	{
		const FUE5MCPPlanValidationResult Result =
			FUE5MCPPlanValidator::ValidateAndResolve(MakeDeleteRequest(/*bSecondConfirmation=*/true));
		TestTrue(TEXT("Destructive with second confirmation is schema-valid"), Result.IsValid());
		if (Result.Plan.Actions.Num() == 1)
		{
			TestTrue(TEXT("Preview shouts DESTRUCTIVE"),
				Result.Plan.Actions[0].PreviewText.Contains(TEXT("DESTRUCTIVE")));
		}
	}
	// R9: duplicate without its required offset.
	{
		FUE5MCPPlanRequest Request = MakeDeleteRequest(true);
		Request.Actions[0].ToolName = TEXT("duplicate_actor_with_offset");
		Request.Actions[0].RiskString = TEXT("low_risk");
		Request.bRequiresSecondConfirmation = false;
		TestTrue(TEXT("R9 fires for duplicate without offset"),
			HasRule(FUE5MCPPlanValidator::ValidateAndResolve(Request), TEXT("R9")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPValidatorAcceptsValidPlanTest,
	"UE5MCP.Validator.AcceptsValidFolderPlanAndBuildsPreview", UE5MCPTests::KernelTestFlags)
bool FUE5MCPValidatorAcceptsValidPlanTest::RunTest(const FString& Parameters)
{
	using namespace UE5MCPValidatorTestUtils;

	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	const TArray<AActor*> Actors = UE5MCPTests::SpawnTestActors(World, 2);
	TArray<FString> Paths;
	for (const AActor* Actor : Actors)
	{
		Paths.Add(Actor->GetPathName());
	}

	const FUE5MCPPlanValidationResult Result =
		FUE5MCPPlanValidator::ValidateAndResolve(MakeFolderRequest(Paths, World->GetName()));

	TestTrue(TEXT("Valid plan accepted"), Result.IsValid());
	TestTrue(TEXT("Plan flagged valid"), Result.Plan.bIsValid);
	TestTrue(TEXT("Plan requires approval"), Result.Plan.bRequiresApproval);
	TestEqual(TEXT("One resolved action"), Result.Plan.Actions.Num(), 1);
	if (Result.Plan.Actions.Num() == 1)
	{
		TestEqual(TEXT("Both targets resolved"), Result.Plan.Actions[0].Action.TargetActors.Num(), 2);
		TestTrue(TEXT("Preview names the exact count"),
			Result.Plan.Actions[0].PreviewText.Contains(TEXT("move 2 actor(s)")));
		TestEqual(TEXT("Target labels captured"), Result.Plan.Actions[0].TargetLabels.Num(), 2);
	}
	TestEqual(TEXT("Fingerprint scene captured"), Result.Plan.ContextWorldName, World->GetName());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPValidatorTransformTest,
	"UE5MCP.Validator.TransformPlanRulesAndPreview", UE5MCPTests::KernelTestFlags)
bool FUE5MCPValidatorTransformTest::RunTest(const FString& Parameters)
{
	using namespace UE5MCPValidatorTestUtils;

	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	const TArray<AActor*> Actors = UE5MCPTests::SpawnTransformableTestActors(World, 2);
	Actors[0]->SetActorLocation(FVector(5.0, 5.0, 5.0));
	TArray<FString> Paths;
	for (const AActor* Actor : Actors)
	{
		Paths.Add(Actor->GetPathName());
	}
	const FString Scene = World->GetName();

	// Valid transform plan: resolves targets and builds a before/after preview.
	{
		const FUE5MCPPlanValidationResult Result =
			FUE5MCPPlanValidator::ValidateAndResolve(MakeTransformRequest(Paths, Scene, /*bWithLocation=*/true));
		TestTrue(TEXT("Valid transform plan accepted"), Result.IsValid());
		TestTrue(TEXT("Plan requires approval (mutation)"), Result.Plan.bRequiresApproval);
		if (TestEqual(TEXT("One resolved action"), Result.Plan.Actions.Num(), 1))
		{
			const FString& Preview = Result.Plan.Actions[0].PreviewText;
			TestTrue(TEXT("Preview names the transform tool"), Preview.Contains(TEXT("set_actor_transform")));
			TestTrue(TEXT("Preview names the new location"), Preview.Contains(TEXT("location->")));
			TestTrue(TEXT("Preview names the affected count"), Preview.Contains(TEXT("to 2 actor(s)")));
			TestTrue(TEXT("Preview carries a per-actor before/after row"), Preview.Contains(TEXT("loc ")));
			TestTrue(TEXT("Both targets resolved"), Result.Plan.Actions[0].Action.TargetActors.Num() == 2);
		}
	}

	// No transform field => no-op mutation => refused (R9).
	{
		const FUE5MCPPlanValidationResult Result =
			FUE5MCPPlanValidator::ValidateAndResolve(MakeTransformRequest(Paths, Scene, /*bWithLocation=*/false));
		TestFalse(TEXT("No-op transform rejected"), Result.IsValid());
		TestTrue(TEXT("R9 fires for the empty transform"), HasRule(Result, TEXT("R9")));
	}

	// Unresolvable target => refused (R6).
	{
		const FUE5MCPPlanValidationResult Result = FUE5MCPPlanValidator::ValidateAndResolve(
			MakeTransformRequest({ TEXT("PersistentLevel.NoSuchActor_77") }, Scene, /*bWithLocation=*/true));
		TestFalse(TEXT("Invalid-target transform rejected"), Result.IsValid());
		TestTrue(TEXT("R6 fires for the missing target"), HasRule(Result, TEXT("R6")));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
