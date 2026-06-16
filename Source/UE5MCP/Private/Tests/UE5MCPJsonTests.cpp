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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPJsonParsePackageStatusTest,
	"UE5MCP.Json.ParsesPackageStatus", UE5MCPTests::KernelTestFlags)
bool FUE5MCPJsonParsePackageStatusTest::RunTest(const FString& Parameters)
{
	const FString Body = TEXT(R"json({
		"schema_version": 1,
		"summary": "Package status",
		"requires_approval": false,
		"actions": [
			{
				"id": "a1",
				"tool": "get_package_status",
				"risk": "read_only",
				"targets": [],
				"params": { "max_packages": 250, "dirty_only": false }
			}
		]
	})json");

	FUE5MCPPlanRequest Request;
	TArray<FString> Errors;
	TestTrue(TEXT("Envelope parses"), UE5MCPJson::ParsePlanRequest(Body, Request, Errors));
	TestEqual(TEXT("No parse errors"), Errors.Num(), 0);
	TestEqual(TEXT("one action"), Request.Actions.Num(), 1);
	if (Request.Actions.Num() == 1)
	{
		TestEqual(TEXT("max_packages parsed"), Request.Actions[0].PackageQuery.MaxPackages, 250);
		TestFalse(TEXT("dirty_only parsed as false"), Request.Actions[0].PackageQuery.bDirtyOnly);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPJsonParseSetPropertyTest,
	"UE5MCP.Json.ParsesSetPropertyValueKinds", UE5MCPTests::KernelTestFlags)
bool FUE5MCPJsonParseSetPropertyTest::RunTest(const FString& Parameters)
{
	const FString Body = TEXT(R"json({
		"schema_version": 1,
		"summary": "Property kinds",
		"requires_approval": true,
		"context_fingerprint": { "scene": "DemoScene", "selected_object_paths": ["PersistentLevel.Light_1"] },
		"actions": [
			{ "id": "n", "tool": "set_actor_property", "risk": "low_risk", "targets": ["PersistentLevel.Light_1"],
			  "params": { "component": "/Script/Engine.PointLightComponent", "property": "Intensity", "value": 5000.0 } },
			{ "id": "c", "tool": "set_actor_property", "risk": "low_risk", "targets": ["PersistentLevel.Light_1"],
			  "params": { "property": "LightColor", "value": [1, 0.5, 0.25, 1] } },
			{ "id": "b", "tool": "set_actor_property", "risk": "low_risk", "targets": ["PersistentLevel.Light_1"],
			  "params": { "property": "bAffectsWorld", "value": true } },
			{ "id": "s", "tool": "set_actor_property", "risk": "low_risk", "targets": ["PersistentLevel.Light_1"],
			  "params": { "property": "Tag", "value": "hero" } }
		]
	})json");

	FUE5MCPPlanRequest Request;
	TArray<FString> Errors;
	TestTrue(TEXT("Envelope parses"), UE5MCPJson::ParsePlanRequest(Body, Request, Errors));
	TestEqual(TEXT("No parse errors"), Errors.Num(), 0);
	TestEqual(TEXT("four actions"), Request.Actions.Num(), 4);
	if (Request.Actions.Num() == 4)
	{
		using EKind = FUE5MCPPropertyValue::EKind;
		TestEqual(TEXT("a0 component"), Request.Actions[0].PropertyComponentClass, FString(TEXT("/Script/Engine.PointLightComponent")));
		TestTrue(TEXT("a0 number kind"), Request.Actions[0].PropertyValue.Kind == EKind::Number);
		TestTrue(TEXT("a0 number value"), FMath::IsNearlyEqual(Request.Actions[0].PropertyValue.Number, 5000.0, 0.001));
		TestTrue(TEXT("a1 color kind"), Request.Actions[1].PropertyValue.Kind == EKind::Color);
		TestTrue(TEXT("a1 color rgba"), Request.Actions[1].PropertyValue.Color.Equals(FLinearColor(1.0f, 0.5f, 0.25f, 1.0f), 0.001f));
		TestTrue(TEXT("a2 bool kind"), Request.Actions[2].PropertyValue.Kind == EKind::Bool);
		TestTrue(TEXT("a2 bool value"), Request.Actions[2].PropertyValue.Bool);
		TestTrue(TEXT("a3 name kind"), Request.Actions[3].PropertyValue.Kind == EKind::Name);
		TestEqual(TEXT("a3 name value"), Request.Actions[3].PropertyValue.Name, FString(TEXT("hero")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPJsonParseGetPropertiesTest,
	"UE5MCP.Json.ParsesGetActorPropertiesQuery", UE5MCPTests::KernelTestFlags)
bool FUE5MCPJsonParseGetPropertiesTest::RunTest(const FString& Parameters)
{
	const FString Body = TEXT(R"json({
		"schema_version": 1,
		"summary": "Inspect properties",
		"requires_approval": false,
		"actions": [
			{
				"id": "g1",
				"tool": "get_actor_properties",
				"risk": "read_only",
				"targets": ["PersistentLevel.Light_1"],
				"params": { "component": "/Script/Engine.PointLightComponent", "editable_only": false, "allowlisted_only": false, "max_properties": 25 }
			}
		]
	})json");

	FUE5MCPPlanRequest Request;
	TArray<FString> Errors;
	TestTrue(TEXT("Envelope parses"), UE5MCPJson::ParsePlanRequest(Body, Request, Errors));
	TestEqual(TEXT("No parse errors"), Errors.Num(), 0);
	TestEqual(TEXT("one action"), Request.Actions.Num(), 1);
	if (Request.Actions.Num() == 1)
	{
		const FUE5MCPActionRequest& Action = Request.Actions[0];
		TestEqual(TEXT("component parsed"), Action.PropertyComponentClass, FString(TEXT("/Script/Engine.PointLightComponent")));
		TestFalse(TEXT("editable_only parsed as false"), Action.GetPropertiesQuery.bEditableOnly);
		TestFalse(TEXT("allowlisted_only parsed as false"), Action.GetPropertiesQuery.bAllowlistedOnly);
		TestEqual(TEXT("max_properties parsed"), Action.GetPropertiesQuery.MaxProperties, 25);
		TestEqual(TEXT("one target path carried"), Action.TargetPaths.Num(), 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPJsonParseComponentNameTest,
	"UE5MCP.Json.ParsesComponentName", UE5MCPTests::KernelTestFlags)
bool FUE5MCPJsonParseComponentNameTest::RunTest(const FString& Parameters)
{
	const FString Body = TEXT(R"json({
		"schema_version": 1,
		"summary": "Component-by-name",
		"requires_approval": true,
		"context_fingerprint": { "scene": "DemoScene", "selected_object_paths": ["PersistentLevel.Rig_1"] },
		"actions": [
			{ "id": "w", "tool": "set_actor_property", "risk": "low_risk", "targets": ["PersistentLevel.Rig_1"],
			  "params": { "component": "/Script/Engine.PointLightComponent", "component_name": "LightB", "property": "Intensity", "value": 5000.0 } },
			{ "id": "r", "tool": "get_actor_properties", "risk": "read_only", "targets": ["PersistentLevel.Rig_1"],
			  "params": { "component_name": "LightB" } }
		]
	})json");

	FUE5MCPPlanRequest Request;
	TArray<FString> Errors;
	TestTrue(TEXT("Envelope parses"), UE5MCPJson::ParsePlanRequest(Body, Request, Errors));
	TestEqual(TEXT("No parse errors"), Errors.Num(), 0);
	TestEqual(TEXT("two actions"), Request.Actions.Num(), 2);
	if (Request.Actions.Num() == 2)
	{
		TestEqual(TEXT("set component_name parsed"), Request.Actions[0].PropertyComponentName, FString(TEXT("LightB")));
		TestEqual(TEXT("get component_name parsed"), Request.Actions[1].PropertyComponentName, FString(TEXT("LightB")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPJsonParseGetComponentsTest,
	"UE5MCP.Json.ParsesGetActorComponentsQuery", UE5MCPTests::KernelTestFlags)
bool FUE5MCPJsonParseGetComponentsTest::RunTest(const FString& Parameters)
{
	const FString Body = TEXT(R"json({
		"schema_version": 1,
		"summary": "Inspect components",
		"requires_approval": false,
		"actions": [
			{
				"id": "c1",
				"tool": "get_actor_components",
				"risk": "read_only",
				"targets": ["PersistentLevel.Light_1"],
				"params": { "max_components": 12 }
			}
		]
	})json");

	FUE5MCPPlanRequest Request;
	TArray<FString> Errors;
	TestTrue(TEXT("Envelope parses"), UE5MCPJson::ParsePlanRequest(Body, Request, Errors));
	TestEqual(TEXT("No parse errors"), Errors.Num(), 0);
	TestEqual(TEXT("one action"), Request.Actions.Num(), 1);
	if (Request.Actions.Num() == 1)
	{
		TestEqual(TEXT("max_components parsed"), Request.Actions[0].GetComponentsQuery.MaxComponents, 12);
		TestEqual(TEXT("one target path carried"), Request.Actions[0].TargetPaths.Num(), 1);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE5MCPJsonSerializeCapabilitiesTest,
	"UE5MCP.Json.SerializesCapabilities", UE5MCPTests::KernelTestFlags)
bool FUE5MCPJsonSerializeCapabilitiesTest::RunTest(const FString& Parameters)
{
	FUE5MCPExecutionResult Execution;
	Execution.bSuccess = true;
	FUE5MCPActionResult ActionResult;
	ActionResult.ActionId = TEXT("caps");
	ActionResult.bSuccess = true;
	ActionResult.Message = TEXT("caps");
	ActionResult.bHasCapabilities = true;

	FUE5MCPCapabilities& Caps = ActionResult.Capabilities;
	Caps.PlanSchemaVersion = 1;
	FUE5MCPToolCapability Tool;
	Tool.Name = TEXT("set_actor_property");
	Tool.Risk = TEXT("low_risk");
	Tool.Params = { TEXT("property"), TEXT("value"), TEXT("component"), TEXT("component_name") };
	Tool.bRequiresTargets = true;
	Tool.bAcceptsTargets = true;
	Caps.Tools.Add(Tool);
	Caps.SpawnClassAllowlist.Add(TEXT("/Script/Engine.StaticMeshActor"));
	FUE5MCPPropertyPolicySummary Prop;
	Prop.ClassPath = TEXT("/Script/Engine.PointLightComponent");
	Prop.PropertyName = TEXT("Intensity");
	Prop.Type = TEXT("float");
	Prop.bHasRange = true;
	Prop.Min = 0.0;
	Prop.Max = 1000000.0;
	Caps.PropertyAllowlist.Add(Prop);
	Caps.bBlockMutationsToUnwritablePackages = true;
	ActionResult.Capabilities = Caps;
	Execution.ActionResults.Add(ActionResult);

	const FString Json = UE5MCPJson::SerializeExecutionResult(Execution);
	TestTrue(TEXT("carries capabilities object"), Json.Contains(TEXT("\"capabilities\"")));
	TestTrue(TEXT("carries plan_schema_version"), Json.Contains(TEXT("\"plan_schema_version\"")));
	TestTrue(TEXT("carries tools with the tool name"), Json.Contains(TEXT("\"tools\"")) && Json.Contains(TEXT("set_actor_property")));
	TestTrue(TEXT("carries the component_name param"), Json.Contains(TEXT("component_name")));
	TestTrue(TEXT("carries spawn_class_allowlist"), Json.Contains(TEXT("spawn_class_allowlist")));
	TestTrue(TEXT("carries property_allowlist"), Json.Contains(TEXT("property_allowlist")));
	TestTrue(TEXT("carries the policy block"), Json.Contains(TEXT("block_mutations_to_unwritable_packages")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
