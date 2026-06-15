// Copyright Epic Games, Inc. All Rights Reserved.

#include "UE5MCPPreviewModel.h"

#include "GameFramework/Actor.h"

FString FUE5MCPPreviewModel::BuildPreviewText(const FUE5MCPResolvedAction& ResolvedAction)
{
	const FUE5MCPAction& Action = ResolvedAction.Action;
	switch (Action.Type)
	{
	case EUE5MCPActionType::SetActorFolder:
		return FString::Printf(TEXT("set_actor_folder: move %d actor(s) to folder '%s'"),
			Action.TargetActors.Num(), *Action.NewFolderPath.ToString());

	case EUE5MCPActionType::SetActorTransform:
	{
		const FUE5MCPTransformDelta& Delta = Action.Transform;
		TArray<FString> Parts;
		if (Delta.bHasLocation) { Parts.Add(FString::Printf(TEXT("location->%s"), *Delta.Location.ToString())); }
		if (Delta.bHasRotation) { Parts.Add(FString::Printf(TEXT("rotation->%s"), *Delta.Rotation.ToString())); }
		if (Delta.bHasScale) { Parts.Add(FString::Printf(TEXT("scale->%s"), *Delta.Scale.ToString())); }

		// Header: exactly which components change, and how many actors are affected.
		FString Text = FString::Printf(TEXT("set_actor_transform: apply %s to %d actor(s)"),
			Parts.IsEmpty() ? TEXT("(no change)") : *FString::Join(Parts, TEXT(", ")),
			Action.TargetActors.Num());

		// One before->after row per affected actor, so the preview shows exactly what changes.
		for (const TWeakObjectPtr<AActor>& ActorPtr : Action.TargetActors)
		{
			const AActor* Actor = ActorPtr.Get();
			if (!Actor)
			{
				continue;
			}
			const FTransform Current = Actor->GetActorTransform();
			TArray<FString> Rows;
			if (Delta.bHasLocation)
			{
				Rows.Add(FString::Printf(TEXT("loc %s -> %s"), *Current.GetLocation().ToString(), *Delta.Location.ToString()));
			}
			if (Delta.bHasRotation)
			{
				Rows.Add(FString::Printf(TEXT("rot %s -> %s"), *Current.GetRotation().Rotator().ToString(), *Delta.Rotation.ToString()));
			}
			if (Delta.bHasScale)
			{
				Rows.Add(FString::Printf(TEXT("scale %s -> %s"), *Current.GetScale3D().ToString(), *Delta.Scale.ToString()));
			}
			Text += FString::Printf(TEXT("\n  %s: %s"), *Actor->GetActorLabel(), *FString::Join(Rows, TEXT("; ")));
		}
		return Text;
	}

	case EUE5MCPActionType::SelectActors:
		return FString::Printf(TEXT("select_actors: change editor selection to %d actor(s)"),
			Action.TargetActors.Num());

	case EUE5MCPActionType::DuplicateActorWithOffset:
	{
		FString Text = FString::Printf(TEXT("duplicate_actor_with_offset: duplicate %d actor(s) with offset %s"),
			Action.TargetActors.Num(), *Action.DuplicateOffset.ToString());
		for (const TWeakObjectPtr<AActor>& ActorPtr : Action.TargetActors)
		{
			if (const AActor* Actor = ActorPtr.Get())
			{
				Text += FString::Printf(TEXT("\n  %s -> copy at %s"), *Actor->GetActorLabel(),
					*(Actor->GetActorLocation() + Action.DuplicateOffset).ToString());
			}
		}
		return Text;
	}

	case EUE5MCPActionType::SpawnActorFromClass:
	{
		FString Text = FString::Printf(TEXT("spawn_actor_from_class: spawn %d instance(s) of '%s'%s%s"),
			Action.SpawnInstances.Num(), *Action.SpawnClassPath,
			Action.SpawnMeshPath.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" with mesh '%s'"), *Action.SpawnMeshPath),
			Action.SpawnLabelBase.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" labeled '%s_N'"), *Action.SpawnLabelBase));
		for (int32 Index = 0; Index < Action.SpawnInstances.Num(); ++Index)
		{
			const FUE5MCPSpawnInstance& Instance = Action.SpawnInstances[Index];
			Text += FString::Printf(TEXT("\n  [%d] at %s"), Index + 1, *Instance.Location.ToString());
			if (!Instance.Rotation.IsNearlyZero())
			{
				Text += FString::Printf(TEXT(" rot %s"), *Instance.Rotation.ToString());
			}
			if (!Instance.Scale.Equals(FVector::OneVector))
			{
				Text += FString::Printf(TEXT(" scale %s"), *Instance.Scale.ToString());
			}
		}
		return Text;
	}

	case EUE5MCPActionType::DeleteActor:
	{
		FString Text = FString::Printf(TEXT("delete_actor (DESTRUCTIVE): permanently delete %d actor(s) — reversible only via editor Undo"),
			Action.TargetActors.Num());
		for (const TWeakObjectPtr<AActor>& ActorPtr : Action.TargetActors)
		{
			if (const AActor* Actor = ActorPtr.Get())
			{
				Text += FString::Printf(TEXT("\n  %s (%s)"), *Actor->GetActorLabel(), *Actor->GetClass()->GetName());
			}
		}
		return Text;
	}

	case EUE5MCPActionType::GetSelectionContext:
		return TEXT("get_selection_context: snapshot the current selection (read-only)");

	case EUE5MCPActionType::ReadLogs:
	{
		const FUE5MCPReadLogsQuery& Query = Action.ReadLogsQuery;
		return FString::Printf(TEXT("read_logs: return up to %d most recent UE5MCP log line(s)%s (read-only)"),
			Query.MaxLines,
			Query.Contains.IsEmpty()
				? TEXT("")
				: *FString::Printf(TEXT(" containing '%s'"), *Query.Contains));
	}

	case EUE5MCPActionType::FindActors:
	{
		const FUE5MCPFindActorsQuery& Query = Action.FindQuery;
		TArray<FString> Filters;
		if (!Query.ClassPath.IsEmpty())
		{
			Filters.Add(FString::Printf(TEXT("class '%s'"), *Query.ClassPath));
		}
		if (!Query.LabelContains.IsEmpty())
		{
			Filters.Add(FString::Printf(TEXT("label contains '%s'"), *Query.LabelContains));
		}
		if (!Query.Tag.IsNone())
		{
			Filters.Add(FString::Printf(TEXT("tag '%s'"), *Query.Tag.ToString()));
		}
		if (!Query.FolderPath.IsNone())
		{
			Filters.Add(FString::Printf(TEXT("folder '%s'"), *Query.FolderPath.ToString()));
		}
		if (Query.bSelectedOnly)
		{
			Filters.Add(TEXT("selected only"));
		}
		return FString::Printf(TEXT("find_actors: search loaded actors%s%s, max %d result(s) (read-only)"),
			Filters.IsEmpty() ? TEXT("") : TEXT(" by "),
			*FString::Join(Filters, TEXT(", ")),
			Query.MaxResults);
	}
	}
	return TEXT("<unknown action>");
}
