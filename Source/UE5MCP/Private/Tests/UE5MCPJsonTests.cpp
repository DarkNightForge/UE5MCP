// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UE5MCPJson.h"
#include "UE5MCPTestHelpers.h"
#include "UE5MCPTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPJsonParsePlanTest,
	"UE5MCP.Json.ParsesPlanEnvelope", UE5MCPTests::KernelTestFlags)
bool FUE5MCPJsonParsePlanTest::RunTest(const FString& Parameters)
{
	const FString Body = TEXT(R"json({
		"schema_version": 1,
		"summary": "Organize props",
		"requires_approval": true,
		"requires_second_confirmation": false,
		"ignored_future_field": {"x": 1},
		"context_fingerprint": {
			"scene": "DemoScene",
			"selected_object_paths": ["PersistentLevel.Crate_1", "PersistentLevel.Crate_2"]
		},
		"actions": [
			{
				"id": "a1",
				"tool": "set_actor_folder",
				"risk": "low_risk",
				"targets": ["PersistentLevel.Crate_1", "PersistentLevel.Crate_2"],
				"params": { "folder_path": "Props\\Background" }
			},
			{
				"id": "a2",
				"tool": "find_actors",
				"risk": "read_only",
				"targets": [],
				"params": { "label_contains": "Crate", "selected_only": true, "max_results": 25 }
			}
		]
	})json");

	FUE5MCPPlanRequest Request;
	TArray<FString> Errors;
	TestTrue(TEXT("Envelope parses"), UE5MCPJson::ParsePlanRequest(Body, Request, Errors));
	TestEqual(TEXT("No parse errors"), Errors.Num(), 0);

	TestEqual(TEXT("schema_version"), Request.SchemaVersion, 1);
	TestEqual(TEXT("summary"), Request.Summary, FString(TEXT("Organize props")));
	TestTrue(TEXT("requires_approval"), Request.bRequiresApproval);
	TestTrue(TEXT("fingerprint present"), Request.bHasContextFingerprint);
	TestEqual(TEXT("fingerprint scene"), Request.Fingerprint.SceneName, FString(TEXT("DemoScene")));
	TestEqual(TEXT("fingerprint selection size"), Request.Fingerprint.SelectedActorPaths.Num(), 2);

	TestEqual(TEXT("two actions"), Request.Actions.Num(), 2);
	if (Request.Actions.Num() == 2)
	{
		TestEqual(TEXT("a1 tool"), Request.Actions[0].ToolName, FString(TEXT("set_actor_folder")));
		TestEqual(TEXT("a1 targets"), Request.Actions[0].TargetPaths.Num(), 2);
		TestEqual(TEXT("a1 folder normalized (backslash to slash)"),
			Request.Actions[0].FolderPath, FName(TEXT("Props/Background")));
		TestEqual(TEXT("a1 provided params recorded"), Request.Actions[0].ProvidedParamKeys.Num(), 1);

		TestEqual(TEXT("a2 label filter"), Request.Actions[1].FindQuery.LabelContains, FString(TEXT("Crate")));
		TestTrue(TEXT("a2 selected_only"), Request.Actions[1].FindQuery.bSelectedOnly);
		TestEqual(TEXT("a2 max_results"), Request.Actions[1].FindQuery.MaxResults, 25);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPJsonRejectsMalformedTest,
	"UE5MCP.Json.RejectsMalformedBodies", UE5MCPTests::KernelTestFlags)
bool FUE5MCPJsonRejectsMalformedTest::RunTest(const FString& Parameters)
{
	FUE5MCPPlanRequest Request;
	TArray<FString> Errors;

	TestFalse(TEXT("Garbage rejected"), UE5MCPJson::ParsePlanRequest(TEXT("{not json"), Request, Errors));
	TestTrue(TEXT("Garbage error recorded"), Errors.Num() > 0);

	Errors.Empty();
	TestFalse(TEXT("Missing actions rejected"), UE5MCPJson::ParsePlanRequest(TEXT("{\"schema_version\": 1}"), Request, Errors));
	TestTrue(TEXT("Missing-actions error names the problem"),
		UE5MCPTests::LogLinesContain(Errors, TEXT("actions")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPJsonParseLabelAndTagsTest,
	"UE5MCP.Json.ParsesLabelAndTags", UE5MCPTests::KernelTestFlags)
bool FUE5MCPJsonParseLabelAndTagsTest::RunTest(const FString& Parameters)
{
	const FString Body = TEXT(R"json({
		"schema_version": 1,
		"summary": "Label and tag",
		"requires_approval": true,
		"context_fingerprint": { "scene": "DemoScene", "selected_object_paths": ["PersistentLevel.Crate_1"] },
		"actions": [
			{
				"id": "a1",
				"tool": "set_actor_label",
				"risk": "low_risk",
				"targets": ["PersistentLevel.Crate_1"],
				"params": { "label": "  Hero Spawn  " }
			},
			{
				"id": "a2",
				"tool": "add_actor_tags",
				"risk": "low_risk",
				"targets": ["PersistentLevel.Crate_1"],
				"params": { "tags": ["Rock", " Cleanup ", "Rock"] }
			}
		]
	})json");

	FUE5MCPPlanRequest Request;
	TArray<FString> Errors;
	TestTrue(TEXT("Envelope parses"), UE5MCPJson::ParsePlanRequest(Body, Request, Errors));
	TestEqual(TEXT("No parse errors"), Errors.Num(), 0);
	TestEqual(TEXT("two actions"), Request.Actions.Num(), 2);
	if (Request.Actions.Num() == 2)
	{
		// Label is trimmed.
		TestEqual(TEXT("a1 label trimmed"), Request.Actions[0].NewLabel, FString(TEXT("Hero Spawn")));
		// Tags: trimmed, de-duplicated (AddUnique), empty/whitespace dropped.
		TestEqual(TEXT("a2 two unique tags"), Request.Actions[1].Tags.Num(), 2);
		TestTrue(TEXT("a2 has Rock"), Request.Actions[1].Tags.Contains(FName(TEXT("Rock"))));
		TestTrue(TEXT("a2 has trimmed Cleanup"), Request.Actions[1].Tags.Contains(FName(TEXT("Cleanup"))));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPJsonParseTransformTest,
	"UE5MCP.Json.ParsesTransformParams", UE5MCPTests::KernelTestFlags)
bool FUE5MCPJsonParseTransformTest::RunTest(const FString& Parameters)
{
	const FString Body = TEXT(R"json({
		"schema_version": 1,
		"summary": "Move a prop",
		"requires_approval": true,
		"context_fingerprint": { "scene": "S", "selected_object_paths": ["PersistentLevel.A"] },
		"actions": [
			{
				"id": "t1",
				"tool": "set_actor_transform",
				"risk": "low_risk",
				"targets": ["PersistentLevel.A"],
				"params": { "location": [1, 2, 3], "rotation": [0, 90, 0], "scale": [2, 2, 2] }
			}
		]
	})json");

	FUE5MCPPlanRequest Request;
	TArray<FString> Errors;
	TestTrue(TEXT("Envelope parses"), UE5MCPJson::ParsePlanRequest(Body, Request, Errors));
	TestEqual(TEXT("No parse errors"), Errors.Num(), 0);

	if (TestEqual(TEXT("One action"), Request.Actions.Num(), 1))
	{
		const FUE5MCPTransformDelta& Transform = Request.Actions[0].Transform;
		TestTrue(TEXT("Location parsed"), Transform.bHasLocation && Transform.Location.Equals(FVector(1.0, 2.0, 3.0)));
		// Euler [roll, pitch, yaw] = [0, 90, 0] => FRotator(Pitch=90, Yaw=0, Roll=0).
		TestTrue(TEXT("Rotation parsed from euler"), Transform.bHasRotation && Transform.Rotation.Equals(FRotator(90.0, 0.0, 0.0)));
		TestTrue(TEXT("Scale parsed"), Transform.bHasScale && Transform.Scale.Equals(FVector(2.0, 2.0, 2.0)));
		TestEqual(TEXT("All three params recorded for the validator"), Request.Actions[0].ProvidedParamKeys.Num(), 3);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPJsonRejectsBadTransformTest,
	"UE5MCP.Json.RejectsMalformedTransformVectors", UE5MCPTests::KernelTestFlags)
bool FUE5MCPJsonRejectsBadTransformTest::RunTest(const FString& Parameters)
{
	FUE5MCPPlanRequest Request;
	TArray<FString> Errors;

	// Wrong length.
	TestFalse(TEXT("Two-element location rejected"), UE5MCPJson::ParsePlanRequest(TEXT(
		"{\"schema_version\":1,\"actions\":[{\"id\":\"t\",\"tool\":\"set_actor_transform\","
		"\"risk\":\"low_risk\",\"targets\":[\"PersistentLevel.A\"],\"params\":{\"location\":[1,2]}}]}"),
		Request, Errors));
	TestTrue(TEXT("Error names location"), UE5MCPTests::LogLinesContain(Errors, TEXT("location")));

	// Non-number element.
	Errors.Empty();
	TestFalse(TEXT("String element in scale rejected"), UE5MCPJson::ParsePlanRequest(TEXT(
		"{\"schema_version\":1,\"actions\":[{\"id\":\"t\",\"tool\":\"set_actor_transform\","
		"\"risk\":\"low_risk\",\"targets\":[\"PersistentLevel.A\"],\"params\":{\"scale\":[1,\"x\",3]}}]}"),
		Request, Errors));
	TestTrue(TEXT("Error names scale"), UE5MCPTests::LogLinesContain(Errors, TEXT("scale")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPJsonSerializeTest,
	"UE5MCP.Json.SerializesContextAndResults", UE5MCPTests::KernelTestFlags)
bool FUE5MCPJsonSerializeTest::RunTest(const FString& Parameters)
{
	FUE5MCPContextPack Context;
	Context.WorldName = TEXT("DemoScene");
	Context.Warnings.Add(TEXT("a warning"));
	FUE5MCPActorSummary Summary;
	Summary.ActorPath = TEXT("PersistentLevel.Crate_1");
	Summary.Label = TEXT("Crate_1");
	Summary.bSelected = true;
	Context.SelectedActors.Add(Summary);
	Context.LoadedActorsCapped.Add(Summary);

	const FString ContextJson = UE5MCPJson::SerializeContextPack(Context);
	TestTrue(TEXT("Context JSON carries world"), ContextJson.Contains(TEXT("\"world\":\"DemoScene\"")));
	TestTrue(TEXT("Context JSON carries warning"), ContextJson.Contains(TEXT("a warning")));
	TestTrue(TEXT("Context JSON carries actor path"), ContextJson.Contains(TEXT("PersistentLevel.Crate_1")));

	FUE5MCPExecutionResult Execution;
	Execution.bSuccess = true;
	FUE5MCPActionResult ActionResult;
	ActionResult.ActionId = TEXT("a1");
	ActionResult.bSuccess = true;
	ActionResult.Message = TEXT("done");
	ActionResult.ChangedCount = 3;
	Execution.ActionResults.Add(ActionResult);
	Execution.UserVisibleLogLines.Add(TEXT("a log line"));

	const FString ResultJson = UE5MCPJson::SerializeExecutionResult(Execution);
	TestTrue(TEXT("Result JSON carries status"), ResultJson.Contains(TEXT("\"status\":\"succeeded\"")));
	TestTrue(TEXT("Result JSON carries changed_count"), ResultJson.Contains(TEXT("\"changed_count\":3")));
	TestTrue(TEXT("Result JSON carries log"), ResultJson.Contains(TEXT("a log line")));

	const FString ErrorJson = UE5MCPJson::SerializeError(TEXT("invalid_plan"), TEXT("nope"));
	TestTrue(TEXT("Error JSON carries machine code"), ErrorJson.Contains(TEXT("\"error\":\"invalid_plan\"")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
