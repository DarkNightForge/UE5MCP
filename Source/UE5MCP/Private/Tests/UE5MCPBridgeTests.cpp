// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
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

// Bridge tests drive the plain request handlers directly (no sockets): the HTTP
// layer is a thin shim over these, and listener/socket plumbing is engine-owned
// and engine-tested. One manual curl pass covers the real socket.

namespace UE5MCPBridgeTestUtils
{
	TSharedPtr<FJsonObject> ParseBody(const FUE5MCPBridgeResponse& Response)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Response.Body);
		FJsonSerializer::Deserialize(Reader, Root);
		return Root;
	}

	FString MakeFolderPlanBody(const TArray<AActor*>& Actors, UWorld* World, const TCHAR* Folder)
	{
		// Build the JSON the way an agent client would, from observed context.
		FString Targets;
		FString SelectedPaths;
		for (int32 Index = 0; Index < Actors.Num(); ++Index)
		{
			const FString Quoted = FString::Printf(TEXT("\"%s\""), *Actors[Index]->GetPathName());
			Targets += (Index ? TEXT(",") : TEXT("")) + Quoted;
		}
		const TArray<AActor*> Selected = GEditor->GetEditorSubsystem<UEditorActorSubsystem>()->GetSelectedLevelActors();
		for (int32 Index = 0; Index < Selected.Num(); ++Index)
		{
			const FString Quoted = FString::Printf(TEXT("\"%s\""), *Selected[Index]->GetPathName());
			SelectedPaths += (Index ? TEXT(",") : TEXT("")) + Quoted;
		}

		return FString::Printf(TEXT(
			"{\"schema_version\":1,\"summary\":\"Bridge test\",\"requires_approval\":true,"
			"\"context_fingerprint\":{\"scene\":\"%s\",\"selected_object_paths\":[%s]},"
			"\"actions\":[{\"id\":\"b1\",\"tool\":\"set_actor_folder\",\"risk\":\"low_risk\","
			"\"targets\":[%s],\"params\":{\"folder_path\":\"%s\"}}]}"),
			*World->GetName(), *SelectedPaths, *Targets, Folder);
	}

	FString MakeTransformPlanBody(const TArray<AActor*>& Actors, UWorld* World, const FVector& Location)
	{
		FString Targets;
		FString SelectedPaths;
		for (int32 Index = 0; Index < Actors.Num(); ++Index)
		{
			const FString Quoted = FString::Printf(TEXT("\"%s\""), *Actors[Index]->GetPathName());
			Targets += (Index ? TEXT(",") : TEXT("")) + Quoted;
		}
		const TArray<AActor*> Selected = GEditor->GetEditorSubsystem<UEditorActorSubsystem>()->GetSelectedLevelActors();
		for (int32 Index = 0; Index < Selected.Num(); ++Index)
		{
			const FString Quoted = FString::Printf(TEXT("\"%s\""), *Selected[Index]->GetPathName());
			SelectedPaths += (Index ? TEXT(",") : TEXT("")) + Quoted;
		}

		// location arrives as a 3-number JSON array, exactly as an agent would emit it.
		return FString::Printf(TEXT(
			"{\"schema_version\":1,\"summary\":\"Bridge transform test\",\"requires_approval\":true,"
			"\"context_fingerprint\":{\"scene\":\"%s\",\"selected_object_paths\":[%s]},"
			"\"actions\":[{\"id\":\"bt1\",\"tool\":\"set_actor_transform\",\"risk\":\"low_risk\","
			"\"targets\":[%s],\"params\":{\"location\":[%f,%f,%f]}}]}"),
			*World->GetName(), *SelectedPaths, *Targets, Location.X, Location.Y, Location.Z);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPBridgeContextTest,
	"UE5MCP.Bridge.ContextEndpointReturnsBoundedJson", UE5MCPTests::KernelTestFlags)
bool FUE5MCPBridgeContextTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	FUE5MCPEditorService::Get().ResetForTests();
	UE5MCPTests::SpawnTestActors(World, 2);

	FUE5MCPBridgeServer Bridge;
	const FUE5MCPBridgeResponse Response = Bridge.HandleGetContext();
	TestEqual(TEXT("HTTP 200"), Response.Code, 200);

	const TSharedPtr<FJsonObject> Body = UE5MCPBridgeTestUtils::ParseBody(Response);
	if (!TestTrue(TEXT("Body is JSON"), Body.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("World reported"), Body->GetStringField(TEXT("world")), World->GetName());
	TestTrue(TEXT("Loaded actors listed"), Body->GetArrayField(TEXT("loaded_actors")).Num() >= 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPBridgeReadOnlyTest,
	"UE5MCP.Bridge.ReadOnlyPlanExecutesImmediately", UE5MCPTests::KernelTestFlags)
bool FUE5MCPBridgeReadOnlyTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	FUE5MCPEditorService::Get().ResetForTests();
	UE5MCPTests::SpawnTestActors(World, 3);

	FUE5MCPBridgeServer Bridge;
	const FUE5MCPBridgeResponse Response = Bridge.HandlePostPlan(TEXT(
		"{\"schema_version\":1,\"summary\":\"find\",\"actions\":[{\"id\":\"f1\",\"tool\":\"find_actors\","
		"\"risk\":\"read_only\",\"targets\":[],\"params\":{\"label_contains\":\"UE5MCPTestActor\"}}]}"));

	TestEqual(TEXT("HTTP 200"), Response.Code, 200);
	const TSharedPtr<FJsonObject> Body = UE5MCPBridgeTestUtils::ParseBody(Response);
	if (!TestTrue(TEXT("Body is JSON"), Body.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("Read-only plan executed immediately"),
		Body->GetStringField(TEXT("status")), FString(TEXT("executed")));
	TestTrue(TEXT("Found actors returned"), Response.Body.Contains(TEXT("UE5MCPTestActor")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPBridgeMutationPendsTest,
	"UE5MCP.Bridge.MutationPlanPendsAndPanelApprovalCompletesIt", UE5MCPTests::KernelTestFlags)
bool FUE5MCPBridgeMutationPendsTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	FUE5MCPEditorService& Service = FUE5MCPEditorService::Get();
	Service.ResetForTests();
	const TArray<AActor*> Actors = UE5MCPTests::SpawnTestActors(World, 2);
	GEditor->GetEditorSubsystem<UEditorActorSubsystem>()->SetSelectedLevelActors(Actors);

	FUE5MCPBridgeServer Bridge;
	const FUE5MCPBridgeResponse PostResponse = Bridge.HandlePostPlan(
		UE5MCPBridgeTestUtils::MakeFolderPlanBody(Actors, World, TEXT("UE5MCPTests/ViaBridge")));

	TestEqual(TEXT("HTTP 200"), PostResponse.Code, 200);
	const TSharedPtr<FJsonObject> PostBody = UE5MCPBridgeTestUtils::ParseBody(PostResponse);
	if (!TestTrue(TEXT("Body is JSON"), PostBody.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("Mutation plan pends"),
		PostBody->GetStringField(TEXT("status")), FString(TEXT("pending_approval")));
	TestEqual(TEXT("Bridge did not mutate"), Actors[0]->GetFolderPath(), FName(NAME_None));

	// A second plan while one pends is refused with 409.
	const FUE5MCPBridgeResponse Busy = Bridge.HandlePostPlan(
		UE5MCPBridgeTestUtils::MakeFolderPlanBody(Actors, World, TEXT("UE5MCPTests/Second")));
	TestEqual(TEXT("HTTP 409 while pending"), Busy.Code, 409);
	TestTrue(TEXT("Busy code is plan_pending"), Busy.Body.Contains(TEXT("plan_pending")));

	// The human approves in the panel (service call stands in for the click).
	const FUE5MCPApprovalOutcome Outcome = Service.ApproveCurrentPlan();
	TestTrue(TEXT("Human approval executed the plan"), Outcome.bExecuted);
	TestEqual(TEXT("Mutation applied after approval"),
		Actors[0]->GetFolderPath(), FName(TEXT("UE5MCPTests/ViaBridge")));

	// The bridge poller sees the terminal state.
	const FString PlanId = PostBody->GetStringField(TEXT("plan_id"));
	const FUE5MCPBridgeResponse Polled = Bridge.HandleGetPlan(PlanId);
	TestEqual(TEXT("HTTP 200 on poll"), Polled.Code, 200);
	TestTrue(TEXT("Poll reports executed"), Polled.Body.Contains(TEXT("\"status\":\"executed\"")));
	TestTrue(TEXT("Poll carries the result"), Polled.Body.Contains(TEXT("set_actor_folder succeeded")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPBridgeTransformPendsTest,
	"UE5MCP.Bridge.TransformPlanPendsAndApprovalApplies", UE5MCPTests::KernelTestFlags)
bool FUE5MCPBridgeTransformPendsTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	FUE5MCPEditorService& Service = FUE5MCPEditorService::Get();
	Service.ResetForTests();
	const TArray<AActor*> Actors = UE5MCPTests::SpawnTransformableTestActors(World, 2);
	GEditor->GetEditorSubsystem<UEditorActorSubsystem>()->SetSelectedLevelActors(Actors);

	const FVector Target(64.0, 128.0, 256.0);
	FUE5MCPBridgeServer Bridge;
	const FUE5MCPBridgeResponse PostResponse = Bridge.HandlePostPlan(
		UE5MCPBridgeTestUtils::MakeTransformPlanBody(Actors, World, Target));

	TestEqual(TEXT("HTTP 200"), PostResponse.Code, 200);
	const TSharedPtr<FJsonObject> PostBody = UE5MCPBridgeTestUtils::ParseBody(PostResponse);
	if (!TestTrue(TEXT("Body is JSON"), PostBody.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("Transform plan pends"),
		PostBody->GetStringField(TEXT("status")), FString(TEXT("pending_approval")));
	TestFalse(TEXT("Bridge did not move the actor"), Actors[0]->GetActorLocation().Equals(Target));
	TestTrue(TEXT("Preview names the transform tool"), PostResponse.Body.Contains(TEXT("set_actor_transform")));

	// A second plan while one pends is refused with 409.
	const FUE5MCPBridgeResponse Busy = Bridge.HandlePostPlan(
		UE5MCPBridgeTestUtils::MakeTransformPlanBody(Actors, World, FVector(1.0, 2.0, 3.0)));
	TestEqual(TEXT("HTTP 409 while pending"), Busy.Code, 409);

	// The human approves in the panel (service call stands in for the click).
	const FUE5MCPApprovalOutcome Outcome = Service.ApproveCurrentPlan();
	TestTrue(TEXT("Human approval executed the transform plan"), Outcome.bExecuted);
	TestTrue(TEXT("Transform applied after approval"), Actors[0]->GetActorLocation().Equals(Target));

	// The bridge poller sees the terminal state and the per-action message.
	const FString PlanId = PostBody->GetStringField(TEXT("plan_id"));
	const FUE5MCPBridgeResponse Polled = Bridge.HandleGetPlan(PlanId);
	TestEqual(TEXT("HTTP 200 on poll"), Polled.Code, 200);
	TestTrue(TEXT("Poll reports executed"), Polled.Body.Contains(TEXT("\"status\":\"executed\"")));
	TestTrue(TEXT("Poll carries the result"), Polled.Body.Contains(TEXT("set_actor_transform succeeded")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPBridgeLoopbackParserTest,
	"UE5MCP.Bridge.LoopbackHostStringClassifier", UE5MCPTests::KernelTestFlags)
bool FUE5MCPBridgeLoopbackParserTest::RunTest(const FString& Parameters)
{
	// The shared classifier behind the bind-address guard and the Origin/Host guard.
	const TCHAR* Loopback[] = {
		TEXT("localhost"), TEXT("LOCALHOST"), TEXT("127.0.0.1"), TEXT("127.0.0.53"),
		TEXT("::1"), TEXT("http://localhost:30110"), TEXT("http://127.0.0.1:30110/plan"),
		TEXT("localhost:30110"), TEXT("[::1]:30110"), TEXT("https://localhost"),
	};
	for (const TCHAR* Value : Loopback)
	{
		TestTrue(FString::Printf(TEXT("loopback: %s"), Value), FUE5MCPBridgeServer::IsLoopbackHostString(Value));
	}

	const TCHAR* NonLoopback[] = {
		TEXT("any"), TEXT("0.0.0.0"), TEXT("::"), TEXT("10.0.0.5"), TEXT("192.168.1.10"),
		TEXT("evil.example.com"), TEXT("https://evil.example.com"), TEXT("http://attacker.test:30110"),
		TEXT("0.0.0.0:30110"), TEXT("128.0.0.1"),
	};
	for (const TCHAR* Value : NonLoopback)
	{
		TestFalse(FString::Printf(TEXT("non-loopback: %s"), Value), FUE5MCPBridgeServer::IsLoopbackHostString(Value));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPBridgePreviewModeTest,
	"UE5MCP.Bridge.PreviewModeReturnsTypedPreviewWithoutExecuting", UE5MCPTests::KernelTestFlags)
bool FUE5MCPBridgePreviewModeTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	FUE5MCPEditorService::Get().ResetForTests();
	const TArray<AActor*> Actors = UE5MCPTests::SpawnTestActors(World, 2);
	GEditor->GetEditorSubsystem<UEditorActorSubsystem>()->SetSelectedLevelActors(Actors);

	FUE5MCPBridgeServer Bridge;
	// Same body as a normal submission, plus the transport-level preview mode.
	FString Body = UE5MCPBridgeTestUtils::MakeFolderPlanBody(Actors, World, TEXT("UE5MCPTests/PreviewMode"));
	Body.InsertAt(1, TEXT("\"mode\":\"preview\","));

	const FUE5MCPBridgeResponse Response = Bridge.HandlePostPlan(Body);
	TestEqual(TEXT("HTTP 200"), Response.Code, 200);
	const TSharedPtr<FJsonObject> ResponseBody = UE5MCPBridgeTestUtils::ParseBody(Response);
	if (!TestTrue(TEXT("Body is JSON"), ResponseBody.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("Status is previewed"), ResponseBody->GetStringField(TEXT("status")), FString(TEXT("previewed")));
	TestEqual(TEXT("Approval mode is preview"), ResponseBody->GetStringField(TEXT("approval_mode")), FString(TEXT("preview")));
	TestTrue(TEXT("Preview text carried back"), Response.Body.Contains(TEXT("set_actor_folder")));
	TestEqual(TEXT("Nothing executed"), Actors[0]->GetFolderPath(), FName(NAME_None));
	TestFalse(TEXT("No pending slot occupied"), FUE5MCPEditorService::Get().IsApprovalAvailable());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPBridgeExternalModeTest,
	"UE5MCP.Bridge.ExternalModeGatedBySettingThenExecutes", UE5MCPTests::KernelTestFlags)
bool FUE5MCPBridgeExternalModeTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	FUE5MCPEditorService::Get().ResetForTests();
	const TArray<AActor*> Actors = UE5MCPTests::SpawnTestActors(World, 2);
	GEditor->GetEditorSubsystem<UEditorActorSubsystem>()->SetSelectedLevelActors(Actors);

	FUE5MCPBridgeServer Bridge;
	FString Body = UE5MCPBridgeTestUtils::MakeFolderPlanBody(Actors, World, TEXT("UE5MCPTests/ExternalMode"));
	Body.InsertAt(1, TEXT("\"mode\":\"execute_external\","));

	// Setting off (the default): the bridge refuses with 403 and mutates nothing.
	{
		UE5MCPTests::FScopedExternalApprovalSettings Scoped(/*bAllowExternal=*/false);
		const FUE5MCPBridgeResponse Response = Bridge.HandlePostPlan(Body);
		TestEqual(TEXT("HTTP 403 while disabled"), Response.Code, 403);
		TestTrue(TEXT("Refusal names the gate"), Response.Body.Contains(TEXT("external_approval_disabled")));
		TestEqual(TEXT("Nothing executed while disabled"), Actors[0]->GetFolderPath(), FName(NAME_None));
	}

	// Setting on: the in-session-approved plan executes immediately, no panel click.
	{
		UE5MCPTests::FScopedExternalApprovalSettings Scoped(/*bAllowExternal=*/true);
		const FUE5MCPBridgeResponse Response = Bridge.HandlePostPlan(Body);
		TestEqual(TEXT("HTTP 200 when enabled"), Response.Code, 200);
		const TSharedPtr<FJsonObject> ResponseBody = UE5MCPBridgeTestUtils::ParseBody(Response);
		if (!TestTrue(TEXT("Body is JSON"), ResponseBody.IsValid()))
		{
			return false;
		}
		TestEqual(TEXT("Status executed"), ResponseBody->GetStringField(TEXT("status")), FString(TEXT("executed")));
		TestEqual(TEXT("Approval mode is external_session"),
			ResponseBody->GetStringField(TEXT("approval_mode")), FString(TEXT("external_session")));
		TestEqual(TEXT("Mutation applied"), Actors[0]->GetFolderPath(), FName(TEXT("UE5MCPTests/ExternalMode")));
	}

	// Unknown mode is a schema-level refusal.
	{
		FString BadBody = UE5MCPBridgeTestUtils::MakeFolderPlanBody(Actors, World, TEXT("UE5MCPTests/BadMode"));
		BadBody.InsertAt(1, TEXT("\"mode\":\"yolo\","));
		const FUE5MCPBridgeResponse Response = Bridge.HandlePostPlan(BadBody);
		TestEqual(TEXT("HTTP 400 for unknown mode"), Response.Code, 400);
		TestTrue(TEXT("Names unknown_mode"), Response.Body.Contains(TEXT("unknown_mode")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPBridgeErrorsTest,
	"UE5MCP.Bridge.RejectsMalformedAndUnknown", UE5MCPTests::KernelTestFlags)
bool FUE5MCPBridgeErrorsTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("CreateNewMap returned a world"), World))
	{
		return false;
	}
	FUE5MCPEditorService::Get().ResetForTests();

	FUE5MCPBridgeServer Bridge;

	const FUE5MCPBridgeResponse Malformed = Bridge.HandlePostPlan(TEXT("{not json"));
	TestEqual(TEXT("Malformed body is HTTP 400"), Malformed.Code, 400);
	TestTrue(TEXT("Malformed body names invalid_plan"), Malformed.Body.Contains(TEXT("invalid_plan")));

	const FUE5MCPBridgeResponse BadPlan = Bridge.HandlePostPlan(TEXT(
		"{\"schema_version\":1,\"actions\":[{\"id\":\"x\",\"tool\":\"run_script\",\"risk\":\"read_only\",\"targets\":[]}]}"));
	TestEqual(TEXT("Unknown tool is HTTP 400"), BadPlan.Code, 400);
	TestTrue(TEXT("Problems carried back"), BadPlan.Body.Contains(TEXT("R3")));

	const FUE5MCPBridgeResponse Missing = Bridge.HandleGetPlan(TEXT("does-not-exist"));
	TestEqual(TEXT("Unknown plan id is HTTP 404"), Missing.Code, 404);
	TestTrue(TEXT("404 names plan_not_found"), Missing.Body.Contains(TEXT("plan_not_found")));

	const FUE5MCPBridgeResponse Status = Bridge.HandleGetStatus();
	TestEqual(TEXT("Status is HTTP 200"), Status.Code, 200);
	TestTrue(TEXT("Status names the plugin"), Status.Body.Contains(TEXT("UE5MCP")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
