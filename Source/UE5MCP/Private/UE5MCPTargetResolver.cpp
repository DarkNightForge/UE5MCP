// Copyright Epic Games, Inc. All Rights Reserved.

#include "UE5MCPTargetResolver.h"

#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Subsystems/EditorActorSubsystem.h"

AActor* FUE5MCPTargetResolver::ResolveActorByPath(const FString& ActorPath)
{
	if (!GEditor || ActorPath.IsEmpty())
	{
		return nullptr;
	}

	UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
	UEditorActorSubsystem* ActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
	if (!EditorWorld || !ActorSubsystem)
	{
		return nullptr;
	}

	// Context packs report full object paths ("/Path/Map.Map:PersistentLevel.Name") while
	// GetActorReference resolves editor-world-relative paths ("PersistentLevel.Name").
	AActor* Found = FindObject<AActor>(nullptr, *ActorPath);
	if (!Found)
	{
		FString RelativePath = ActorPath;
		const FString WorldPrefix = EditorWorld->GetPathName() + TEXT(":");
		if (RelativePath.StartsWith(WorldPrefix))
		{
			RelativePath.RightChopInline(WorldPrefix.Len());
		}
		Found = ActorSubsystem->GetActorReference(RelativePath);
	}

	return (IsValid(Found) && Found->GetWorld() == EditorWorld) ? Found : nullptr;
}

TArray<TWeakObjectPtr<AActor>> FUE5MCPTargetResolver::ResolveActorPaths(
	const TArray<FString>& ActorPaths, TArray<FString>& OutMissingPaths)
{
	TArray<TWeakObjectPtr<AActor>> Resolved;
	for (const FString& ActorPath : ActorPaths)
	{
		if (AActor* Actor = ResolveActorByPath(ActorPath))
		{
			Resolved.Add(Actor);
		}
		else
		{
			OutMissingPaths.Add(ActorPath);
		}
	}
	return Resolved;
}

TArray<AActor*> FUE5MCPTargetResolver::FindActors(const FUE5MCPFindActorsQuery& Query, bool& bOutTruncated)
{
	bOutTruncated = false;
	TArray<AActor*> Results;

	UEditorActorSubsystem* ActorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
	if (!ActorSubsystem)
	{
		return Results;
	}

	UClass* FilterClass = Query.ClassPath.IsEmpty() ? nullptr : FindObject<UClass>(nullptr, *Query.ClassPath);

	TSet<AActor*> SelectedSet;
	if (Query.bSelectedOnly)
	{
		SelectedSet.Append(ActorSubsystem->GetSelectedLevelActors());
	}

	for (AActor* Actor : ActorSubsystem->GetAllLevelActors())
	{
		if (!IsValid(Actor))
		{
			continue;
		}
		if (!Query.ClassPath.IsEmpty())
		{
			if (FilterClass)
			{
				if (!Actor->IsA(FilterClass))
				{
					continue;
				}
			}
			else if (!Actor->GetClass()->GetPathName().Contains(Query.ClassPath))
			{
				continue;
			}
		}
		if (!Query.LabelContains.IsEmpty() && !Actor->GetActorLabel().Contains(Query.LabelContains))
		{
			continue;
		}
		if (!Query.Tag.IsNone() && !Actor->Tags.Contains(Query.Tag))
		{
			continue;
		}
		if (!Query.FolderPath.IsNone())
		{
			const FString ActorFolder = Actor->GetFolderPath().ToString();
			const FString QueryFolder = Query.FolderPath.ToString();
			if (ActorFolder != QueryFolder && !ActorFolder.StartsWith(QueryFolder + TEXT("/")))
			{
				continue;
			}
		}
		if (Query.bSelectedOnly && !SelectedSet.Contains(Actor))
		{
			continue;
		}
		Results.Add(Actor);
	}

	Results.Sort([](const AActor& A, const AActor& B)
	{
		return A.GetActorLabel() < B.GetActorLabel();
	});

	const int32 MaxResults = FMath::Clamp(Query.MaxResults, 1, 200);
	if (Results.Num() > MaxResults)
	{
		bOutTruncated = true;
		Results.SetNum(MaxResults);
	}
	return Results;
}
