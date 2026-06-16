// Copyright Epic Games, Inc. All Rights Reserved.

#include "UE5MCPPreviewModel.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "UE5MCPSettings.h"
#include "UObject/UnrealType.h"

namespace
{
	/** Human-readable form of the intended set_actor_property value. */
	FString FormatPropertyValue(const FUE5MCPPropertyValue& Value)
	{
		using EKind = FUE5MCPPropertyValue::EKind;
		switch (Value.Kind)
		{
		case EKind::Number: return FString::Printf(TEXT("%g"), Value.Number);
		case EKind::Bool: return Value.Bool ? TEXT("true") : TEXT("false");
		case EKind::Vector: return Value.Vector.ToString();
		case EKind::Color: return Value.Color.ToString();
		case EKind::Name: return FString::Printf(TEXT("'%s'"), *Value.Name);
		default: return TEXT("(unset)");
		}
	}

	/** Best-effort current value of the targeted property on an actor, for the before→after
	 *  preview. Resolves the owner the same way the executor does (allowlist-driven), but
	 *  reads only — returns empty when it cannot resolve. */
	FString ReadCurrentPropertyText(const AActor* Actor, const FUE5MCPAction& Action)
	{
		if (!Actor)
		{
			return FString();
		}
		const UUE5MCPSettings* Settings = GetDefault<UUE5MCPSettings>();
		const FName PropName(*Action.PropertyName);
		const UObject* Owner = nullptr;
		for (const FUE5MCPPropertyAllowEntry& Entry : Settings->PropertyAllowlist)
		{
			if (Entry.PropertyName != PropName)
			{
				continue;
			}
			if (!Action.PropertyComponentClass.IsEmpty() && Entry.ClassPath != Action.PropertyComponentClass)
			{
				continue;
			}
			UClass* OwnerClass = FindObject<UClass>(nullptr, *Entry.ClassPath);
			if (!OwnerClass)
			{
				continue;
			}
			if (OwnerClass->IsChildOf(AActor::StaticClass()))
			{
				if (Actor->IsA(OwnerClass)) { Owner = Actor; break; }
			}
			else if (OwnerClass->IsChildOf(UActorComponent::StaticClass()))
			{
				TArray<UActorComponent*> Components;
				const_cast<AActor*>(Actor)->GetComponents(OwnerClass, Components);
				if (!Action.PropertyComponentName.IsEmpty())
				{
					for (UActorComponent* Component : Components)
					{
						if (Component->GetName() == Action.PropertyComponentName) { Owner = Component; break; }
					}
					if (Owner) { break; }
				}
				else if (Components.Num() == 1) { Owner = Components[0]; break; }
			}
		}
		if (!Owner)
		{
			return FString();
		}
		// Walk the (possibly dotted) property path read-only to the leaf, mirroring the executor.
		TArray<FString> Segments;
		Action.PropertyName.ParseIntoArray(Segments, TEXT("."));
		if (Segments.Num() == 0)
		{
			return FString();
		}
		const UStruct* CurStruct = Owner->GetClass();
		const void* CurBase = static_cast<const void*>(Owner);
		for (int32 Index = 0; Index < Segments.Num() - 1; ++Index)
		{
			const FStructProperty* StructProp = CastField<FStructProperty>(FindFProperty<FProperty>(CurStruct, FName(*Segments[Index])));
			if (!StructProp)
			{
				return FString();
			}
			CurBase = StructProp->ContainerPtrToValuePtr<void>(CurBase);
			CurStruct = StructProp->Struct;
		}
		const FProperty* Leaf = FindFProperty<FProperty>(CurStruct, FName(*Segments.Last()));
		if (!Leaf)
		{
			return FString();
		}
		FString Out;
		Leaf->ExportTextItem_Direct(Out, Leaf->ContainerPtrToValuePtr<void>(CurBase), nullptr, nullptr, PPF_None);
		return Out;
	}
}

FString FUE5MCPPreviewModel::BuildPreviewText(const FUE5MCPResolvedAction& ResolvedAction)
{
	const FUE5MCPAction& Action = ResolvedAction.Action;
	switch (Action.Type)
	{
	case EUE5MCPActionType::SetActorFolder:
		return FString::Printf(TEXT("set_actor_folder: move %d actor(s) to folder '%s'"),
			Action.TargetActors.Num(), *Action.NewFolderPath.ToString());

	case EUE5MCPActionType::SetActorLabel:
	{
		FString Text = FString::Printf(TEXT("set_actor_label: set label of %d actor(s) to '%s'"),
			Action.TargetActors.Num(), *Action.NewLabel);
		for (const TWeakObjectPtr<AActor>& ActorPtr : Action.TargetActors)
		{
			if (const AActor* Actor = ActorPtr.Get())
			{
				Text += FString::Printf(TEXT("\n  %s -> '%s'"), *Actor->GetActorLabel(), *Action.NewLabel);
			}
		}
		return Text;
	}

	case EUE5MCPActionType::AddActorTags:
	case EUE5MCPActionType::RemoveActorTags:
	{
		const bool bAdd = Action.Type == EUE5MCPActionType::AddActorTags;
		TArray<FString> TagStrings;
		for (const FName& Tag : Action.Tags)
		{
			TagStrings.Add(Tag.ToString());
		}
		FString Text = FString::Printf(TEXT("%s: %s tag(s) [%s] %s %d actor(s)"),
			bAdd ? TEXT("add_actor_tags") : TEXT("remove_actor_tags"),
			bAdd ? TEXT("add") : TEXT("remove"),
			*FString::Join(TagStrings, TEXT(", ")),
			bAdd ? TEXT("to") : TEXT("from"),
			Action.TargetActors.Num());
		for (const TWeakObjectPtr<AActor>& ActorPtr : Action.TargetActors)
		{
			const AActor* Actor = ActorPtr.Get();
			if (!Actor)
			{
				continue;
			}
			TArray<FString> CurrentTags;
			for (const FName& Tag : Actor->Tags)
			{
				CurrentTags.Add(Tag.ToString());
			}
			Text += FString::Printf(TEXT("\n  %s: tags [%s]"), *Actor->GetActorLabel(),
				*FString::Join(CurrentTags, TEXT(", ")));
		}
		return Text;
	}

	case EUE5MCPActionType::SetActorProperty:
	{
		const FString ValueText = FormatPropertyValue(Action.PropertyValue);
		FString Text = FString::Printf(TEXT("set_actor_property: set '%s'%s%s = %s on %d actor(s)"),
			*Action.PropertyName,
			Action.PropertyComponentClass.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" on '%s'"), *Action.PropertyComponentClass),
			Action.PropertyComponentName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" [component '%s']"), *Action.PropertyComponentName),
			*ValueText, Action.TargetActors.Num());
		for (const TWeakObjectPtr<AActor>& ActorPtr : Action.TargetActors)
		{
			const AActor* Actor = ActorPtr.Get();
			if (!Actor)
			{
				continue;
			}
			const FString Current = ReadCurrentPropertyText(Actor, Action);
			Text += FString::Printf(TEXT("\n  %s: %s -> %s"), *Actor->GetActorLabel(),
				Current.IsEmpty() ? TEXT("(unresolved)") : *Current, *ValueText);
		}
		return Text;
	}

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

	case EUE5MCPActionType::GetPackageStatus:
	{
		const FUE5MCPPackageStatusQuery& Query = Action.PackageQuery;
		return FString::Printf(TEXT("get_package_status: report up to %d %spackage(s) and the source-control summary (read-only)"),
			Query.MaxPackages,
			Query.bDirtyOnly ? TEXT("dirty ") : TEXT("loaded "));
	}

	case EUE5MCPActionType::GetActorProperties:
	{
		const FUE5MCPGetPropertiesQuery& Query = Action.GetPropertiesQuery;
		return FString::Printf(TEXT("get_actor_properties: list up to %d %s%sproperty(ies)%s of the first target%s (read-only)"),
			Query.MaxProperties,
			Query.bAllowlistedOnly ? TEXT("allowlisted ") : TEXT(""),
			Query.bEditableOnly ? TEXT("editable ") : TEXT(""),
			Action.PropertyComponentClass.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" on its '%s'"), *Action.PropertyComponentClass),
			Action.TargetActors.Num() > 1 ? *FString::Printf(TEXT(" (of %d)"), Action.TargetActors.Num()) : TEXT(""));
	}

	case EUE5MCPActionType::GetActorComponents:
	{
		const FUE5MCPGetComponentsQuery& Query = Action.GetComponentsQuery;
		return FString::Printf(TEXT("get_actor_components: list up to %d component(s) of the first target%s (read-only)"),
			Query.MaxComponents,
			Action.TargetActors.Num() > 1 ? *FString::Printf(TEXT(" (of %d)"), Action.TargetActors.Num()) : TEXT(""));
	}

	case EUE5MCPActionType::ListCapabilities:
		return TEXT("list_capabilities: report the live tool registry + configured allowlists/policy (read-only)");

	case EUE5MCPActionType::CheckOutPackage:
		return FString::Printf(TEXT("check_out_package: check out '%s' from source control (real SC write; NOT editor-undoable — revert via source control)"),
			*Action.PackageName);

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
