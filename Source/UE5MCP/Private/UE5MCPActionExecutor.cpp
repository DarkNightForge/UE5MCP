// Copyright Epic Games, Inc. All Rights Reserved.

#include "UE5MCPActionExecutor.h"

#include "ScopedTransaction.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "UE5MCPContextCollector.h"
#include "UE5MCPEditorService.h"
#include "UE5MCPSettings.h"
#include "UE5MCPTargetResolver.h"

#define LOCTEXT_NAMESPACE "FUE5MCPActionExecutor"

FUE5MCPExecutionResult FUE5MCPActionExecutor::ExecuteApprovedPlan(const FUE5MCPValidatedPlan& Plan)
{
	FUE5MCPExecutionResult Result;

	if (!Plan.bIsValid)
	{
		Result.UpfrontRefusalCode = TEXT("invalid_plan");
		Result.UserVisibleLogLines.Add(TEXT("Rejected: plan was not validated."));
		return Result;
	}

	const bool bHasMutations = HasMutations(Plan);
	if (bHasMutations)
	{
		FString BlockReason;
		if (IsEditorMutationBlocked(&BlockReason))
		{
			Result.UpfrontRefusalCode = (GEditor && GEditor->PlayWorld)
				? TEXT("play_mode_active")
				: TEXT("editor_unavailable");
			Result.UserVisibleLogLines.Add(FString::Printf(TEXT("Rejected: %s"), *BlockReason));
			return Result;
		}
	}

	FScopedTransaction Transaction(LOCTEXT("ApplyApprovedUE5MCPPlan", "UE5MCP: Apply Approved Actor Plan"), bHasMutations);

	bool bAllSucceeded = true;
	for (const FUE5MCPResolvedAction& ResolvedAction : Plan.Actions)
	{
		FUE5MCPActionResult ActionResult;
		switch (ResolvedAction.Action.Type)
		{
		case EUE5MCPActionType::SetActorFolder:
			ActionResult = ExecuteSetActorFolder(ResolvedAction);
			break;
		case EUE5MCPActionType::SetActorTransform:
			ActionResult = ExecuteSetActorTransform(ResolvedAction);
			break;
		case EUE5MCPActionType::GetSelectionContext:
			ActionResult = ExecuteGetSelectionContext(ResolvedAction);
			break;
		case EUE5MCPActionType::FindActors:
			ActionResult = ExecuteFindActors(ResolvedAction);
			break;
		case EUE5MCPActionType::ReadLogs:
			ActionResult = ExecuteReadLogs(ResolvedAction);
			break;
		case EUE5MCPActionType::SelectActors:
			ActionResult = ExecuteSelectActors(ResolvedAction);
			break;
		case EUE5MCPActionType::DuplicateActorWithOffset:
			ActionResult = ExecuteDuplicateActorWithOffset(ResolvedAction);
			break;
		case EUE5MCPActionType::SpawnActorFromClass:
			ActionResult = ExecuteSpawnActorFromClass(ResolvedAction);
			break;
		case EUE5MCPActionType::DeleteActor:
			ActionResult = ExecuteDeleteActor(ResolvedAction);
			break;
		default:
			ActionResult.ActionId = ResolvedAction.Action.Id;
			ActionResult.bSuccess = false;
			ActionResult.RefusalCode = TEXT("not_implemented");
			ActionResult.Message = TEXT("Rejected: action is not implemented in the first executor proof.");
			break;
		}

		bAllSucceeded &= ActionResult.bSuccess;
		Result.UserVisibleLogLines.Add(ActionResult.Message);
		Result.ActionResults.Add(ActionResult);
	}

	Result.bSuccess = bAllSucceeded;
	return Result;
}

bool FUE5MCPActionExecutor::IsEditorMutationBlocked(FString* OutReason)
{
	if (!GEditor)
	{
		if (OutReason)
		{
			*OutReason = TEXT("GEditor is unavailable; editor mutations are blocked.");
		}
		return true;
	}

	if (GEditor->PlayWorld)
	{
		if (OutReason)
		{
			*OutReason = TEXT("PIE/SIE is active; editor mutations are blocked for the UE5MCP first proof.");
		}
		return true;
	}

	if (!GEditor->GetEditorWorldContext().World())
	{
		if (OutReason)
		{
			*OutReason = TEXT("No editor world is available; editor mutations are blocked.");
		}
		return true;
	}

	return false;
}

bool FUE5MCPActionExecutor::HasMutations(const FUE5MCPValidatedPlan& Plan)
{
	for (const FUE5MCPResolvedAction& ResolvedAction : Plan.Actions)
	{
		if (ResolvedAction.Action.Type == EUE5MCPActionType::SetActorFolder ||
			ResolvedAction.Action.Type == EUE5MCPActionType::SelectActors ||
			ResolvedAction.Action.Type == EUE5MCPActionType::SetActorTransform ||
			ResolvedAction.Action.Type == EUE5MCPActionType::DuplicateActorWithOffset ||
			ResolvedAction.Action.Type == EUE5MCPActionType::SpawnActorFromClass ||
			ResolvedAction.Action.Type == EUE5MCPActionType::DeleteActor)
		{
			return true;
		}
	}
	return false;
}

FUE5MCPActionResult FUE5MCPActionExecutor::ExecuteSetActorFolder(const FUE5MCPResolvedAction& ResolvedAction)
{
	FUE5MCPActionResult Result;
	Result.ActionId = ResolvedAction.Action.Id;

	if (ResolvedAction.Action.NewFolderPath.IsNone())
	{
		Result.Message = TEXT("Rejected set_actor_folder: folder path was empty.");
		return Result;
	}

	const int32 RequestedCount = ResolvedAction.Action.TargetActors.Num();
	int32 ChangedCount = 0;
	for (const TWeakObjectPtr<AActor>& ActorPtr : ResolvedAction.Action.TargetActors)
	{
		AActor* Actor = ActorPtr.Get();
		if (!IsValid(Actor))
		{
			continue;
		}

		Actor->Modify();
		Actor->SetFolderPath(ResolvedAction.Action.NewFolderPath);
		Actor->MarkPackageDirty();
		++ChangedCount;
	}

	// Match the spawn/duplicate convention: success requires every requested target
	// to have been mutated, and the message reports M-of-N when some went stale.
	Result.ChangedCount = ChangedCount;
	Result.bSuccess = ChangedCount > 0 && ChangedCount == RequestedCount;
	Result.Message = Result.bSuccess
		? FString::Printf(TEXT("set_actor_folder succeeded for %d actor(s) -> %s"),
			ChangedCount, *ResolvedAction.Action.NewFolderPath.ToString())
		: (ChangedCount == 0
			? TEXT("set_actor_folder found no valid actors to mutate.")
			: FString::Printf(TEXT("set_actor_folder changed %d of %d requested actor(s) -> %s"),
				ChangedCount, RequestedCount, *ResolvedAction.Action.NewFolderPath.ToString()));
	return Result;
}

FUE5MCPActionResult FUE5MCPActionExecutor::ExecuteSetActorTransform(const FUE5MCPResolvedAction& ResolvedAction)
{
	const FUE5MCPAction& Action = ResolvedAction.Action;
	FUE5MCPActionResult Result;
	Result.ActionId = Action.Id;

	// No-op guard (defence in depth): the validator already refuses empty transform
	// requests, but the executor never trusts an unvalidated plan to have done so.
	if (Action.Transform.IsEmpty())
	{
		Result.RefusalCode = TEXT("noop_transform");
		Result.Message = TEXT("Rejected set_actor_transform: no transform fields were provided.");
		return Result;
	}

	const int32 RequestedCount = Action.TargetActors.Num();
	int32 ChangedCount = 0;
	for (const TWeakObjectPtr<AActor>& ActorPtr : Action.TargetActors)
	{
		AActor* Actor = ActorPtr.Get();
		if (!IsValid(Actor))
		{
			continue;
		}

		// Actors with no root component cannot be transformed (e.g. pure logic actors);
		// report them per-actor rather than silently counting them as changed.
		USceneComponent* Root = Actor->GetRootComponent();
		if (!Root)
		{
			Result.FoundActors.Add(FUE5MCPContextCollector::SummarizeActor(Actor, false));
			continue;
		}

		// Capture both the actor and its root component for Undo before mutating.
		Actor->Modify();
		Root->Modify();

		FTransform NewTransform = Actor->GetActorTransform();
		if (Action.Transform.bHasLocation)
		{
			NewTransform.SetLocation(Action.Transform.Location);
		}
		if (Action.Transform.bHasRotation)
		{
			NewTransform.SetRotation(Action.Transform.Rotation.Quaternion());
		}
		if (Action.Transform.bHasScale)
		{
			NewTransform.SetScale3D(Action.Transform.Scale);
		}

		Actor->SetActorTransform(NewTransform);
		Actor->PostEditMove(/*bFinished=*/true);
		Actor->MarkPackageDirty();

		// Per-actor structured result: the post-mutation summary carries the new transform.
		Result.FoundActors.Add(FUE5MCPContextCollector::SummarizeActor(Actor, false));
		++ChangedCount;
	}

	Result.ChangedCount = ChangedCount;
	Result.bSuccess = ChangedCount > 0 && ChangedCount == RequestedCount;
	Result.Message = Result.bSuccess
		? FString::Printf(TEXT("set_actor_transform succeeded for %d actor(s)"), ChangedCount)
		: (ChangedCount == 0
			? TEXT("set_actor_transform found no transformable actors to mutate.")
			: FString::Printf(TEXT("set_actor_transform changed %d of %d requested actor(s) (others had no root component or went stale)"),
				ChangedCount, RequestedCount));
	return Result;
}

FUE5MCPActionResult FUE5MCPActionExecutor::ExecuteDuplicateActorWithOffset(const FUE5MCPResolvedAction& ResolvedAction)
{
	const FUE5MCPAction& Action = ResolvedAction.Action;
	FUE5MCPActionResult Result;
	Result.ActionId = Action.Id;

	UEditorActorSubsystem* ActorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
	if (!ActorSubsystem)
	{
		Result.Message = TEXT("Rejected duplicate_actor_with_offset: editor subsystem unavailable.");
		return Result;
	}

	TArray<AActor*> SourceActors;
	for (const TWeakObjectPtr<AActor>& ActorPtr : Action.TargetActors)
	{
		if (AActor* Actor = ActorPtr.Get(); IsValid(Actor))
		{
			SourceActors.Add(Actor);
		}
	}
	if (SourceActors.IsEmpty())
	{
		Result.Message = TEXT("duplicate_actor_with_offset found no valid actors to duplicate.");
		return Result;
	}

	// DuplicateActors opens its own transaction internally; it nests under the plan's
	// outer FScopedTransaction, so the whole approved batch stays one undo step.
	const TArray<AActor*> Duplicates = ActorSubsystem->DuplicateActors(SourceActors, /*ToWorld=*/nullptr, Action.DuplicateOffset);
	for (AActor* Duplicate : Duplicates)
	{
		if (IsValid(Duplicate))
		{
			Duplicate->MarkPackageDirty();
			// Structured per-actor result: the new copy's path lets the client target it next.
			Result.FoundActors.Add(FUE5MCPContextCollector::SummarizeActor(Duplicate, false));
		}
	}

	Result.ChangedCount = Result.FoundActors.Num();
	Result.bSuccess = Result.ChangedCount == SourceActors.Num() && Result.ChangedCount > 0;
	Result.Message = Result.bSuccess
		? FString::Printf(TEXT("duplicate_actor_with_offset created %d duplicate(s) with offset %s"),
			Result.ChangedCount, *Action.DuplicateOffset.ToString())
		: FString::Printf(TEXT("duplicate_actor_with_offset created %d of %d requested duplicate(s)"),
			Result.ChangedCount, SourceActors.Num());
	return Result;
}

FUE5MCPActionResult FUE5MCPActionExecutor::ExecuteSpawnActorFromClass(const FUE5MCPResolvedAction& ResolvedAction)
{
	const FUE5MCPAction& Action = ResolvedAction.Action;
	FUE5MCPActionResult Result;
	Result.ActionId = Action.Id;

	// Allowlist re-check (defence in depth): the validator already enforced the spawn
	// policy, but the executor never trusts an unvalidated plan to have done so.
	const UUE5MCPSettings* Settings = GetDefault<UUE5MCPSettings>();
	if (!Settings->SpawnClassAllowlist.Contains(Action.SpawnClassPath))
	{
		Result.RefusalCode = TEXT("class_not_allowlisted");
		Result.Message = FString::Printf(TEXT("Rejected spawn_actor_from_class: class '%s' is not on the spawn class allowlist."), *Action.SpawnClassPath);
		return Result;
	}
	if (!Action.SpawnMeshPath.IsEmpty() && !Settings->SpawnMeshAllowlist.Contains(Action.SpawnMeshPath))
	{
		Result.RefusalCode = TEXT("mesh_not_allowlisted");
		Result.Message = FString::Printf(TEXT("Rejected spawn_actor_from_class: mesh '%s' is not on the spawn mesh allowlist."), *Action.SpawnMeshPath);
		return Result;
	}
	if (Action.SpawnInstances.IsEmpty())
	{
		Result.RefusalCode = TEXT("noop_spawn");
		Result.Message = TEXT("Rejected spawn_actor_from_class: no spawn instances were provided.");
		return Result;
	}

	UClass* SpawnClass = LoadClass<AActor>(nullptr, *Action.SpawnClassPath);
	if (!SpawnClass)
	{
		Result.RefusalCode = TEXT("class_not_found");
		Result.Message = FString::Printf(TEXT("Rejected spawn_actor_from_class: class '%s' could not be loaded."), *Action.SpawnClassPath);
		return Result;
	}

	UStaticMesh* Mesh = nullptr;
	if (!Action.SpawnMeshPath.IsEmpty())
	{
		Mesh = LoadObject<UStaticMesh>(nullptr, *Action.SpawnMeshPath);
		if (!Mesh)
		{
			Result.RefusalCode = TEXT("mesh_not_found");
			Result.Message = FString::Printf(TEXT("Rejected spawn_actor_from_class: mesh asset '%s' could not be loaded."), *Action.SpawnMeshPath);
			return Result;
		}
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		Result.Message = TEXT("Rejected spawn_actor_from_class: no editor world available.");
		return Result;
	}

	int32 SpawnedCount = 0;
	for (int32 Index = 0; Index < Action.SpawnInstances.Num(); ++Index)
	{
		const FUE5MCPSpawnInstance& Instance = Action.SpawnInstances[Index];
		// Spawn directly through UWorld rather than the editor subsystem: the
		// subsystem's path runs viewport placement logic (it can move the actor
		// toward the viewport surface, and its hit-proxy read crashes headless).
		// We have exact typed transforms; RF_Transactional puts the creation in
		// the outer transaction so undo removes the actor.
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.ObjectFlags = RF_Transactional;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Spawned = World->SpawnActor(SpawnClass, &Instance.Location, &Instance.Rotation, SpawnParameters);
		if (!IsValid(Spawned))
		{
			continue;
		}

		// Creation is captured by the outer transaction (RF_Transactional spawn), so
		// undo removes the actor; follow-up tweaks ride in the same transaction.
		if (!Instance.Scale.Equals(FVector::OneVector))
		{
			Spawned->Modify();
			Spawned->SetActorScale3D(Instance.Scale);
		}
		if (!Action.SpawnLabelBase.IsEmpty())
		{
			Spawned->SetActorLabel(FString::Printf(TEXT("%s_%d"), *Action.SpawnLabelBase, Index + 1));
		}
		if (Mesh)
		{
			if (AStaticMeshActor* MeshActor = Cast<AStaticMeshActor>(Spawned))
			{
				if (UStaticMeshComponent* MeshComponent = MeshActor->GetStaticMeshComponent())
				{
					MeshComponent->Modify();
					MeshComponent->SetStaticMesh(Mesh);
				}
			}
		}
		Spawned->MarkPackageDirty();

		Result.FoundActors.Add(FUE5MCPContextCollector::SummarizeActor(Spawned, false));
		++SpawnedCount;
	}

	Result.ChangedCount = SpawnedCount;
	Result.bSuccess = SpawnedCount == Action.SpawnInstances.Num() && SpawnedCount > 0;
	Result.Message = Result.bSuccess
		? FString::Printf(TEXT("spawn_actor_from_class spawned %d instance(s) of '%s'"), SpawnedCount, *Action.SpawnClassPath)
		: FString::Printf(TEXT("spawn_actor_from_class spawned %d of %d requested instance(s) of '%s'"),
			SpawnedCount, Action.SpawnInstances.Num(), *Action.SpawnClassPath);
	return Result;
}

FUE5MCPActionResult FUE5MCPActionExecutor::ExecuteDeleteActor(const FUE5MCPResolvedAction& ResolvedAction)
{
	const FUE5MCPAction& Action = ResolvedAction.Action;
	FUE5MCPActionResult Result;
	Result.ActionId = Action.Id;

	UEditorActorSubsystem* ActorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
	if (!ActorSubsystem)
	{
		Result.Message = TEXT("Rejected delete_actor: editor subsystem unavailable.");
		return Result;
	}

	TArray<AActor*> ActorsToDelete;
	for (const TWeakObjectPtr<AActor>& ActorPtr : Action.TargetActors)
	{
		if (AActor* Actor = ActorPtr.Get(); IsValid(Actor))
		{
			ActorsToDelete.Add(Actor);
		}
	}
	if (ActorsToDelete.IsEmpty())
	{
		Result.Message = TEXT("delete_actor found no valid actors to delete.");
		return Result;
	}

	// Summarize BEFORE destroying: the structured result records exactly what was
	// removed (the audit trail outlives the actors).
	for (AActor* Actor : ActorsToDelete)
	{
		Result.FoundActors.Add(FUE5MCPContextCollector::SummarizeActor(Actor, false));
	}

	// DestroyActors opens its own transaction; it nests under the plan's outer
	// FScopedTransaction so the destructive batch stays one standard undo step.
	// Its bool return is whether ANY actor was destroyed — not how many — so we
	// derive the true count from the targets themselves (a destroyed actor's weak
	// pointer no longer resolves to a valid object).
	ActorSubsystem->DestroyActors(ActorsToDelete);

	int32 DestroyedCount = 0;
	for (const TWeakObjectPtr<AActor>& ActorPtr : Action.TargetActors)
	{
		AActor* Actor = ActorPtr.Get();
		if (!IsValid(Actor) || Actor->IsActorBeingDestroyed())
		{
			++DestroyedCount;
		}
	}

	const int32 RequestedCount = ActorsToDelete.Num();
	Result.ChangedCount = DestroyedCount;
	Result.bSuccess = DestroyedCount > 0 && DestroyedCount == RequestedCount;
	Result.Message = Result.bSuccess
		? FString::Printf(TEXT("delete_actor destroyed %d actor(s) (DESTRUCTIVE; reversible via editor Undo)"), DestroyedCount)
		: (DestroyedCount == 0
			? FString::Printf(TEXT("delete_actor failed to destroy %d requested actor(s)"), RequestedCount)
			: FString::Printf(TEXT("delete_actor destroyed %d of %d requested actor(s) (DESTRUCTIVE; reversible via editor Undo)"),
				DestroyedCount, RequestedCount));
	return Result;
}

FUE5MCPActionResult FUE5MCPActionExecutor::ExecuteGetSelectionContext(const FUE5MCPResolvedAction& ResolvedAction)
{
	FUE5MCPActionResult Result;
	Result.ActionId = ResolvedAction.Action.Id;

	const FUE5MCPContextPack Context = FUE5MCPContextCollector::Collect();
	Result.FoundActors = Context.SelectedActors;
	Result.bSuccess = true;
	Result.Message = FString::Printf(
		TEXT("get_selection_context: %d selected actor(s) in world '%s'%s"),
		Context.SelectedActors.Num(),
		Context.WorldName.IsEmpty() ? TEXT("<none>") : *Context.WorldName,
		Context.Warnings.IsEmpty() ? TEXT("") : TEXT(" (with warnings)"));
	return Result;
}

FUE5MCPActionResult FUE5MCPActionExecutor::ExecuteFindActors(const FUE5MCPResolvedAction& ResolvedAction)
{
	FUE5MCPActionResult Result;
	Result.ActionId = ResolvedAction.Action.Id;

	UEditorActorSubsystem* ActorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
	if (!ActorSubsystem)
	{
		Result.Message = TEXT("Rejected find_actors: editor subsystem unavailable.");
		return Result;
	}

	bool bTruncated = false;
	const TArray<AActor*> Found = FUE5MCPTargetResolver::FindActors(ResolvedAction.Action.FindQuery, bTruncated);

	TSet<AActor*> SelectedSet;
	SelectedSet.Append(ActorSubsystem->GetSelectedLevelActors());
	for (AActor* Actor : Found)
	{
		Result.FoundActors.Add(FUE5MCPContextCollector::SummarizeActor(Actor, SelectedSet.Contains(Actor)));
	}

	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("find_actors matched %d actor(s)%s"),
		Result.FoundActors.Num(),
		bTruncated ? TEXT(" (results truncated at the cap)") : TEXT(""));
	return Result;
}

FUE5MCPActionResult FUE5MCPActionExecutor::ExecuteReadLogs(const FUE5MCPResolvedAction& ResolvedAction)
{
	const FUE5MCPAction& Action = ResolvedAction.Action;
	FUE5MCPActionResult Result;
	Result.ActionId = Action.Id;

	// Clamp the request to the buffer's hard bound (defence in depth): the buffer
	// holds at most MaxBufferedLines, and a client cannot ask for more or for zero.
	const int32 MaxLines = FMath::Clamp(Action.ReadLogsQuery.MaxLines, 1, FUE5MCPLog::MaxBufferedLines);
	const FString& Filter = Action.ReadLogsQuery.Contains;

	// Snapshot the structured log buffer. This read runs during execution, BEFORE the
	// service appends this call's own result line, so read_logs never describes itself.
	const TArray<FString>& AllLines = FUE5MCPEditorService::Get().GetLog().GetLines();

	TArray<FString> Matched;
	for (const FString& Line : AllLines)
	{
		if (Filter.IsEmpty() || Line.Contains(Filter))
		{
			Matched.Add(Line);
		}
	}

	// Keep the most recent MaxLines (oldest dropped), preserving chronological order.
	const int32 TotalMatched = Matched.Num();
	const bool bTruncated = TotalMatched > MaxLines;
	if (bTruncated)
	{
		Matched.RemoveAt(0, TotalMatched - MaxLines);
	}

	Result.LogLines = MoveTemp(Matched);
	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("read_logs returned %d of %d matching log line(s)%s%s (read-only)"),
		Result.LogLines.Num(), TotalMatched,
		Filter.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" filtered by '%s'"), *Filter),
		bTruncated ? TEXT(" (older lines truncated at the cap)") : TEXT(""));
	return Result;
}

FUE5MCPActionResult FUE5MCPActionExecutor::ExecuteSelectActors(const FUE5MCPResolvedAction& ResolvedAction)
{
	FUE5MCPActionResult Result;
	Result.ActionId = ResolvedAction.Action.Id;

	UEditorActorSubsystem* ActorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
	if (!ActorSubsystem)
	{
		Result.Message = TEXT("Rejected select_actors: editor subsystem unavailable.");
		return Result;
	}

	TArray<AActor*> ActorsToSelect;
	for (const TWeakObjectPtr<AActor>& ActorPtr : ResolvedAction.Action.TargetActors)
	{
		AActor* Actor = ActorPtr.Get();
		if (IsValid(Actor))
		{
			ActorsToSelect.Add(Actor);
		}
	}
	if (ActorsToSelect.IsEmpty())
	{
		Result.Message = TEXT("select_actors found no valid actors to select.");
		return Result;
	}

	ActorSubsystem->SetSelectedLevelActors(ActorsToSelect);

	// SetSelectedLevelActors silently no-ops when selection is locked; verify by re-reading.
	const TArray<AActor*> NowSelected = ActorSubsystem->GetSelectedLevelActors();
	int32 SelectedCount = 0;
	for (AActor* Actor : ActorsToSelect)
	{
		if (NowSelected.Contains(Actor))
		{
			++SelectedCount;
		}
	}

	Result.ChangedCount = SelectedCount;
	Result.bSuccess = SelectedCount == ActorsToSelect.Num();
	Result.Message = Result.bSuccess
		? FString::Printf(TEXT("select_actors selected %d actor(s)"), SelectedCount)
		: FString::Printf(TEXT("select_actors selected %d of %d requested actor(s) (selection may be locked)"), SelectedCount, ActorsToSelect.Num());
	return Result;
}

#undef LOCTEXT_NAMESPACE
