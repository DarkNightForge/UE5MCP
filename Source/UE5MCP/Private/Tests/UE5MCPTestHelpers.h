// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "UE5MCPSettings.h"
#include "UE5MCPTypes.h"

// Shared helpers for the UE5MCP headless automation suites. Tests run with
// EditorContext | EngineFilter; never add NonNullRHI or RequiresUser (they
// silently filter tests out of -nullrhi / -unattended runs).
namespace UE5MCPTests
{
	constexpr EAutomationTestFlags KernelTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	inline TArray<AActor*> SpawnTestActors(UWorld* World, int32 Count)
	{
		TArray<AActor*> Actors;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			AActor* Actor = World->SpawnActor<AActor>();
			if (Actor)
			{
				Actor->SetActorLabel(FString::Printf(TEXT("UE5MCPTestActor_%d"), Index));
				Actors.Add(Actor);
			}
		}
		return Actors;
	}

	// Bare AActor has no root component, so SetActorTransform would silently no-op.
	// Transform tests need actors with a registered scene root.
	inline TArray<AActor*> SpawnTransformableTestActors(UWorld* World, int32 Count)
	{
		TArray<AActor*> Actors;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			AActor* Actor = World->SpawnActor<AActor>();
			if (Actor)
			{
				// RF_Transactional is required for the transaction buffer to capture the
				// component's pre-mutation transform: a runtime NewObject does not inherit
				// it (only CreateDefaultSubobject in a constructor does), so without this
				// flag undo/redo would not restore the transform on this synthetic actor.
				USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("TransformRoot"), RF_Transactional);
				Actor->SetRootComponent(Root);
				Root->RegisterComponent();
				Actor->AddInstanceComponent(Root);
				Actor->SetActorLabel(FString::Printf(TEXT("UE5MCPXformActor_%d"), Index));
				Actors.Add(Actor);
			}
		}
		return Actors;
	}

	inline FUE5MCPValidatedPlan BuildSetFolderPlan(const TArray<AActor*>& Actors, const FName FolderPath)
	{
		FUE5MCPResolvedAction Resolved;
		Resolved.Action.Id = TEXT("test-set-folder");
		Resolved.Action.Type = EUE5MCPActionType::SetActorFolder;
		Resolved.Action.Risk = EUE5MCPRiskLevel::LowMutation;
		Resolved.Action.NewFolderPath = FolderPath;
		for (AActor* Actor : Actors)
		{
			Resolved.Action.TargetActors.Add(Actor);
			Resolved.TargetLabels.Add(Actor->GetActorLabel());
		}

		FUE5MCPValidatedPlan Plan;
		Plan.Summary = TEXT("Automation test plan");
		Plan.Actions.Add(Resolved);
		Plan.bIsValid = true;
		Plan.bRequiresApproval = true;
		return Plan;
	}

	inline FUE5MCPValidatedPlan WrapPlanForTest(const FUE5MCPResolvedAction& Resolved)
	{
		FUE5MCPValidatedPlan Plan;
		Plan.Summary = TEXT("Automation test plan");
		Plan.Actions.Add(Resolved);
		Plan.bIsValid = true;
		Plan.bRequiresApproval = true;
		return Plan;
	}

	inline FUE5MCPValidatedPlan BuildSetTransformPlan(const TArray<AActor*>& Actors, const FUE5MCPTransformDelta& Delta)
	{
		FUE5MCPResolvedAction Resolved;
		Resolved.Action.Id = TEXT("test-set-transform");
		Resolved.Action.Type = EUE5MCPActionType::SetActorTransform;
		Resolved.Action.Risk = EUE5MCPRiskLevel::LowMutation;
		Resolved.Action.Transform = Delta;
		for (AActor* Actor : Actors)
		{
			Resolved.Action.TargetActors.Add(Actor);
			Resolved.TargetLabels.Add(Actor->GetActorLabel());
		}
		return WrapPlanForTest(Resolved);
	}

	inline bool LogLinesContain(const TArray<FString>& Lines, const TCHAR* Needle)
	{
		for (const FString& Line : Lines)
		{
			if (Line.Contains(Needle))
			{
				return true;
			}
		}
		return false;
	}

	// Native StaticMeshActors mirror the demo's spawn/duplicate/delete targets:
	// real scene roots, RF_Transactional components, duplicable and destroyable.
	inline TArray<AActor*> SpawnStaticMeshTestActors(UWorld* World, int32 Count, const TCHAR* LabelBase = TEXT("UE5MCPMeshActor"))
	{
		TArray<AActor*> Actors;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>())
			{
				Actor->SetActorLabel(FString::Printf(TEXT("%s_%d"), LabelBase, Index));
				Actors.Add(Actor);
			}
		}
		return Actors;
	}

	inline int32 CountActorsLabeled(const TCHAR* LabelContains)
	{
		int32 Count = 0;
		UEditorActorSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
		if (!Subsystem)
		{
			return 0;
		}
		for (const AActor* Actor : Subsystem->GetAllLevelActors())
		{
			if (IsValid(Actor) && Actor->GetActorLabel().Contains(LabelContains))
			{
				++Count;
			}
		}
		return Count;
	}

	/** Scoped toggle for the external-session approval settings; always restores. */
	struct FScopedExternalApprovalSettings
	{
		explicit FScopedExternalApprovalSettings(bool bAllowExternal, bool bInEditorConfirmDestructive = false)
		{
			UUE5MCPSettings* Settings = GetMutableDefault<UUE5MCPSettings>();
			bOldAllow = Settings->bAllowExternalSessionApproval;
			bOldConfirm = Settings->bRequireInEditorConfirmForDestructive;
			Settings->bAllowExternalSessionApproval = bAllowExternal;
			Settings->bRequireInEditorConfirmForDestructive = bInEditorConfirmDestructive;
		}
		~FScopedExternalApprovalSettings()
		{
			UUE5MCPSettings* Settings = GetMutableDefault<UUE5MCPSettings>();
			Settings->bAllowExternalSessionApproval = bOldAllow;
			Settings->bRequireInEditorConfirmForDestructive = bOldConfirm;
		}
		bool bOldAllow = false;
		bool bOldConfirm = false;
	};

	inline FUE5MCPValidatedPlan BuildDuplicatePlan(const TArray<AActor*>& Actors, const FVector& Offset)
	{
		FUE5MCPResolvedAction Resolved;
		Resolved.Action.Id = TEXT("test-duplicate");
		Resolved.Action.Type = EUE5MCPActionType::DuplicateActorWithOffset;
		Resolved.Action.Risk = EUE5MCPRiskLevel::LowMutation;
		Resolved.Action.DuplicateOffset = Offset;
		for (AActor* Actor : Actors)
		{
			Resolved.Action.TargetActors.Add(Actor);
			Resolved.TargetLabels.Add(Actor->GetActorLabel());
		}
		return WrapPlanForTest(Resolved);
	}

	inline FUE5MCPValidatedPlan BuildSpawnPlan(const FString& ClassPath, const TArray<FUE5MCPSpawnInstance>& Instances,
		const FString& MeshPath = FString(), const FString& LabelBase = FString())
	{
		FUE5MCPResolvedAction Resolved;
		Resolved.Action.Id = TEXT("test-spawn");
		Resolved.Action.Type = EUE5MCPActionType::SpawnActorFromClass;
		Resolved.Action.Risk = EUE5MCPRiskLevel::LowMutation;
		Resolved.Action.SpawnClassPath = ClassPath;
		Resolved.Action.SpawnInstances = Instances;
		Resolved.Action.SpawnMeshPath = MeshPath;
		Resolved.Action.SpawnLabelBase = LabelBase;
		return WrapPlanForTest(Resolved);
	}

	inline FUE5MCPValidatedPlan BuildDeletePlan(const TArray<AActor*>& Actors)
	{
		FUE5MCPResolvedAction Resolved;
		Resolved.Action.Id = TEXT("test-delete");
		Resolved.Action.Type = EUE5MCPActionType::DeleteActor;
		Resolved.Action.Risk = EUE5MCPRiskLevel::Destructive;
		for (AActor* Actor : Actors)
		{
			Resolved.Action.TargetActors.Add(Actor);
			Resolved.TargetLabels.Add(Actor->GetActorLabel());
		}
		FUE5MCPValidatedPlan Plan = WrapPlanForTest(Resolved);
		Plan.bRequiresSecondConfirmation = true;
		return Plan;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
