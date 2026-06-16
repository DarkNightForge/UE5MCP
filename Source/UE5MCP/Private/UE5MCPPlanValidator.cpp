// Copyright Epic Games, Inc. All Rights Reserved.

#include "UE5MCPPlanValidator.h"

#include "GameFramework/Actor.h"
#include "UE5MCPActionExecutor.h"
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
		// A label action with no (or only whitespace) label is a no-op mutation; refuse it.
		if (Tool->ActionType == EUE5MCPActionType::SetActorLabel && ActionRequest.NewLabel.IsEmpty())
		{
			Problems.Add(FString::Printf(TEXT("R9: %s missing required non-empty param 'label'"), *Where));
		}
		// Tag add/remove with no tags is a no-op mutation; refuse it (the parser already
		// dropped empty tag strings, so an empty list here means nothing usable was sent).
		if ((Tool->ActionType == EUE5MCPActionType::AddActorTags || Tool->ActionType == EUE5MCPActionType::RemoveActorTags)
			&& ActionRequest.Tags.IsEmpty())
		{
			Problems.Add(FString::Printf(TEXT("R9: %s 'tags' must contain at least one non-empty tag"), *Where));
		}
		if (Tool->ActionType == EUE5MCPActionType::SetActorProperty)
		{
			if (ActionRequest.PropertyName.IsEmpty())
			{
				Problems.Add(FString::Printf(TEXT("R9: %s missing required non-empty param 'property'"), *Where));
			}
			if (!ActionRequest.PropertyValue.IsSet())
			{
				Problems.Add(FString::Printf(TEXT("R9: %s missing required param 'value'"), *Where));
			}

			// R12 property policy: only explicitly allowlisted (class, property, type)
			// tuples may be written, ever. The executor re-checks against the live object;
			// the validator refuses first with a clear rule and (on a miss) the allowed set.
			if (!ActionRequest.PropertyName.IsEmpty() && ActionRequest.PropertyValue.IsSet())
			{
				const UUE5MCPSettings* Settings = GetDefault<UUE5MCPSettings>();
				const FUE5MCPPropertyValue::EKind Kind = ActionRequest.PropertyValue.Kind;
				bool bNameAllowed = false;       // some entry matches name (+ component filter)
				bool bTypeMatched = false;       // ...and its declared type matches the value kind
				bool bRangeOk = true;
				for (const FUE5MCPPropertyAllowEntry& Entry : Settings->PropertyAllowlist)
				{
					if (Entry.PropertyName != FName(*ActionRequest.PropertyName))
					{
						continue;
					}
					if (!ActionRequest.PropertyComponentClass.IsEmpty() && Entry.ClassPath != ActionRequest.PropertyComponentClass)
					{
						continue;
					}
					bNameAllowed = true;

					const FString& Type = Entry.Type;
					const bool bKindMatches =
						((Type == TEXT("float") || Type == TEXT("int")) && Kind == FUE5MCPPropertyValue::EKind::Number) ||
						(Type == TEXT("bool") && Kind == FUE5MCPPropertyValue::EKind::Bool) ||
						(Type == TEXT("vector") && Kind == FUE5MCPPropertyValue::EKind::Vector) ||
						(Type == TEXT("color") && Kind == FUE5MCPPropertyValue::EKind::Color) ||
						// name / enum / asset all arrive as a JSON string; the live property
						// type (FName/FStr vs enum vs object) disambiguates in the executor.
						((Type == TEXT("name") || Type == TEXT("enum") || Type == TEXT("asset")) && Kind == FUE5MCPPropertyValue::EKind::Name);
					if (!bKindMatches)
					{
						continue;
					}
					bTypeMatched = true;

					if (Entry.bHasRange && Kind == FUE5MCPPropertyValue::EKind::Number)
					{
						if (ActionRequest.PropertyValue.Number < Entry.Min || ActionRequest.PropertyValue.Number > Entry.Max)
						{
							bRangeOk = false;
						}
					}
					if (bTypeMatched && bRangeOk)
					{
						break;
					}
				}

				if (!bNameAllowed)
				{
					TArray<FString> Allowed;
					for (const FUE5MCPPropertyAllowEntry& Entry : Settings->PropertyAllowlist)
					{
						Allowed.AddUnique(FString::Printf(TEXT("%s.%s (%s)"), *Entry.ClassPath, *Entry.PropertyName.ToString(), *Entry.Type));
					}
					Problems.Add(FString::Printf(TEXT("R12: %s property '%s'%s is not on the property allowlist. Allowed: %s"),
						*Where, *ActionRequest.PropertyName,
						ActionRequest.PropertyComponentClass.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" on '%s'"), *ActionRequest.PropertyComponentClass),
						Allowed.IsEmpty() ? TEXT("(none configured)") : *FString::Join(Allowed, TEXT("; "))));
				}
				else if (!bTypeMatched)
				{
					Problems.Add(FString::Printf(TEXT("R12: %s value type does not match the allowlisted type for property '%s'"),
						*Where, *ActionRequest.PropertyName));
				}
				else if (!bRangeOk)
				{
					Problems.Add(FString::Printf(TEXT("R12: %s value %g for property '%s' is outside the allowed range"),
						*Where, ActionRequest.PropertyValue.Number, *ActionRequest.PropertyName));
				}
			}
		}
		if (Tool->ActionType == EUE5MCPActionType::CheckOutPackage && ActionRequest.PackageName.IsEmpty())
		{
			Problems.Add(FString::Printf(TEXT("R9: %s missing required non-empty param 'package_name'"), *Where));
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
		Resolved.Action.NewLabel = ActionRequest.NewLabel;
		Resolved.Action.Tags = ActionRequest.Tags;
		Resolved.Action.PropertyName = ActionRequest.PropertyName;
		Resolved.Action.PropertyComponentClass = ActionRequest.PropertyComponentClass;
		Resolved.Action.PropertyComponentName = ActionRequest.PropertyComponentName;
		Resolved.Action.PackageName = ActionRequest.PackageName;
		Resolved.Action.PropertyValue = ActionRequest.PropertyValue;
		Resolved.Action.FindQuery = ActionRequest.FindQuery;
		Resolved.Action.ReadLogsQuery = ActionRequest.ReadLogsQuery;
		Resolved.Action.PackageQuery = ActionRequest.PackageQuery;
		Resolved.Action.GetPropertiesQuery = ActionRequest.GetPropertiesQuery;
		Resolved.Action.GetComponentsQuery = ActionRequest.GetComponentsQuery;
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
		// Surface package writability in the preview for mutations that dirty a package
		// (everything mutating except transient selection), so the human sees blast
		// radius — and any not-writable / not-checked-out target — before approving.
		if (Tool->Risk != EUE5MCPRiskLevel::ReadOnly && Tool->ActionType != EUE5MCPActionType::SelectActors)
		{
			bool bPackagesBlocked = false;
			const FString PackageNote = FUE5MCPActionExecutor::DescribeActionPackages(Resolved, bPackagesBlocked);
			if (!PackageNote.IsEmpty())
			{
				Resolved.PreviewText += TEXT("\n  ") + PackageNote;
			}
		}
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
