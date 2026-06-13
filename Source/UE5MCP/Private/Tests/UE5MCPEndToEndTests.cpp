// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/StaticMeshComponent.h"
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Tests/AutomationEditorCommon.h"
#include "UE5MCPBridgeServer.h"
#include "UE5MCPEditorService.h"
#include "UE5MCPTestHelpers.h"
#include "UE5MCPTypes.h"

// The machine rehearsal of the M5 north-star demo: an external client (stood in
// for by raw bridge bodies identical to what the MCP server emits) builds an
// organized scene through the execute_external path — spawn a cube grid, arrange
// transforms, organize into an outliner folder — and the whole build then fully
// reverts through standard editor Undo, one step per approved batch.

namespace UE5MCPEndToEndTestUtils
{
	TSharedPtr<FJsonObject> ParseBody(const FUE5MCPBridgeResponse& Response)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Response.Body);
		FJsonSerializer::Deserialize(Reader, Root);
		return Root;
	}

	FString Fingerprint(UWorld* World)
	{
		FString SelectedPaths;
		const TArray<AActor*> Selected = GEditor->GetEditorSubsystem<UEditorActorSubsystem>()->GetSelectedLevelActors();
		for (int32 Index = 0; Index < Selected.Num(); ++Index)
		{
			SelectedPaths += FString::Printf(TEXT("%s\"%s\""), Index ? TEXT(",") : TEXT(""), *Selected[Index]->GetPathName());
		}
		return FString::Printf(TEXT("\"context_fingerprint\":{\"scene\":\"%s\",\"selected_object_paths\":[%s]}"),
			*World->GetName(), *SelectedPaths);
	}

	FString JoinQuoted(const TArray<FString>& Values)
	{
		FString Result;
		for (int32 Index = 0; Index < Values.Num(); ++Index)
		{
			Result += FString::Printf(TEXT("%s\"%s\""), Index ? TEXT(",") : TEXT(""), *Values[Index]);
		}
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPEndToEndExternalSceneBuildAndUndoTest,
	"UE5MCP.EndToEnd.ExternalSceneBuildThenFullUndo", UE5MCPTests::KernelTestFlags)
bool FUE5MCPEndToEndExternalSceneBuildAndUndoTest::RunTest(const FString& Parameters)
{
	using namespace UE5MCPEndToEndTestUtils;

	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	FUE5MCPEditorService::Get().ResetForTests();
	GEditor->GetEditorSubsystem<UEditorActorSubsystem>()->SelectNothing();
	UE5MCPTests::FScopedExternalApprovalSettings Scoped(/*bAllowExternal=*/true);

	FUE5MCPBridgeServer Bridge;
	const TCHAR* Folder = TEXT("UE5MCP/DemoGrid");

	// --- Batch 1: spawn a 2x2 cube grid (in-session approval already happened). ---
	const FString SpawnBody = FString::Printf(TEXT(
		"{\"mode\":\"execute_external\",\"schema_version\":1,\"summary\":\"Spawn demo cube grid\","
		"\"requires_approval\":true,%s,"
		"\"actions\":[{\"id\":\"e2e-spawn\",\"tool\":\"spawn_actor_from_class\",\"risk\":\"low_risk\",\"targets\":[],"
		"\"params\":{\"class_path\":\"/Script/Engine.StaticMeshActor\",\"static_mesh\":\"/Engine/BasicShapes/Cube.Cube\","
		"\"label_base\":\"E2EDemoCube\",\"transforms\":["
		"{\"location\":[0,0,50]},{\"location\":[200,0,50]},{\"location\":[0,200,50]},{\"location\":[200,200,50]}]}}]}"),
		*Fingerprint(World));

	const FUE5MCPBridgeResponse SpawnResponse = Bridge.HandlePostPlan(SpawnBody);
	TestEqual(TEXT("Spawn batch HTTP 200"), SpawnResponse.Code, 200);
	const TSharedPtr<FJsonObject> SpawnRecord = ParseBody(SpawnResponse);
	if (!TestTrue(TEXT("Spawn response is JSON"), SpawnRecord.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("Spawn executed"), SpawnRecord->GetStringField(TEXT("status")), FString(TEXT("executed")));
	TestEqual(TEXT("Spawn approval mode is external_session"),
		SpawnRecord->GetStringField(TEXT("approval_mode")), FString(TEXT("external_session")));
	TestEqual(TEXT("Four cubes in the level"), UE5MCPTests::CountActorsLabeled(TEXT("E2EDemoCube")), 4);

	// The structured result hands the client the spawned actor paths — the same
	// way the MCP server feeds follow-up tool calls.
	TArray<FString> CubePaths;
	{
		const TSharedPtr<FJsonObject>* ResultObject = nullptr;
		if (TestTrue(TEXT("Spawn result attached"), SpawnRecord->TryGetObjectField(TEXT("result"), ResultObject)))
		{
			for (const TSharedPtr<FJsonValue>& ActionValue : (*ResultObject)->GetArrayField(TEXT("action_results")))
			{
				const TSharedPtr<FJsonObject> ActionObject = ActionValue->AsObject();
				for (const TSharedPtr<FJsonValue>& FoundValue : ActionObject->GetArrayField(TEXT("found_actors")))
				{
					CubePaths.Add(FoundValue->AsObject()->GetStringField(TEXT("path")));
				}
			}
		}
	}
	if (!TestEqual(TEXT("Spawn returned four actor paths"), CubePaths.Num(), 4))
	{
		return false;
	}

	// --- Batch 2: arrange the grid (uniform scale + a deliberate rotation). ---
	const FString TransformBody = FString::Printf(TEXT(
		"{\"mode\":\"execute_external\",\"schema_version\":1,\"summary\":\"Arrange demo grid\","
		"\"requires_approval\":true,%s,"
		"\"actions\":[{\"id\":\"e2e-arrange\",\"tool\":\"set_actor_transform\",\"risk\":\"low_risk\","
		"\"targets\":[%s],\"params\":{\"rotation\":[0,0,45],\"scale\":[1,1,2]}}]}"),
		*Fingerprint(World), *JoinQuoted(CubePaths));

	const FUE5MCPBridgeResponse TransformResponse = Bridge.HandlePostPlan(TransformBody);
	TestEqual(TEXT("Transform batch HTTP 200"), TransformResponse.Code, 200);
	const TSharedPtr<FJsonObject> TransformRecord = ParseBody(TransformResponse);
	TestTrue(TEXT("Transform executed"),
		TransformRecord.IsValid() && TransformRecord->GetStringField(TEXT("status")) == TEXT("executed"));

	// --- Batch 3: organize into a named outliner folder. ---
	const FString FolderBody = FString::Printf(TEXT(
		"{\"mode\":\"execute_external\",\"schema_version\":1,\"summary\":\"Organize demo grid\","
		"\"requires_approval\":true,%s,"
		"\"actions\":[{\"id\":\"e2e-folder\",\"tool\":\"set_actor_folder\",\"risk\":\"low_risk\","
		"\"targets\":[%s],\"params\":{\"folder_path\":\"%s\"}}]}"),
		*Fingerprint(World), *JoinQuoted(CubePaths), Folder);

	const FUE5MCPBridgeResponse FolderResponse = Bridge.HandlePostPlan(FolderBody);
	TestEqual(TEXT("Folder batch HTTP 200"), FolderResponse.Code, 200);

	// --- Verify the built scene. ---
	UEditorActorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
	int32 VerifiedCubes = 0;
	for (AActor* Actor : Subsystem->GetAllLevelActors())
	{
		if (!IsValid(Actor) || !Actor->GetActorLabel().Contains(TEXT("E2EDemoCube")))
		{
			continue;
		}
		TestEqual(TEXT("Cube organized into the demo folder"), Actor->GetFolderPath(), FName(Folder));
		TestTrue(TEXT("Cube scale applied"), Actor->GetActorScale3D().Equals(FVector(1.0, 1.0, 2.0)));
		const AStaticMeshActor* MeshActor = Cast<AStaticMeshActor>(Actor);
		if (MeshActor && MeshActor->GetStaticMeshComponent() && MeshActor->GetStaticMeshComponent()->GetStaticMesh())
		{
			++VerifiedCubes;
		}
	}
	TestEqual(TEXT("All four cubes verified with meshes"), VerifiedCubes, 4);

	// --- THE north-star check: the whole externally built scene fully unwinds via
	// standard editor Undo, one step per approved batch (folder, arrange, spawn). ---
	TestTrue(TEXT("Undo step 1 (folder)"), GEditor->UndoTransaction());
	for (AActor* Actor : Subsystem->GetAllLevelActors())
	{
		if (IsValid(Actor) && Actor->GetActorLabel().Contains(TEXT("E2EDemoCube")))
		{
			TestEqual(TEXT("Folder reverted"), Actor->GetFolderPath(), FName(NAME_None));
		}
	}

	TestTrue(TEXT("Undo step 2 (arrange)"), GEditor->UndoTransaction());
	for (AActor* Actor : Subsystem->GetAllLevelActors())
	{
		if (IsValid(Actor) && Actor->GetActorLabel().Contains(TEXT("E2EDemoCube")))
		{
			TestTrue(TEXT("Scale reverted"), Actor->GetActorScale3D().Equals(FVector::OneVector));
		}
	}

	TestTrue(TEXT("Undo step 3 (spawn)"), GEditor->UndoTransaction());
	TestEqual(TEXT("Scene fully restored: no demo cubes remain"),
		UE5MCPTests::CountActorsLabeled(TEXT("E2EDemoCube")), 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
