// Copyright Epic Games, Inc. All Rights Reserved.

#include "UE5MCPActionExecutor.h"

#include "ScopedTransaction.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlState.h"
#include "SourceControlHelpers.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "UObject/EnumProperty.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
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
		case EUE5MCPActionType::SetActorLabel:
			ActionResult = ExecuteSetActorLabel(ResolvedAction);
			break;
		case EUE5MCPActionType::AddActorTags:
			ActionResult = ExecuteAddActorTags(ResolvedAction);
			break;
		case EUE5MCPActionType::RemoveActorTags:
			ActionResult = ExecuteRemoveActorTags(ResolvedAction);
			break;
		case EUE5MCPActionType::SetActorProperty:
			ActionResult = ExecuteSetActorProperty(ResolvedAction);
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
		case EUE5MCPActionType::GetPackageStatus:
			ActionResult = ExecuteGetPackageStatus(ResolvedAction);
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
			ResolvedAction.Action.Type == EUE5MCPActionType::SetActorLabel ||
			ResolvedAction.Action.Type == EUE5MCPActionType::AddActorTags ||
			ResolvedAction.Action.Type == EUE5MCPActionType::RemoveActorTags ||
			ResolvedAction.Action.Type == EUE5MCPActionType::SetActorProperty ||
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

FUE5MCPActionResult FUE5MCPActionExecutor::ExecuteSetActorLabel(const FUE5MCPResolvedAction& ResolvedAction)
{
	const FUE5MCPAction& Action = ResolvedAction.Action;
	FUE5MCPActionResult Result;
	Result.ActionId = Action.Id;

	// No-op guard (defence in depth): the validator already refuses an empty label,
	// but the executor never trusts an unvalidated plan to have done so.
	if (Action.NewLabel.IsEmpty())
	{
		Result.RefusalCode = TEXT("noop_label");
		Result.Message = TEXT("Rejected set_actor_label: label was empty.");
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

		Actor->Modify();
		// SetActorLabel sets the editor display label; it does not force uniqueness, so
		// targets may share a label (labels are display-only). The post-mutation summary
		// reports the resulting label per actor.
		Actor->SetActorLabel(Action.NewLabel);
		Actor->MarkPackageDirty();
		Result.FoundActors.Add(FUE5MCPContextCollector::SummarizeActor(Actor, false));
		++ChangedCount;
	}

	Result.ChangedCount = ChangedCount;
	Result.bSuccess = ChangedCount > 0 && ChangedCount == RequestedCount;
	Result.Message = Result.bSuccess
		? FString::Printf(TEXT("set_actor_label succeeded for %d actor(s) -> '%s'"), ChangedCount, *Action.NewLabel)
		: (ChangedCount == 0
			? TEXT("set_actor_label found no valid actors to mutate.")
			: FString::Printf(TEXT("set_actor_label changed %d of %d requested actor(s) -> '%s' (others went stale)"),
				ChangedCount, RequestedCount, *Action.NewLabel));
	return Result;
}

FUE5MCPActionResult FUE5MCPActionExecutor::ExecuteAddActorTags(const FUE5MCPResolvedAction& ResolvedAction)
{
	const FUE5MCPAction& Action = ResolvedAction.Action;
	FUE5MCPActionResult Result;
	Result.ActionId = Action.Id;

	if (Action.Tags.IsEmpty())
	{
		Result.RefusalCode = TEXT("noop_tags");
		Result.Message = TEXT("Rejected add_actor_tags: no tags were provided.");
		return Result;
	}

	const int32 RequestedCount = Action.TargetActors.Num();
	int32 ValidCount = 0;
	int32 MutatedCount = 0;
	for (const TWeakObjectPtr<AActor>& ActorPtr : Action.TargetActors)
	{
		AActor* Actor = ActorPtr.Get();
		if (!IsValid(Actor))
		{
			continue;
		}
		++ValidCount;

		// Add only the tags the actor does not already carry, so the op is idempotent:
		// re-adding an existing tag changes nothing and is not counted as a mutation.
		bool bActorChanged = false;
		for (const FName& Tag : Action.Tags)
		{
			if (!Actor->Tags.Contains(Tag))
			{
				if (!bActorChanged)
				{
					Actor->Modify();
					bActorChanged = true;
				}
				Actor->Tags.Add(Tag);
			}
		}
		if (bActorChanged)
		{
			Actor->MarkPackageDirty();
			++MutatedCount;
		}
		Result.FoundActors.Add(FUE5MCPContextCollector::SummarizeActor(Actor, false));
	}

	Result.ChangedCount = MutatedCount;
	// Idempotent success: every requested target resolved to a live actor and now
	// carries the tags. MutatedCount reports how many actually changed.
	Result.bSuccess = ValidCount > 0 && ValidCount == RequestedCount;
	Result.Message = Result.bSuccess
		? FString::Printf(TEXT("add_actor_tags added %d tag(s) to %d actor(s) (%d already had them)"),
			Action.Tags.Num(), MutatedCount, ValidCount - MutatedCount)
		: (ValidCount == 0
			? TEXT("add_actor_tags found no valid actors to mutate.")
			: FString::Printf(TEXT("add_actor_tags applied to %d of %d requested actor(s) (others went stale)"),
				ValidCount, RequestedCount));
	return Result;
}

FUE5MCPActionResult FUE5MCPActionExecutor::ExecuteRemoveActorTags(const FUE5MCPResolvedAction& ResolvedAction)
{
	const FUE5MCPAction& Action = ResolvedAction.Action;
	FUE5MCPActionResult Result;
	Result.ActionId = Action.Id;

	if (Action.Tags.IsEmpty())
	{
		Result.RefusalCode = TEXT("noop_tags");
		Result.Message = TEXT("Rejected remove_actor_tags: no tags were provided.");
		return Result;
	}

	const int32 RequestedCount = Action.TargetActors.Num();
	int32 ValidCount = 0;
	int32 MutatedCount = 0;
	for (const TWeakObjectPtr<AActor>& ActorPtr : Action.TargetActors)
	{
		AActor* Actor = ActorPtr.Get();
		if (!IsValid(Actor))
		{
			continue;
		}
		++ValidCount;

		// Remove only the tags the actor actually carries, so the op is idempotent:
		// removing an absent tag changes nothing and is not counted as a mutation.
		bool bActorChanged = false;
		for (const FName& Tag : Action.Tags)
		{
			if (Actor->Tags.Contains(Tag))
			{
				if (!bActorChanged)
				{
					Actor->Modify();
					bActorChanged = true;
				}
				Actor->Tags.Remove(Tag);
			}
		}
		if (bActorChanged)
		{
			Actor->MarkPackageDirty();
			++MutatedCount;
		}
		Result.FoundActors.Add(FUE5MCPContextCollector::SummarizeActor(Actor, false));
	}

	Result.ChangedCount = MutatedCount;
	Result.bSuccess = ValidCount > 0 && ValidCount == RequestedCount;
	Result.Message = Result.bSuccess
		? FString::Printf(TEXT("remove_actor_tags removed %d tag(s) from %d actor(s) (%d did not have them)"),
			Action.Tags.Num(), MutatedCount, ValidCount - MutatedCount)
		: (ValidCount == 0
			? TEXT("remove_actor_tags found no valid actors to mutate.")
			: FString::Printf(TEXT("remove_actor_tags applied to %d of %d requested actor(s) (others went stale)"),
				ValidCount, RequestedCount));
	return Result;
}

namespace
{
	UClass* ResolveClassByPath(const FString& Path)
	{
		if (Path.IsEmpty())
		{
			return nullptr;
		}
		if (UClass* Found = FindObject<UClass>(nullptr, *Path))
		{
			return Found;
		}
		return LoadObject<UClass>(nullptr, *Path);
	}

	FString PropertyValueToText(const FProperty* Prop, const void* ValuePtr)
	{
		FString Out;
		Prop->ExportTextItem_Direct(Out, ValuePtr, /*DefaultValue=*/nullptr, /*Parent=*/nullptr, PPF_None);
		return Out;
	}

	/** Walk a dotted property path ("Struct.Member.Leaf") from a UObject. On success sets
	 *  the leaf property + its value ptr AND the immediate container (struct + base ptr) so
	 *  a sibling override flag can be written. Returns false + a refusal on any miss. */
	bool ResolvePropertyPath(UObject* Owner, const FString& Path, FProperty*& OutLeaf, void*& OutLeafPtr,
		UStruct*& OutContainerStruct, void*& OutContainerBase, FString& OutRefusal)
	{
		TArray<FString> Segments;
		Path.ParseIntoArray(Segments, TEXT("."));
		if (Segments.Num() == 0)
		{
			OutRefusal = TEXT("property_not_found");
			return false;
		}
		UStruct* CurStruct = Owner->GetClass();
		void* CurBase = static_cast<void*>(Owner);
		// Intermediate segments must each be a struct we can descend into.
		for (int32 Index = 0; Index < Segments.Num() - 1; ++Index)
		{
			const FStructProperty* StructProp = CastField<FStructProperty>(FindFProperty<FProperty>(CurStruct, FName(*Segments[Index])));
			if (!StructProp)
			{
				OutRefusal = TEXT("property_not_found");
				return false;
			}
			CurBase = StructProp->ContainerPtrToValuePtr<void>(CurBase);
			CurStruct = StructProp->Struct;
		}
		FProperty* Leaf = FindFProperty<FProperty>(CurStruct, FName(*Segments.Last()));
		if (!Leaf)
		{
			OutRefusal = TEXT("property_not_found");
			return false;
		}
		OutLeaf = Leaf;
		OutLeafPtr = Leaf->ContainerPtrToValuePtr<void>(CurBase);
		OutContainerStruct = CurStruct;
		OutContainerBase = CurBase;
		return true;
	}

	/** Type-safe reflection write. Returns true if the value was written; on a type
	 *  mismatch returns false and sets OutRefusal (the allowlist already vetted the
	 *  declared type, but the executor never trusts the plan to match the live class).
	 *  ResolvedAsset is the pre-loaded, class-checked object for asset-ref writes. */
	bool WritePropertyValue(const FProperty* Prop, void* ValuePtr, const FUE5MCPPropertyValue& Value, UObject* ResolvedAsset, FString& OutRefusal)
	{
		using EKind = FUE5MCPPropertyValue::EKind;
		switch (Value.Kind)
		{
		case EKind::Number:
			if (const FFloatProperty* P = CastField<FFloatProperty>(Prop)) { P->SetPropertyValue(ValuePtr, static_cast<float>(Value.Number)); return true; }
			if (const FDoubleProperty* P = CastField<FDoubleProperty>(Prop)) { P->SetPropertyValue(ValuePtr, Value.Number); return true; }
			if (const FIntProperty* P = CastField<FIntProperty>(Prop)) { P->SetPropertyValue(ValuePtr, static_cast<int32>(Value.Number)); return true; }
			if (const FInt64Property* P = CastField<FInt64Property>(Prop)) { P->SetPropertyValue(ValuePtr, static_cast<int64>(Value.Number)); return true; }
			break;
		case EKind::Bool:
			if (const FBoolProperty* P = CastField<FBoolProperty>(Prop)) { P->SetPropertyValue(ValuePtr, Value.Bool); return true; }
			break;
		case EKind::Vector:
			if (const FStructProperty* P = CastField<FStructProperty>(Prop))
			{
				if (P->Struct == TBaseStructure<FVector>::Get()) { *static_cast<FVector*>(ValuePtr) = Value.Vector; return true; }
			}
			break;
		case EKind::Color:
			if (const FStructProperty* P = CastField<FStructProperty>(Prop))
			{
				if (P->Struct == TBaseStructure<FLinearColor>::Get()) { *static_cast<FLinearColor*>(ValuePtr) = Value.Color; return true; }
				if (P->Struct == TBaseStructure<FColor>::Get()) { *static_cast<FColor*>(ValuePtr) = Value.Color.ToFColor(/*bSRGB=*/true); return true; }
			}
			break;
		case EKind::Name:
			// A JSON string targets an FName/FString, an enum (by value name), or an
			// object/asset reference — disambiguated by the LIVE property type.
			if (const FNameProperty* P = CastField<FNameProperty>(Prop)) { P->SetPropertyValue(ValuePtr, FName(*Value.Name)); return true; }
			if (const FStrProperty* P = CastField<FStrProperty>(Prop)) { P->SetPropertyValue(ValuePtr, Value.Name); return true; }
			if (const FEnumProperty* P = CastField<FEnumProperty>(Prop))
			{
				const UEnum* Enum = P->GetEnum();
				const int64 EnumValue = Enum ? Enum->GetValueByNameString(Value.Name) : INDEX_NONE;
				if (EnumValue == INDEX_NONE) { OutRefusal = TEXT("property_value_invalid_enum"); return false; }
				P->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, EnumValue);
				return true;
			}
			if (const FNumericProperty* P = CastField<FNumericProperty>(Prop))
			{
				// Legacy TEnumAsByte: a numeric property backed by a UEnum.
				if (const UEnum* Enum = P->GetIntPropertyEnum())
				{
					const int64 EnumValue = Enum->GetValueByNameString(Value.Name);
					if (EnumValue == INDEX_NONE) { OutRefusal = TEXT("property_value_invalid_enum"); return false; }
					P->SetIntPropertyValue(ValuePtr, EnumValue);
					return true;
				}
			}
			if (const FObjectPropertyBase* P = CastField<FObjectPropertyBase>(Prop))
			{
				if (!ResolvedAsset) { OutRefusal = TEXT("asset_not_resolved"); return false; }
				P->SetObjectPropertyValue(ValuePtr, ResolvedAsset);
				return true;
			}
			break;
		default:
			break;
		}
		OutRefusal = TEXT("property_type_mismatch");
		return false;
	}
}

FUE5MCPActionResult FUE5MCPActionExecutor::ExecuteSetActorProperty(const FUE5MCPResolvedAction& ResolvedAction)
{
	const FUE5MCPAction& Action = ResolvedAction.Action;
	FUE5MCPActionResult Result;
	Result.ActionId = Action.Id;

	// No-op / shape guard (defence in depth): the validator already refuses these.
	if (Action.PropertyName.IsEmpty() || !Action.PropertyValue.IsSet())
	{
		Result.RefusalCode = TEXT("invalid_property_request");
		Result.Message = TEXT("Rejected set_actor_property: requires a non-empty 'property' and a 'value'.");
		return Result;
	}

	using EKind = FUE5MCPPropertyValue::EKind;
	const EKind Kind = Action.PropertyValue.Kind;
	const FName PropName(*Action.PropertyName);

	// Re-derive the allowlist candidates (defence in depth — never trust the plan to
	// have been validated). A candidate matches by name, the optional component filter,
	// the declared type vs the value kind, and the optional range.
	const UUE5MCPSettings* Settings = GetDefault<UUE5MCPSettings>();
	TArray<FUE5MCPPropertyAllowEntry> Candidates;
	bool bNameAllowed = false;
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
		bNameAllowed = true;

		const FString& Type = Entry.Type;
		const bool bKindMatches =
			((Type == TEXT("float") || Type == TEXT("int")) && Kind == EKind::Number) ||
			(Type == TEXT("bool") && Kind == EKind::Bool) ||
			(Type == TEXT("vector") && Kind == EKind::Vector) ||
			(Type == TEXT("color") && Kind == EKind::Color) ||
			// name / enum / asset all arrive as a JSON string; the live property type disambiguates.
			((Type == TEXT("name") || Type == TEXT("enum") || Type == TEXT("asset")) && Kind == EKind::Name);
		if (!bKindMatches)
		{
			continue;
		}
		if (Entry.bHasRange && Kind == EKind::Number &&
			(Action.PropertyValue.Number < Entry.Min || Action.PropertyValue.Number > Entry.Max))
		{
			Result.RefusalCode = TEXT("property_value_out_of_range");
			Result.Message = FString::Printf(TEXT("Rejected set_actor_property: value %g for '%s' is outside the allowed range [%g, %g]."),
				Action.PropertyValue.Number, *Action.PropertyName, Entry.Min, Entry.Max);
			return Result;
		}
		Candidates.Add(Entry);
	}
	if (!bNameAllowed)
	{
		Result.RefusalCode = TEXT("property_not_allowlisted");
		Result.Message = FString::Printf(TEXT("Rejected set_actor_property: property '%s' is not on the property allowlist."), *Action.PropertyName);
		return Result;
	}
	if (Candidates.IsEmpty())
	{
		Result.RefusalCode = TEXT("property_type_mismatch");
		Result.Message = FString::Printf(TEXT("Rejected set_actor_property: the value type does not match the allowlisted type for '%s'."), *Action.PropertyName);
		return Result;
	}

	const int32 RequestedCount = Action.TargetActors.Num();
	int32 ResolvedCount = 0;
	int32 ChangedCount = 0;
	int32 ComponentMisses = 0;
	TArray<FString> ChangeRows;
	for (const TWeakObjectPtr<AActor>& ActorPtr : Action.TargetActors)
	{
		AActor* Actor = ActorPtr.Get();
		if (!IsValid(Actor))
		{
			continue;
		}

		// Resolve the matched (allowlist entry, owning object) for this target: an actor the
		// target IS, or a unique component of the entry's class on the target.
		const FUE5MCPPropertyAllowEntry* MatchedEntry = nullptr;
		UObject* Owner = nullptr;
		bool bAmbiguous = false;
		for (const FUE5MCPPropertyAllowEntry& Entry : Candidates)
		{
			UClass* OwnerClass = ResolveClassByPath(Entry.ClassPath);
			if (!OwnerClass)
			{
				continue;
			}
			UObject* Candidate = nullptr;
			if (OwnerClass->IsChildOf(AActor::StaticClass()))
			{
				if (Actor->IsA(OwnerClass)) { Candidate = Actor; }
			}
			else if (OwnerClass->IsChildOf(UActorComponent::StaticClass()))
			{
				TArray<UActorComponent*> Components;
				Actor->GetComponents(OwnerClass, Components);
				if (Components.Num() == 1) { Candidate = Components[0]; }
				else if (Components.Num() > 1) { bAmbiguous = true; }
			}
			if (Candidate)
			{
				if (Owner && Owner != Candidate) { bAmbiguous = true; }
				Owner = Candidate;
				MatchedEntry = &Entry;
			}
		}
		if (bAmbiguous)
		{
			++ComponentMisses;
			ChangeRows.Add(FString::Printf(TEXT("%s: ambiguous owner (more than one matching component)"), *Actor->GetActorLabel()));
			continue;
		}
		if (!Owner || !MatchedEntry)
		{
			++ComponentMisses;
			ChangeRows.Add(FString::Printf(TEXT("%s: no matching owner component/class found"), *Actor->GetActorLabel()));
			continue;
		}

		// Resolve the (possibly dotted) property path to its leaf + container struct.
		FProperty* Leaf = nullptr;
		void* LeafPtr = nullptr;
		UStruct* ContainerStruct = nullptr;
		void* ContainerBase = nullptr;
		FString ResolveRefusal;
		if (!ResolvePropertyPath(Owner, MatchedEntry->PropertyName.ToString(), Leaf, LeafPtr, ContainerStruct, ContainerBase, ResolveRefusal))
		{
			++ComponentMisses;
			ChangeRows.Add(FString::Printf(TEXT("%s: %s for '%s' on %s"), *Actor->GetActorLabel(), *ResolveRefusal, *Action.PropertyName, *Owner->GetClass()->GetName()));
			continue;
		}

		// Asset-ref: load + class-check up front. A hard miss is a malformed action → refuse it all.
		UObject* ResolvedAsset = nullptr;
		if (MatchedEntry->Type == TEXT("asset"))
		{
			ResolvedAsset = LoadObject<UObject>(nullptr, *Action.PropertyValue.Name);
			if (!ResolvedAsset)
			{
				Result.RefusalCode = TEXT("asset_not_found");
				Result.Message = FString::Printf(TEXT("Rejected set_actor_property: asset '%s' could not be loaded."), *Action.PropertyValue.Name);
				return Result;
			}
			if (UClass* AllowedClass = ResolveClassByPath(MatchedEntry->AssetClass))
			{
				if (!ResolvedAsset->IsA(AllowedClass))
				{
					Result.RefusalCode = TEXT("asset_class_not_allowed");
					Result.Message = FString::Printf(TEXT("Rejected set_actor_property: asset '%s' is not a %s."), *Action.PropertyValue.Name, *AllowedClass->GetName());
					return Result;
				}
			}
		}

		const FString Before = PropertyValueToText(Leaf, LeafPtr);

		Actor->Modify();
		Owner->Modify();
		Owner->PreEditChange(Leaf);
		FString WriteRefusal;
		if (!WritePropertyValue(Leaf, LeafPtr, Action.PropertyValue, ResolvedAsset, WriteRefusal))
		{
			// Mismatch/invalid against the LIVE property — a malformed/misconfigured request.
			Result.RefusalCode = WriteRefusal;
			Result.Message = FString::Printf(TEXT("Rejected set_actor_property: '%s' on %s could not be written (%s)."),
				*Action.PropertyName, *Owner->GetClass()->GetName(), *WriteRefusal);
			return Result;
		}

		// Paired override flag (e.g. bOverride_BloomIntensity) lives in the leaf's container struct.
		FString OverrideNote;
		if (!MatchedEntry->OverrideFlag.IsEmpty())
		{
			const FBoolProperty* OverrideProp = CastField<FBoolProperty>(FindFProperty<FProperty>(ContainerStruct, FName(*MatchedEntry->OverrideFlag)));
			if (!OverrideProp)
			{
				Result.RefusalCode = TEXT("override_flag_not_found");
				Result.Message = FString::Printf(TEXT("Rejected set_actor_property: override flag '%s' is not a bool on %s."),
					*MatchedEntry->OverrideFlag, *ContainerStruct->GetName());
				return Result;
			}
			OverrideProp->SetPropertyValue(OverrideProp->ContainerPtrToValuePtr<void>(ContainerBase), true);
			OverrideNote = FString::Printf(TEXT(" (+%s=true)"), *MatchedEntry->OverrideFlag);
		}

		FPropertyChangedEvent ChangeEvent(Leaf);
		Owner->PostEditChangeProperty(ChangeEvent);

		const FString After = PropertyValueToText(Leaf, LeafPtr);
		Owner->MarkPackageDirty();
		++ResolvedCount;
		if (Before != After)
		{
			++ChangedCount;
		}
		ChangeRows.Add(FString::Printf(TEXT("%s [%s.%s]: %s -> %s%s"),
			*Actor->GetActorLabel(), *Owner->GetClass()->GetName(), *Action.PropertyName, *Before, *After, *OverrideNote));
	}

	Result.ChangedCount = ChangedCount;
	Result.bSuccess = ResolvedCount > 0 && ResolvedCount == RequestedCount;
	const FString Detail = ChangeRows.IsEmpty() ? FString() : (TEXT("\n  ") + FString::Join(ChangeRows, TEXT("\n  ")));
	Result.Message = Result.bSuccess
		? FString::Printf(TEXT("set_actor_property '%s' applied to %d actor(s) (%d changed)%s"),
			*Action.PropertyName, ResolvedCount, ChangedCount, *Detail)
		: (ResolvedCount == 0
			? FString::Printf(TEXT("set_actor_property '%s' resolved no writable owner on %d requested actor(s)%s"),
				*Action.PropertyName, RequestedCount, *Detail)
			: FString::Printf(TEXT("set_actor_property '%s' applied to %d of %d requested actor(s) (%d missed owner/property)%s"),
				*Action.PropertyName, ResolvedCount, RequestedCount, ComponentMisses, *Detail));
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

namespace
{
	/** Hard cap on packages get_package_status returns; a client cannot ask for more. */
	constexpr int32 MaxPackageStatusResults = 500;

	/** Classify a cached source-control state into a stable machine token. The state is
	 *  whatever the provider has cached (EStateCacheUsage::Use); we never trigger a
	 *  blocking source-control network operation from a model-originated request. */
	FString ClassifySourceControlState(const FSourceControlStatePtr& State)
	{
		if (!State.IsValid())
		{
			return TEXT("unknown");
		}
		if (State->IsCheckedOutOther())
		{
			return TEXT("checked_out_other");
		}
		if (State->IsCheckedOut())
		{
			return TEXT("checked_out");
		}
		if (State->IsAdded())
		{
			return TEXT("added");
		}
		if (State->IsDeleted())
		{
			return TEXT("deleted");
		}
		if (!State->IsSourceControlled())
		{
			return TEXT("not_controlled");
		}
		if (!State->IsCurrent())
		{
			return TEXT("not_current");
		}
		return TEXT("up_to_date");
	}
}

FUE5MCPActionResult FUE5MCPActionExecutor::ExecuteGetPackageStatus(const FUE5MCPResolvedAction& ResolvedAction)
{
	const FUE5MCPAction& Action = ResolvedAction.Action;
	FUE5MCPActionResult Result;
	Result.ActionId = Action.Id;
	Result.bHasPackageStatus = true;

	const int32 MaxPackages = FMath::Clamp(Action.PackageQuery.MaxPackages, 1, MaxPackageStatusResults);
	const bool bDirtyOnly = Action.PackageQuery.bDirtyOnly;

	// Source-control summary (read from the active provider; never starts a connection).
	ISourceControlModule& SCModule = ISourceControlModule::Get();
	ISourceControlProvider& Provider = SCModule.GetProvider();
	Result.SourceControl.bEnabled = SCModule.IsEnabled();
	Result.SourceControl.bAvailable = Result.SourceControl.bEnabled && Provider.IsAvailable();
	Result.SourceControl.ProviderName = Provider.GetName().ToString();

	// Collect the packages of interest. Dirty packages are the blast radius a save or
	// mutation would touch; the optional non-dirty path adds other loaded on-disk packages.
	TArray<UPackage*> Packages;
	FEditorFileUtils::GetDirtyContentPackages(Packages);
	FEditorFileUtils::GetDirtyWorldPackages(Packages);
	if (!bDirtyOnly)
	{
		for (TObjectIterator<UPackage> It; It; ++It)
		{
			UPackage* Package = *It;
			if (!Package || Package->HasAnyFlags(RF_Transient) || Package == GetTransientPackage())
			{
				continue;
			}
			if (!FPackageName::IsValidLongPackageName(Package->GetName()))
			{
				continue;
			}
			Packages.AddUnique(Package);
		}
	}

	const int32 TotalPackages = Packages.Num();
	Result.bPackagesTruncated = TotalPackages > MaxPackages;

	int32 Reported = 0;
	for (UPackage* Package : Packages)
	{
		if (Reported >= MaxPackages)
		{
			break;
		}
		if (!Package)
		{
			continue;
		}

		FUE5MCPPackageState State;
		State.PackageName = Package->GetName();
		State.bDirty = Package->IsDirty();
		State.Filename = USourceControlHelpers::PackageFilename(State.PackageName);
		if (Result.SourceControl.bAvailable && !State.Filename.IsEmpty())
		{
			const FSourceControlStatePtr SCState = Provider.GetState(State.Filename, EStateCacheUsage::Use);
			State.SourceControlState = ClassifySourceControlState(SCState);
		}
		else
		{
			State.SourceControlState = TEXT("source_control_disabled");
		}
		Result.Packages.Add(MoveTemp(State));
		++Reported;
	}

	Result.bSuccess = true;
	Result.ChangedCount = 0;
	Result.Message = FString::Printf(
		TEXT("get_package_status: %d of %d package(s)%s; source control %s (provider '%s')%s (read-only)"),
		Result.Packages.Num(), TotalPackages,
		bDirtyOnly ? TEXT(" (dirty only)") : TEXT(""),
		Result.SourceControl.bAvailable
			? TEXT("available")
			: (Result.SourceControl.bEnabled ? TEXT("enabled but unavailable") : TEXT("disabled")),
		*Result.SourceControl.ProviderName,
		Result.bPackagesTruncated ? TEXT(" (truncated at cap)") : TEXT(""));
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
