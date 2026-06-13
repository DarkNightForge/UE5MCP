// Copyright Epic Games, Inc. All Rights Reserved.

#include "UE5MCPContextCollector.h"

#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Subsystems/EditorActorSubsystem.h"

FUE5MCPContextPack FUE5MCPContextCollector::Collect(int32 MaxLoadedActors)
{
	FUE5MCPContextPack Context;

	if (!GEditor)
	{
		Context.Warnings.Add(TEXT("GEditor is unavailable; UE5MCP only runs in the editor."));
		return Context;
	}

	if (GEditor->PlayWorld)
	{
		Context.Warnings.Add(TEXT("PIE/SIE appears active. The first proof blocks editor mutations while play worlds exist."));
	}

	UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
	if (!EditorWorld)
	{
		Context.Warnings.Add(TEXT("No editor world is available."));
		return Context;
	}

	Context.WorldName = EditorWorld->GetName();

	UEditorActorSubsystem* EditorActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
	if (!EditorActorSubsystem)
	{
		Context.Warnings.Add(TEXT("UEditorActorSubsystem is unavailable."));
		return Context;
	}

	TSet<TWeakObjectPtr<AActor>> SelectedSet;
	const TArray<AActor*> SelectedActors = EditorActorSubsystem->GetSelectedLevelActors();
	for (AActor* Actor : SelectedActors)
	{
		if (IsValid(Actor))
		{
			SelectedSet.Add(Actor);
			Context.SelectedActors.Add(SummarizeActor(Actor, true));
		}
	}

	const TArray<AActor*> LoadedActors = EditorActorSubsystem->GetAllLevelActors();
	const int32 ActorLimit = FMath::Max(0, MaxLoadedActors);
	for (AActor* Actor : LoadedActors)
	{
		if (!IsValid(Actor))
		{
			continue;
		}

		if (Context.LoadedActorsCapped.Num() >= ActorLimit)
		{
			Context.Warnings.Add(FString::Printf(TEXT("Loaded actor context capped at %d actors."), ActorLimit));
			break;
		}

		Context.LoadedActorsCapped.Add(SummarizeActor(Actor, SelectedSet.Contains(Actor)));
	}

	return Context;
}

FUE5MCPActorSummary FUE5MCPContextCollector::SummarizeActor(AActor* Actor, bool bSelected)
{
	FUE5MCPActorSummary Summary;
	if (!IsValid(Actor))
	{
		return Summary;
	}

	Summary.Actor = Actor;
	Summary.ActorPath = Actor->GetPathName();
	Summary.Label = Actor->GetActorLabel();
	Summary.ClassPath = Actor->GetClass() ? Actor->GetClass()->GetPathName() : FString();
	Summary.Tags = Actor->Tags;
	Summary.FolderPath = Actor->GetFolderPath();
	Summary.Transform = Actor->GetActorTransform();
	Summary.bSelected = bSelected;
	return Summary;
}
