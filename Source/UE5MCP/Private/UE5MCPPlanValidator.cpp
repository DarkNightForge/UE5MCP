// Copyright Epic Games, Inc. All Rights Reserved.

#include "UE5MCPPlanValidator.h"

#include "GameFramework/Actor.h"
#include "UE5MCPPreviewModel.h"
#include "UE5MCPSettings.h"
#include "UE5MCPTargetResolver.h"
#include "UE5MCPToolRegistry.h"

FUE5MCPPlanValidationResult FUE5MCPPlanValidator::ValidateAndResolve(const FUE5MCPPlanRequest& Request)
{
	FUE5MCPPlanValidationResult Result;
	TArray<FString>& Problems = Result.Problems;

	if (Request.SchemaVersion != SchemaVersion)
	{
		Problems.Add(FString::Printf(TEXT("R1: schema_version must be %d, got %d"), SchemaVersion, Request.SchemaVersion));
	}

	if (Request.Actions.IsEmpty())
	{
		Problems.Add(TEXT("R2: plan must contain a non-empty actions list"));
	}

	bool bHasMutation = false;
	bool bHasDestructive = false;
	TSet<FString> SeenIds;

	for (int32 Index = 0; Index < Request.Actions.Num(); ++Index)
	{
		const FUE5MCPActionRequest& ActionRequest = Request.Actions[Index];
		const FString Where = FString::Printf(TEXT("actions[%d]"), Index);

		if (ActionRequest.Id.IsEmpty())
		{
			Problems.Add(FString::Printf(TEXT("R2: %s needs a non-empty id"), *Where));
		}
		else if (SeenIds.Contains(ActionRequest.Id))
		{
			Problems.Add(FString::Printf(TEXT("R2: duplicate action id '%s'"), *ActionRequest.Id));
		}
		else
		{
			SeenIds.Add(ActionRequest.Id);
		}

		const FUE5MCPToolDescriptor* Tool = FUE5MCPToolRegistry::FindByName(ActionRequest.ToolName);
		if (!Tool)
		{
			Problems.Add(FString::Printf(TEXT("R3: %s uses unknown tool '%s'"), *Where, *ActionRequest.ToolName));
			continue;
		}

		EUE5MCPRiskLevel DeclaredRisk = EUE5MCPRiskLevel::ReadOnly;
		if (!FUE5MCPToolRegistry::ParseRisk(ActionRequest.RiskString, DeclaredRisk) || DeclaredRisk != Tool->Risk)
		{
			Problems.Add(FString::Printf(TEXT("R4: %s declares risk '%s' but %s is '%s'"),
				*Where, *ActionRequest.RiskString, *Tool->ToolName, *FUE5MCPToolRegistry::RiskToString(Tool->Risk)));
		}

		if (Tool->Risk != EUE5MCPRiskLevel::ReadOnly)
		{
			bHasMutation = true;
		}
		if (Tool->Risk == EUE5MCPRiskLevel::Destructive)
		{
			bHasDestructive = true;
		}

		for (const FString& ParamKey : ActionRequest.ProvidedParamKeys)
		{
			if (!Tool->AllowedParams.Contains(ParamKey))
			{
				Problems.Add(FString::Printf(TEXT("R9: %s has unknown param '%s' for %s"), *Where, *ParamKey, *Tool->ToolName));
			}
		}
		if (Tool->ActionType == EUE5MCPActionType::SetActorFolder && ActionRequest.FolderPath.IsNone())
		{
			Problems.Add(FString::Printf(TEXT("R9: %s missing required non-empty param 'folder_path'"), *Where));
		}
		// A transform action that changes nothing is a no-op mutation; refuse it so a
		// blank set_actor_transform can never occupy the approval slot.
		if (Tool->ActionType == EUE5MCPActionType::SetActorTransform && ActionRequest.Transform.IsEmpty())
		{
			Problems.Add(FString::Printf(TEXT("R9: %s set_actor_transform needs at least one of 'location', 'rotation', or 'scale'"), *Where));
		}
		if (Tool->ActionType == EUE5MCPActionType::DuplicateActorWithOffset && !ActionRequest.bHasDuplicateOffset)
		{
			Problems.Add(FString::Printf(TEXT("R9: %s missing required param 'offset' (array of 3 numbers)"), *Where));
		}
		if (Tool->ActionType == EUE5MCPActionType::SpawnActorFromClass)
		{
			if (ActionRequest.SpawnClassPath.IsEmpty())
			{
				Problems.Add(FString::Printf(TEXT("R9: %s missing required non-empty param 'class_path'"), *Where));
			}
			if (ActionRequest.SpawnInstances.IsEmpty())
			{
				Problems.Add(FString::Printf(TEXT("R9: %s 'transforms' must contain at least one instance with a 'location'"), *Where));
			}
			if (ActionRequest.SpawnInstances.Num() > MaxSpawnInstancesPerAction)
			{
				Problems.Add(FString::Printf(TEXT("R10: %s spawns %d instances (max %d)"),
					*Where, ActionRequest.SpawnInstances.Num(), MaxSpawnInstancesPerAction));
			}

			// R11 spawn policy: only explicitly allowlisted classes/meshes, ever. The
			// executor re-checks this; the validator refuses it first with a clear rule.
			const UUE5MCPSettings* Settings = GetDefault<UUE5MCPSettings>();
			if (!ActionRequest.SpawnClassPath.IsEmpty() && !Settings->SpawnClassAllowlist.Contains(ActionRequest.SpawnClassPath))
			{
				Problems.Add(FString::Printf(TEXT("R11: %s class '%s' is not on the spawn class allowlist"),
					*Where, *ActionRequest.SpawnClassPath));
			}
			if (!ActionRequest.SpawnMeshPath.IsEmpty())
			{
				if (!Settings->SpawnMeshAllowlist.Contains(ActionRequest.SpawnMeshPath))
				{
					Problems.Add(FString::Printf(TEXT("R11: %s static_mesh '%s' is not on the spawn mesh allowlist"),
						*Where, *ActionRequest.SpawnMeshPath));
				}
				if (ActionRequest.SpawnClassPath != TEXT("/Script/Engine.StaticMeshActor"))
				{
					Problems.Add(FString::Printf(TEXT("R11: %s 'static_mesh' is only valid with class_path '/Script/Engine.StaticMeshActor'"), *Where));
				}
			}
		}

		if (Tool->bRequiresTargets && ActionRequest.TargetPaths.IsEmpty())
		{
			Problems.Add(FString::Printf(TEXT("R6: %s is a mutation with an empty targets list"), *Where));
		}
		if (!Tool->bAcceptsTargets && !ActionRequest.TargetPaths.IsEmpty())
		{
			Problems.Add(FString::Printf(TEXT("R6: %s tool %s does not accept targets"), *Where, *Tool->ToolName));
		}
		if (ActionRequest.TargetPaths.Num() > MaxTargetsPerAction)
		{
			Problems.Add(FString::Printf(TEXT("R10: %s has %d targets (max %d)"),
				*Where, ActionRequest.TargetPaths.Num(), MaxTargetsPerAction));
		}

		// Target gate: every requested path must resolve to a live editor-world actor.
		FUE5MCPResolvedAction Resolved;
		Resolved.Action.Id = ActionRequest.Id;
		Resolved.Action.Type = Tool->ActionType;
		Resolved.Action.Risk = Tool->Risk;
		Resolved.Action.NewFolderPath = ActionRequest.FolderPath;
		Resolved.Action.FindQuery = ActionRequest.FindQuery;
		Resolved.Action.ReadLogsQuery = ActionRequest.ReadLogsQuery;
		Resolved.Action.Transform = ActionRequest.Transform;
		Resolved.Action.DuplicateOffset = ActionRequest.DuplicateOffset;
		Resolved.Action.SpawnClassPath = ActionRequest.SpawnClassPath;
		Resolved.Action.SpawnInstances = ActionRequest.SpawnInstances;
		Resolved.Action.SpawnMeshPath = ActionRequest.SpawnMeshPath;
		Resolved.Action.SpawnLabelBase = ActionRequest.SpawnLabelBase;
		if (!ActionRequest.TargetPaths.IsEmpty() && ActionRequest.TargetPaths.Num() <= MaxTargetsPerAction)
		{
			TArray<FString> MissingPaths;
			Resolved.Action.TargetActors = FUE5MCPTargetResolver::ResolveActorPaths(ActionRequest.TargetPaths, MissingPaths);
			for (const FString& Missing : MissingPaths)
			{
				Problems.Add(FString::Printf(TEXT("R6: %s target not found in the editor world: %s"), *Where, *Missing));
			}
			for (const TWeakObjectPtr<AActor>& ActorPtr : Resolved.Action.TargetActors)
			{
				if (const AActor* Actor = ActorPtr.Get())
				{
					Resolved.TargetLabels.Add(Actor->GetActorLabel());
				}
			}
		}
		Resolved.PreviewText = FUE5MCPPreviewModel::BuildPreviewText(Resolved);
		Result.Plan.Actions.Add(Resolved);
	}

	if (bHasMutation && !Request.bRequiresApproval)
	{
		Problems.Add(TEXT("R5: plan contains mutations but requires_approval is not true"));
	}

	if (bHasDestructive && !Request.bRequiresSecondConfirmation)
	{
		// Destructive actions stay double-gated: the schema must acknowledge the
		// destructive tier here (R7), and the SERVICE additionally refuses destructive
		// plans on the single-click pending path — they only execute through the
		// external-session gate (whose client prompt is never allowlistable) or its
		// belt-and-suspenders in-editor-confirm variant.
		Problems.Add(TEXT("R7: plan contains destructive actions but requires_second_confirmation is not true"));
	}

	if (bHasMutation)
	{
		if (!Request.bHasContextFingerprint || Request.Fingerprint.SceneName.IsEmpty())
		{
			Problems.Add(TEXT("R8: mutation plans need a context_fingerprint with scene and selected_object_paths"));
		}
	}

	Result.Plan.Summary = Request.Summary;
	Result.Plan.ContextWorldName = Request.Fingerprint.SceneName;
	Result.Plan.SelectedActorPathsAtGeneration = Request.Fingerprint.SelectedActorPaths;
	Result.Plan.SelectedActorPathsAtGeneration.Sort();
	Result.Plan.bRequiresApproval = bHasMutation;
	Result.Plan.bRequiresSecondConfirmation = Request.bRequiresSecondConfirmation;
	Result.Plan.Warnings = Problems;
	Result.Plan.bIsValid = Problems.IsEmpty();
	return Result;
}
