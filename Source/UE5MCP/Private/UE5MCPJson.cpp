// Copyright Epic Games, Inc. All Rights Reserved.

#include "UE5MCPJson.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UE5MCPToolRegistry.h"

namespace
{
	FString JsonObjectToString(const TSharedRef<FJsonObject>& JsonObject)
	{
		FString Output;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
		FJsonSerializer::Serialize(JsonObject, Writer);
		return Output;
	}

	/** Strict 3-number array parse. Rejects wrong length, and string/bool elements
	 *  (only EJson::Number is accepted) so a transform component is never silently
	 *  coerced from text. */
	bool TryParseVector3(const TSharedPtr<FJsonValue>& Value, FVector& OutVector)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!Value.IsValid() || !Value->TryGetArray(Array) || Array->Num() != 3)
		{
			return false;
		}
		double Components[3] = { 0.0, 0.0, 0.0 };
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const TSharedPtr<FJsonValue>& Element = (*Array)[Index];
			if (!Element.IsValid() || Element->Type != EJson::Number || !Element->TryGetNumber(Components[Index]))
			{
				return false;
			}
		}
		OutVector = FVector(Components[0], Components[1], Components[2]);
		return true;
	}

	TSharedPtr<FJsonValue> VectorToJson(const FVector& Vector)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		Values.Add(MakeShared<FJsonValueNumber>(Vector.X));
		Values.Add(MakeShared<FJsonValueNumber>(Vector.Y));
		Values.Add(MakeShared<FJsonValueNumber>(Vector.Z));
		return MakeShared<FJsonValueArray>(Values);
	}

	TSharedRef<FJsonObject> ActorSummaryToJson(const FUE5MCPActorSummary& Summary)
	{
		TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
		JsonObject->SetStringField(TEXT("path"), Summary.ActorPath);
		JsonObject->SetStringField(TEXT("label"), Summary.Label);
		JsonObject->SetStringField(TEXT("class_path"), Summary.ClassPath);
		TArray<TSharedPtr<FJsonValue>> TagValues;
		for (const FName& Tag : Summary.Tags)
		{
			TagValues.Add(MakeShared<FJsonValueString>(Tag.ToString()));
		}
		JsonObject->SetArrayField(TEXT("tags"), TagValues);
		JsonObject->SetStringField(TEXT("folder"), Summary.FolderPath.IsNone() ? FString() : Summary.FolderPath.ToString());
		JsonObject->SetBoolField(TEXT("selected"), Summary.bSelected);

		TSharedRef<FJsonObject> TransformObject = MakeShared<FJsonObject>();
		TransformObject->SetField(TEXT("location"), VectorToJson(Summary.Transform.GetLocation()));
		TransformObject->SetField(TEXT("rotation"), VectorToJson(Summary.Transform.GetRotation().Euler()));
		TransformObject->SetField(TEXT("scale"), VectorToJson(Summary.Transform.GetScale3D()));
		JsonObject->SetObjectField(TEXT("transform"), TransformObject);
		return JsonObject;
	}
}

bool UE5MCPJson::ParsePlanRequest(const FString& Body, FUE5MCPPlanRequest& OutRequest, TArray<FString>& OutErrors)
{
	OutRequest = FUE5MCPPlanRequest();

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutErrors.Add(TEXT("body is not a valid JSON object"));
		return false;
	}

	double SchemaVersion = 0.0;
	if (Root->TryGetNumberField(TEXT("schema_version"), SchemaVersion))
	{
		OutRequest.SchemaVersion = static_cast<int32>(SchemaVersion);
	}
	Root->TryGetStringField(TEXT("summary"), OutRequest.Summary);
	Root->TryGetBoolField(TEXT("requires_approval"), OutRequest.bRequiresApproval);
	Root->TryGetBoolField(TEXT("requires_second_confirmation"), OutRequest.bRequiresSecondConfirmation);

	const TSharedPtr<FJsonObject>* FingerprintObject = nullptr;
	if (Root->TryGetObjectField(TEXT("context_fingerprint"), FingerprintObject))
	{
		OutRequest.bHasContextFingerprint = true;
		(*FingerprintObject)->TryGetStringField(TEXT("scene"), OutRequest.Fingerprint.SceneName);
		const TArray<TSharedPtr<FJsonValue>>* SelectedPaths = nullptr;
		if ((*FingerprintObject)->TryGetArrayField(TEXT("selected_object_paths"), SelectedPaths))
		{
			for (const TSharedPtr<FJsonValue>& Value : *SelectedPaths)
			{
				FString Path;
				if (Value->TryGetString(Path))
				{
					OutRequest.Fingerprint.SelectedActorPaths.Add(Path);
				}
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* Actions = nullptr;
	if (!Root->TryGetArrayField(TEXT("actions"), Actions))
	{
		OutErrors.Add(TEXT("plan has no actions array"));
		return false;
	}

	for (int32 Index = 0; Index < Actions->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* ActionObject = nullptr;
		if (!(*Actions)[Index]->TryGetObject(ActionObject))
		{
			OutErrors.Add(FString::Printf(TEXT("actions[%d] is not an object"), Index));
			continue;
		}

		FUE5MCPActionRequest ActionRequest;
		(*ActionObject)->TryGetStringField(TEXT("id"), ActionRequest.Id);
		(*ActionObject)->TryGetStringField(TEXT("tool"), ActionRequest.ToolName);
		(*ActionObject)->TryGetStringField(TEXT("risk"), ActionRequest.RiskString);

		const TArray<TSharedPtr<FJsonValue>>* Targets = nullptr;
		if ((*ActionObject)->TryGetArrayField(TEXT("targets"), Targets))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Targets)
			{
				FString Path;
				if (Value->TryGetString(Path) && !Path.IsEmpty())
				{
					ActionRequest.TargetPaths.Add(Path);
				}
				else
				{
					OutErrors.Add(FString::Printf(TEXT("actions[%d] has a non-string or empty target"), Index));
				}
			}
		}

		const TSharedPtr<FJsonObject>* Params = nullptr;
		if ((*ActionObject)->TryGetObjectField(TEXT("params"), Params))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Param : (*Params)->Values)
			{
				ActionRequest.ProvidedParamKeys.Add(Param.Key);

				const FString& Key = Param.Key;
				const TSharedPtr<FJsonValue>& Value = Param.Value;
				FString StringValue;
				double NumberValue = 0.0;
				bool BoolValue = false;
				FVector VectorValue = FVector::ZeroVector;

				if (Key == TEXT("folder_path") && Value->TryGetString(StringValue) && !StringValue.TrimStartAndEnd().IsEmpty())
				{
					FString Normalized = StringValue.TrimStartAndEnd();
					Normalized.ReplaceInline(TEXT("\\"), TEXT("/"));
					ActionRequest.FolderPath = FName(*Normalized);
				}
				else if (Key == TEXT("label") && Value->TryGetString(StringValue))
				{
					// Trimmed display label for set_actor_label; the validator refuses an
					// empty/whitespace label as a no-op.
					ActionRequest.NewLabel = StringValue.TrimStartAndEnd();
				}
				else if (Key == TEXT("tags"))
				{
					// add/remove_actor_tags: a non-empty array of non-empty tag strings.
					// Drop empty/whitespace entries with an error; the validator refuses
					// an action that ends up with no tags.
					const TArray<TSharedPtr<FJsonValue>>* TagValues = nullptr;
					if (!Value->TryGetArray(TagValues))
					{
						OutErrors.Add(FString::Printf(TEXT("actions[%d] param 'tags' must be an array of strings"), Index));
					}
					else
					{
						for (const TSharedPtr<FJsonValue>& TagValue : *TagValues)
						{
							FString Tag;
							if (TagValue->TryGetString(Tag) && !Tag.TrimStartAndEnd().IsEmpty())
							{
								ActionRequest.Tags.AddUnique(FName(*Tag.TrimStartAndEnd()));
							}
							else
							{
								OutErrors.Add(FString::Printf(TEXT("actions[%d] has a non-string or empty tag"), Index));
							}
						}
					}
				}
				else if (Key == TEXT("location"))
				{
					if (TryParseVector3(Value, VectorValue))
					{
						ActionRequest.Transform.bHasLocation = true;
						ActionRequest.Transform.Location = VectorValue;
					}
					else
					{
						OutErrors.Add(FString::Printf(TEXT("actions[%d] param 'location' must be an array of 3 numbers"), Index));
					}
				}
				else if (Key == TEXT("rotation"))
				{
					if (TryParseVector3(Value, VectorValue))
					{
						// Euler degrees [roll, pitch, yaw] — the same convention the
						// context pack reports via FQuat::Euler().
						ActionRequest.Transform.bHasRotation = true;
						ActionRequest.Transform.Rotation = FRotator::MakeFromEuler(VectorValue);
					}
					else
					{
						OutErrors.Add(FString::Printf(TEXT("actions[%d] param 'rotation' must be an array of 3 numbers"), Index));
					}
				}
				else if (Key == TEXT("scale"))
				{
					if (TryParseVector3(Value, VectorValue))
					{
						ActionRequest.Transform.bHasScale = true;
						ActionRequest.Transform.Scale = VectorValue;
					}
					else
					{
						OutErrors.Add(FString::Printf(TEXT("actions[%d] param 'scale' must be an array of 3 numbers"), Index));
					}
				}
				else if (Key == TEXT("class_path") && Value->TryGetString(StringValue))
				{
					// class_path doubles as the find_actors filter and the spawn class;
					// the validator/executor pick the right field by action type.
					ActionRequest.FindQuery.ClassPath = StringValue;
					ActionRequest.SpawnClassPath = StringValue;
				}
				else if (Key == TEXT("offset"))
				{
					if (TryParseVector3(Value, VectorValue))
					{
						ActionRequest.bHasDuplicateOffset = true;
						ActionRequest.DuplicateOffset = VectorValue;
					}
					else
					{
						OutErrors.Add(FString::Printf(TEXT("actions[%d] param 'offset' must be an array of 3 numbers"), Index));
					}
				}
				else if (Key == TEXT("static_mesh") && Value->TryGetString(StringValue))
				{
					ActionRequest.SpawnMeshPath = StringValue.TrimStartAndEnd();
				}
				else if (Key == TEXT("label_base") && Value->TryGetString(StringValue))
				{
					ActionRequest.SpawnLabelBase = StringValue.TrimStartAndEnd();
				}
				else if (Key == TEXT("transforms"))
				{
					const TArray<TSharedPtr<FJsonValue>>* Instances = nullptr;
					if (!Value->TryGetArray(Instances))
					{
						OutErrors.Add(FString::Printf(TEXT("actions[%d] param 'transforms' must be an array of objects"), Index));
					}
					else
					{
						for (int32 InstanceIndex = 0; InstanceIndex < Instances->Num(); ++InstanceIndex)
						{
							const TSharedPtr<FJsonObject>* InstanceObject = nullptr;
							if (!(*Instances)[InstanceIndex]->TryGetObject(InstanceObject))
							{
								OutErrors.Add(FString::Printf(TEXT("actions[%d] transforms[%d] is not an object"), Index, InstanceIndex));
								continue;
							}

							FUE5MCPSpawnInstance Instance;
							FVector InstanceVector = FVector::ZeroVector;
							// Location is mandatory per spawn instance; rotation/scale are optional.
							if (!TryParseVector3((*InstanceObject)->TryGetField(TEXT("location")), InstanceVector))
							{
								OutErrors.Add(FString::Printf(TEXT("actions[%d] transforms[%d] needs 'location' as an array of 3 numbers"), Index, InstanceIndex));
								continue;
							}
							Instance.Location = InstanceVector;
							if (const TSharedPtr<FJsonValue> RotationValue = (*InstanceObject)->TryGetField(TEXT("rotation")))
							{
								if (TryParseVector3(RotationValue, InstanceVector))
								{
									Instance.Rotation = FRotator::MakeFromEuler(InstanceVector);
								}
								else
								{
									OutErrors.Add(FString::Printf(TEXT("actions[%d] transforms[%d] 'rotation' must be an array of 3 numbers"), Index, InstanceIndex));
									continue;
								}
							}
							if (const TSharedPtr<FJsonValue> ScaleValue = (*InstanceObject)->TryGetField(TEXT("scale")))
							{
								if (TryParseVector3(ScaleValue, InstanceVector))
								{
									Instance.Scale = InstanceVector;
								}
								else
								{
									OutErrors.Add(FString::Printf(TEXT("actions[%d] transforms[%d] 'scale' must be an array of 3 numbers"), Index, InstanceIndex));
									continue;
								}
							}
							ActionRequest.SpawnInstances.Add(Instance);
						}
					}
				}
				else if (Key == TEXT("label_contains") && Value->TryGetString(StringValue))
				{
					ActionRequest.FindQuery.LabelContains = StringValue;
				}
				else if (Key == TEXT("tag") && Value->TryGetString(StringValue))
				{
					ActionRequest.FindQuery.Tag = FName(*StringValue);
				}
				else if (Key == TEXT("folder") && Value->TryGetString(StringValue))
				{
					// find_actors folder filter arrives as folder_path too; both accepted.
					ActionRequest.FindQuery.FolderPath = FName(*StringValue);
				}
				else if (Key == TEXT("selected_only") && Value->TryGetBool(BoolValue))
				{
					ActionRequest.FindQuery.bSelectedOnly = BoolValue;
				}
				else if ((Key == TEXT("max_results") || Key == TEXT("max_objects")) && Value->TryGetNumber(NumberValue))
				{
					ActionRequest.FindQuery.MaxResults = static_cast<int32>(NumberValue);
				}
				else if (Key == TEXT("max_lines") && Value->TryGetNumber(NumberValue))
				{
					ActionRequest.ReadLogsQuery.MaxLines = static_cast<int32>(NumberValue);
				}
				else if (Key == TEXT("contains") && Value->TryGetString(StringValue))
				{
					ActionRequest.ReadLogsQuery.Contains = StringValue;
				}
				else if (Key == TEXT("property") && Value->TryGetString(StringValue))
				{
					ActionRequest.PropertyName = StringValue.TrimStartAndEnd();
				}
				else if (Key == TEXT("component") && Value->TryGetString(StringValue))
				{
					ActionRequest.PropertyComponentClass = StringValue.TrimStartAndEnd();
				}
				else if (Key == TEXT("component_name") && Value->TryGetString(StringValue))
				{
					ActionRequest.PropertyComponentName = StringValue.TrimStartAndEnd();
				}
				else if (Key == TEXT("package_name") && Value->TryGetString(StringValue))
				{
					ActionRequest.PackageName = StringValue.TrimStartAndEnd();
				}
				else if (Key == TEXT("value"))
				{
					// Polymorphic, tagged by JSON kind. The validator checks this kind
					// against the allowlisted property type; nothing is coerced silently.
					FUE5MCPPropertyValue& PropValue = ActionRequest.PropertyValue;
					double Scalar = 0.0;
					bool Flag = false;
					FString Text;
					const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
					if (Value->Type == EJson::Number && Value->TryGetNumber(Scalar))
					{
						PropValue.Kind = FUE5MCPPropertyValue::EKind::Number;
						PropValue.Number = Scalar;
					}
					else if (Value->Type == EJson::Boolean && Value->TryGetBool(Flag))
					{
						PropValue.Kind = FUE5MCPPropertyValue::EKind::Bool;
						PropValue.Bool = Flag;
					}
					else if (Value->Type == EJson::String && Value->TryGetString(Text))
					{
						PropValue.Kind = FUE5MCPPropertyValue::EKind::Name;
						PropValue.Name = Text;
					}
					else if (Value->TryGetArray(Arr) && (Arr->Num() == 3 || Arr->Num() == 4))
					{
						double Components[4] = { 0.0, 0.0, 0.0, 1.0 };
						bool bAllNumbers = true;
						for (int32 CompIndex = 0; CompIndex < Arr->Num(); ++CompIndex)
						{
							const TSharedPtr<FJsonValue>& Element = (*Arr)[CompIndex];
							if (!Element.IsValid() || Element->Type != EJson::Number || !Element->TryGetNumber(Components[CompIndex]))
							{
								bAllNumbers = false;
								break;
							}
						}
						if (!bAllNumbers)
						{
							OutErrors.Add(FString::Printf(TEXT("actions[%d] param 'value' array must contain only numbers"), Index));
						}
						else if (Arr->Num() == 3)
						{
							PropValue.Kind = FUE5MCPPropertyValue::EKind::Vector;
							PropValue.Vector = FVector(Components[0], Components[1], Components[2]);
						}
						else
						{
							PropValue.Kind = FUE5MCPPropertyValue::EKind::Color;
							PropValue.Color = FLinearColor(Components[0], Components[1], Components[2], Components[3]);
						}
					}
					else
					{
						OutErrors.Add(FString::Printf(TEXT("actions[%d] param 'value' must be a number, bool, string, or array of 3 (vector) or 4 (rgba) numbers"), Index));
					}
				}
				else if (Key == TEXT("max_packages") && Value->TryGetNumber(NumberValue))
				{
					ActionRequest.PackageQuery.MaxPackages = static_cast<int32>(NumberValue);
				}
				else if (Key == TEXT("dirty_only") && Value->TryGetBool(BoolValue))
				{
					ActionRequest.PackageQuery.bDirtyOnly = BoolValue;
				}
				else if (Key == TEXT("editable_only") && Value->TryGetBool(BoolValue))
				{
					ActionRequest.GetPropertiesQuery.bEditableOnly = BoolValue;
				}
				else if (Key == TEXT("allowlisted_only") && Value->TryGetBool(BoolValue))
				{
					ActionRequest.GetPropertiesQuery.bAllowlistedOnly = BoolValue;
				}
				else if (Key == TEXT("max_properties") && Value->TryGetNumber(NumberValue))
				{
					ActionRequest.GetPropertiesQuery.MaxProperties = static_cast<int32>(NumberValue);
				}
				else if (Key == TEXT("max_components") && Value->TryGetNumber(NumberValue))
				{
					ActionRequest.GetComponentsQuery.MaxComponents = static_cast<int32>(NumberValue);
				}
			}

			// find_actors uses folder_path as its folder filter as well.
			if (ActionRequest.ToolName == TEXT("find_actors") && !ActionRequest.FolderPath.IsNone())
			{
				ActionRequest.FindQuery.FolderPath = ActionRequest.FolderPath;
			}
		}

		OutRequest.Actions.Add(MoveTemp(ActionRequest));
	}

	return OutErrors.IsEmpty();
}

FString UE5MCPJson::SerializeContextPack(const FUE5MCPContextPack& Context)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema_version"), 1);
	Root->SetStringField(TEXT("world"), Context.WorldName);

	TArray<TSharedPtr<FJsonValue>> Warnings;
	for (const FString& Warning : Context.Warnings)
	{
		Warnings.Add(MakeShared<FJsonValueString>(Warning));
	}
	Root->SetArrayField(TEXT("warnings"), Warnings);

	TArray<TSharedPtr<FJsonValue>> Selected;
	for (const FUE5MCPActorSummary& Summary : Context.SelectedActors)
	{
		Selected.Add(MakeShared<FJsonValueObject>(ActorSummaryToJson(Summary)));
	}
	Root->SetArrayField(TEXT("selected_actors"), Selected);

	TArray<TSharedPtr<FJsonValue>> Loaded;
	for (const FUE5MCPActorSummary& Summary : Context.LoadedActorsCapped)
	{
		Loaded.Add(MakeShared<FJsonValueObject>(ActorSummaryToJson(Summary)));
	}
	Root->SetArrayField(TEXT("loaded_actors"), Loaded);

	return JsonObjectToString(Root);
}

FString UE5MCPJson::SerializeExecutionResult(const FUE5MCPExecutionResult& Result)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("executed"), Result.bSuccess);

	TArray<TSharedPtr<FJsonValue>> ActionResults;
	for (const FUE5MCPActionResult& ActionResult : Result.ActionResults)
	{
		TSharedRef<FJsonObject> ResultObject = MakeShared<FJsonObject>();
		ResultObject->SetStringField(TEXT("id"), ActionResult.ActionId);
		ResultObject->SetStringField(TEXT("status"), ActionResult.bSuccess ? TEXT("succeeded") : TEXT("failed"));
		ResultObject->SetStringField(TEXT("message"), ActionResult.Message);
		ResultObject->SetNumberField(TEXT("changed_count"), ActionResult.ChangedCount);
		if (!ActionResult.RefusalCode.IsEmpty())
		{
			ResultObject->SetStringField(TEXT("refusal_code"), ActionResult.RefusalCode);
		}
		if (!ActionResult.FoundActors.IsEmpty())
		{
			TArray<TSharedPtr<FJsonValue>> Found;
			for (const FUE5MCPActorSummary& Summary : ActionResult.FoundActors)
			{
				Found.Add(MakeShared<FJsonValueObject>(ActorSummaryToJson(Summary)));
			}
			ResultObject->SetArrayField(TEXT("found_actors"), Found);
		}
		if (!ActionResult.LogLines.IsEmpty())
		{
			TArray<TSharedPtr<FJsonValue>> Lines;
			for (const FString& Line : ActionResult.LogLines)
			{
				Lines.Add(MakeShared<FJsonValueString>(Line));
			}
			ResultObject->SetArrayField(TEXT("log_lines"), Lines);
		}
		if (ActionResult.bHasPackageStatus)
		{
			TSharedRef<FJsonObject> ScObject = MakeShared<FJsonObject>();
			ScObject->SetBoolField(TEXT("enabled"), ActionResult.SourceControl.bEnabled);
			ScObject->SetBoolField(TEXT("available"), ActionResult.SourceControl.bAvailable);
			ScObject->SetStringField(TEXT("provider"), ActionResult.SourceControl.ProviderName);
			ResultObject->SetObjectField(TEXT("source_control"), ScObject);

			TArray<TSharedPtr<FJsonValue>> PackageValues;
			for (const FUE5MCPPackageState& Package : ActionResult.Packages)
			{
				TSharedRef<FJsonObject> PackageObject = MakeShared<FJsonObject>();
				PackageObject->SetStringField(TEXT("name"), Package.PackageName);
				PackageObject->SetStringField(TEXT("filename"), Package.Filename);
				PackageObject->SetBoolField(TEXT("dirty"), Package.bDirty);
				PackageObject->SetStringField(TEXT("source_control_state"), Package.SourceControlState);
				PackageValues.Add(MakeShared<FJsonValueObject>(PackageObject));
			}
			ResultObject->SetArrayField(TEXT("packages"), PackageValues);
			ResultObject->SetBoolField(TEXT("packages_truncated"), ActionResult.bPackagesTruncated);
		}
		if (ActionResult.bHasProperties)
		{
			ResultObject->SetStringField(TEXT("inspected_owner_class"), ActionResult.InspectedOwnerClass);
			TArray<TSharedPtr<FJsonValue>> PropertyValues;
			for (const FUE5MCPPropertySummary& Property : ActionResult.Properties)
			{
				TSharedRef<FJsonObject> PropObject = MakeShared<FJsonObject>();
				PropObject->SetStringField(TEXT("name"), Property.Name);
				PropObject->SetStringField(TEXT("cpp_type"), Property.CppType);
				PropObject->SetStringField(TEXT("current_value"), Property.CurrentValue);
				PropObject->SetBoolField(TEXT("editable"), Property.bEditable);
				PropObject->SetBoolField(TEXT("differs_from_default"), Property.bDiffersFromDefault);
				PropObject->SetBoolField(TEXT("allowlisted"), Property.bAllowlisted);
				if (!Property.AllowedType.IsEmpty())
				{
					PropObject->SetStringField(TEXT("allowed_type"), Property.AllowedType);
				}
				if (Property.bHasRange)
				{
					PropObject->SetNumberField(TEXT("range_min"), Property.RangeMin);
					PropObject->SetNumberField(TEXT("range_max"), Property.RangeMax);
				}
				if (Property.bHasSuggestedRange)
				{
					PropObject->SetNumberField(TEXT("suggested_min"), Property.SuggestedMin);
					PropObject->SetNumberField(TEXT("suggested_max"), Property.SuggestedMax);
				}
				PropertyValues.Add(MakeShared<FJsonValueObject>(PropObject));
			}
			ResultObject->SetArrayField(TEXT("properties"), PropertyValues);
			ResultObject->SetBoolField(TEXT("properties_truncated"), ActionResult.bPropertiesTruncated);
		}
		if (ActionResult.bHasComponents)
		{
			ResultObject->SetStringField(TEXT("inspected_owner_class"), ActionResult.InspectedOwnerClass);
			TArray<TSharedPtr<FJsonValue>> ComponentValues;
			for (const FUE5MCPComponentSummary& Component : ActionResult.Components)
			{
				TSharedRef<FJsonObject> CompObject = MakeShared<FJsonObject>();
				CompObject->SetStringField(TEXT("name"), Component.Name);
				CompObject->SetStringField(TEXT("class_path"), Component.ClassPath);
				CompObject->SetStringField(TEXT("creation_method"), Component.CreationMethod);
				CompObject->SetBoolField(TEXT("editable_instance"), Component.bEditableInstance);
				if (!Component.AttachParent.IsEmpty())
				{
					CompObject->SetStringField(TEXT("attach_parent"), Component.AttachParent);
				}
				TArray<TSharedPtr<FJsonValue>> TagValues;
				for (const FName& Tag : Component.ComponentTags)
				{
					TagValues.Add(MakeShared<FJsonValueString>(Tag.ToString()));
				}
				CompObject->SetArrayField(TEXT("component_tags"), TagValues);
				ComponentValues.Add(MakeShared<FJsonValueObject>(CompObject));
			}
			ResultObject->SetArrayField(TEXT("components"), ComponentValues);
			ResultObject->SetBoolField(TEXT("components_truncated"), ActionResult.bComponentsTruncated);
		}
		if (ActionResult.bHasCapabilities)
		{
			const FUE5MCPCapabilities& Caps = ActionResult.Capabilities;
			TSharedRef<FJsonObject> CapsObject = MakeShared<FJsonObject>();
			CapsObject->SetNumberField(TEXT("plan_schema_version"), Caps.PlanSchemaVersion);

			TArray<TSharedPtr<FJsonValue>> ToolValues;
			for (const FUE5MCPToolCapability& Tool : Caps.Tools)
			{
				TSharedRef<FJsonObject> ToolObject = MakeShared<FJsonObject>();
				ToolObject->SetStringField(TEXT("name"), Tool.Name);
				ToolObject->SetStringField(TEXT("risk"), Tool.Risk);
				ToolObject->SetBoolField(TEXT("requires_targets"), Tool.bRequiresTargets);
				ToolObject->SetBoolField(TEXT("accepts_targets"), Tool.bAcceptsTargets);
				TArray<TSharedPtr<FJsonValue>> ParamValues;
				for (const FString& Param : Tool.Params)
				{
					ParamValues.Add(MakeShared<FJsonValueString>(Param));
				}
				ToolObject->SetArrayField(TEXT("params"), ParamValues);
				ToolValues.Add(MakeShared<FJsonValueObject>(ToolObject));
			}
			CapsObject->SetArrayField(TEXT("tools"), ToolValues);

			auto StringArray = [](const TArray<FString>& In)
			{
				TArray<TSharedPtr<FJsonValue>> Out;
				for (const FString& S : In) { Out.Add(MakeShared<FJsonValueString>(S)); }
				return Out;
			};
			CapsObject->SetArrayField(TEXT("spawn_class_allowlist"), StringArray(Caps.SpawnClassAllowlist));
			CapsObject->SetArrayField(TEXT("spawn_mesh_allowlist"), StringArray(Caps.SpawnMeshAllowlist));

			TArray<TSharedPtr<FJsonValue>> PropValues;
			for (const FUE5MCPPropertyPolicySummary& Entry : Caps.PropertyAllowlist)
			{
				TSharedRef<FJsonObject> EntryObject = MakeShared<FJsonObject>();
				EntryObject->SetStringField(TEXT("class_path"), Entry.ClassPath);
				EntryObject->SetStringField(TEXT("property"), Entry.PropertyName);
				EntryObject->SetStringField(TEXT("type"), Entry.Type);
				if (Entry.bHasRange)
				{
					EntryObject->SetNumberField(TEXT("range_min"), Entry.Min);
					EntryObject->SetNumberField(TEXT("range_max"), Entry.Max);
				}
				if (!Entry.AssetClass.IsEmpty()) { EntryObject->SetStringField(TEXT("asset_class"), Entry.AssetClass); }
				if (!Entry.OverrideFlag.IsEmpty()) { EntryObject->SetStringField(TEXT("override_flag"), Entry.OverrideFlag); }
				PropValues.Add(MakeShared<FJsonValueObject>(EntryObject));
			}
			CapsObject->SetArrayField(TEXT("property_allowlist"), PropValues);

			TSharedRef<FJsonObject> PolicyObject = MakeShared<FJsonObject>();
			PolicyObject->SetBoolField(TEXT("block_mutations_to_unwritable_packages"), Caps.bBlockMutationsToUnwritablePackages);
			PolicyObject->SetBoolField(TEXT("allow_external_session_approval"), Caps.bAllowExternalSessionApproval);
			PolicyObject->SetBoolField(TEXT("require_in_editor_confirm_for_destructive"), Caps.bRequireInEditorConfirmForDestructive);
			PolicyObject->SetNumberField(TEXT("max_context_actors"), Caps.MaxContextActors);
			CapsObject->SetObjectField(TEXT("policy"), PolicyObject);

			ResultObject->SetObjectField(TEXT("capabilities"), CapsObject);
		}
		ActionResults.Add(MakeShared<FJsonValueObject>(ResultObject));
	}
	Root->SetArrayField(TEXT("action_results"), ActionResults);

	TArray<TSharedPtr<FJsonValue>> LogLines;
	for (const FString& Line : Result.UserVisibleLogLines)
	{
		LogLines.Add(MakeShared<FJsonValueString>(Line));
	}
	Root->SetArrayField(TEXT("log"), LogLines);

	return JsonObjectToString(Root);
}

FString UE5MCPJson::PlanStatusToString(EUE5MCPPlanStatus Status)
{
	switch (Status)
	{
	case EUE5MCPPlanStatus::PendingApproval: return TEXT("pending_approval");
	case EUE5MCPPlanStatus::Invalid: return TEXT("invalid");
	case EUE5MCPPlanStatus::Executed: return TEXT("executed");
	case EUE5MCPPlanStatus::Failed: return TEXT("failed");
	case EUE5MCPPlanStatus::RefusedStale: return TEXT("refused_stale");
	case EUE5MCPPlanStatus::Superseded: return TEXT("superseded");
	case EUE5MCPPlanStatus::PreviewedOnly: return TEXT("previewed");
	}
	return TEXT("unknown");
}

FString UE5MCPJson::SerializePlanRecord(const FUE5MCPPlanRecord& Record)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema_version"), 1);
	Root->SetStringField(TEXT("plan_id"), Record.PlanId);
	Root->SetStringField(TEXT("status"), PlanStatusToString(Record.Status));
	if (!Record.RefusalCode.IsEmpty())
	{
		Root->SetStringField(TEXT("refusal_code"), Record.RefusalCode);
	}
	Root->SetStringField(TEXT("summary"), Record.Plan.Summary);
	Root->SetBoolField(TEXT("requires_approval"), Record.Plan.bRequiresApproval);
	Root->SetStringField(TEXT("source"), Record.Source == EUE5MCPPlanSource::Bridge ? TEXT("bridge") : TEXT("panel"));
	Root->SetStringField(TEXT("approval_mode"),
		Record.Status == EUE5MCPPlanStatus::PreviewedOnly
			? TEXT("preview")
			: (Record.bExternalSessionApproval ? TEXT("external_session") : TEXT("panel")));

	if (!Record.Plan.Warnings.IsEmpty())
	{
		TArray<TSharedPtr<FJsonValue>> Problems;
		for (const FString& Warning : Record.Plan.Warnings)
		{
			Problems.Add(MakeShared<FJsonValueString>(Warning));
		}
		Root->SetArrayField(TEXT("problems"), Problems);
	}

	TArray<TSharedPtr<FJsonValue>> Preview;
	for (const FUE5MCPResolvedAction& Resolved : Record.Plan.Actions)
	{
		TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("id"), Resolved.Action.Id);
		const FUE5MCPToolDescriptor* Tool = FUE5MCPToolRegistry::FindByType(Resolved.Action.Type);
		Row->SetStringField(TEXT("tool"), Tool ? Tool->ToolName : TEXT("unknown"));
		Row->SetStringField(TEXT("risk"), FUE5MCPToolRegistry::RiskToString(Resolved.Action.Risk));
		Row->SetStringField(TEXT("preview_text"), Resolved.PreviewText);
		TArray<TSharedPtr<FJsonValue>> Labels;
		for (const FString& Label : Resolved.TargetLabels)
		{
			Labels.Add(MakeShared<FJsonValueString>(Label));
		}
		Row->SetArrayField(TEXT("target_labels"), Labels);
		Preview.Add(MakeShared<FJsonValueObject>(Row));
	}
	Root->SetArrayField(TEXT("preview"), Preview);

	if (Record.Status == EUE5MCPPlanStatus::Executed || Record.Status == EUE5MCPPlanStatus::Failed)
	{
		TSharedPtr<FJsonObject> ResultObject;
		const TSharedRef<TJsonReader<TCHAR>> Reader =
			TJsonReaderFactory<TCHAR>::Create(SerializeExecutionResult(Record.Result));
		if (FJsonSerializer::Deserialize(Reader, ResultObject) && ResultObject.IsValid())
		{
			Root->SetObjectField(TEXT("result"), ResultObject);
		}
	}

	return JsonObjectToString(Root);
}

FString UE5MCPJson::SerializeError(const FString& MachineCode, const FString& Message)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("error"), MachineCode);
	Root->SetStringField(TEXT("message"), Message);
	return JsonObjectToString(Root);
}
