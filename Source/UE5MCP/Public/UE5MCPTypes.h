// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;

/** Typed allowlist. No script/process/code-execution actions belong here. */
enum class EUE5MCPActionType : uint8
{
	GetSelectionContext,
	FindActors,
	ReadLogs,
	GetPackageStatus,
	GetActorProperties,
	GetActorComponents,
	SelectActors,
	SetActorFolder,
	SetActorLabel,
	AddActorTags,
	RemoveActorTags,
	SetActorProperty,
	SetActorTransform,
	DuplicateActorWithOffset,
	SpawnActorFromClass,
	DeleteActor
};

enum class EUE5MCPRiskLevel : uint8
{
	ReadOnly,
	LowMutation,
	Destructive
};

struct FUE5MCPActorSummary
{
	FString ActorPath;
	FString Label;
	FString ClassPath;
	TArray<FName> Tags;
	FName FolderPath;
	FTransform Transform = FTransform::Identity;
	bool bSelected = false;
	TWeakObjectPtr<AActor> Actor;
};

struct FUE5MCPContextPack
{
	FString WorldName;
	TArray<FUE5MCPActorSummary> SelectedActors;
	TArray<FUE5MCPActorSummary> LoadedActorsCapped;
	TArray<FString> Warnings;
};

/** Bounded query parameters for the find_actors read-only tool. */
struct FUE5MCPFindActorsQuery
{
	FString ClassPath;
	FString LabelContains;
	FName Tag;
	FName FolderPath;
	bool bSelectedOnly = false;
	int32 MaxResults = 100;
};

/** Bounded query parameters for the read_logs read-only diagnostics tool. Returns
 *  the most recent lines from the plugin's structured LogUE5MCP buffer (tool calls,
 *  refusals, errors) so an agent can self-correct without re-running an action. */
struct FUE5MCPReadLogsQuery
{
	/** Cap on returned lines; the executor clamps to [1, FUE5MCPLog::MaxBufferedLines]. */
	int32 MaxLines = 100;
	/** Optional case-insensitive substring filter (e.g. "refused", an action id). */
	FString Contains;
};

/** Bounded query parameters for the get_package_status read-only tool. Reports the
 *  packages a save/mutation would touch (dirty set) plus a source-control summary,
 *  so an agent can see blast radius before mutating. No source-control network call
 *  is issued — per-package state is read from the provider's cache only. */
struct FUE5MCPPackageStatusQuery
{
	/** Cap on returned packages; the executor clamps to [1, MaxPackageStatusResults]. */
	int32 MaxPackages = 100;
	/** When true (default), report only dirty (unsaved) packages; when false, also
	 *  include currently loaded on-disk packages (still capped). */
	bool bDirtyOnly = true;
};

/** Editor-wide source-control summary for get_package_status. */
struct FUE5MCPSourceControlSummary
{
	bool bEnabled = false;
	bool bAvailable = false;
	FString ProviderName;
};

/** Per-package state row for get_package_status. SourceControlState is a stable
 *  machine token (e.g. "checked_out", "not_current", "not_controlled",
 *  "source_control_disabled", "unknown") classified from the cached provider state. */
struct FUE5MCPPackageState
{
	FString PackageName;
	FString Filename;
	bool bDirty = false;
	FString SourceControlState;
};

/** Bounded query parameters for the get_actor_properties read-only discovery tool.
 *  Lists the reflected properties of the first valid target actor (or a uniquely
 *  resolved component of the optional `component` class on it), with current values
 *  and the flags an agent needs to know what set_actor_property will accept — so it
 *  never has to guess a property name and learn from a refusal. The component class
 *  reuses FUE5MCPAction::PropertyComponentClass (the `component` param). */
struct FUE5MCPGetPropertiesQuery
{
	/** When true (default), list only properties the editor considers editable
	 *  (CPF_Edit && !CPF_EditConst && the live object permits editing). */
	bool bEditableOnly = true;
	/** When true (default), list only properties on the PropertyAllowlist for the
	 *  resolved class — i.e. exactly the surface set_actor_property would accept. */
	bool bAllowlistedOnly = true;
	/** Cap on returned properties; the executor clamps to [1, MaxPropertyResults]. */
	int32 MaxProperties = 50;
};

/** One reflected-property row returned by get_actor_properties. `AllowedType`/range
 *  are populated only when the property is on the allowlist; `SuggestedRange` carries
 *  the engine's own declared ClampMin/ClampMax metadata (advisory — a hint for widening
 *  the allowlist, never an implicit grant). */
struct FUE5MCPPropertySummary
{
	FString Name;
	FString CppType;
	FString CurrentValue;
	bool bEditable = false;
	bool bDiffersFromDefault = false;
	bool bAllowlisted = false;
	FString AllowedType;
	bool bHasRange = false;
	double RangeMin = 0.0;
	double RangeMax = 0.0;
	bool bHasSuggestedRange = false;
	double SuggestedMin = 0.0;
	double SuggestedMax = 0.0;
};

/** Bounded query parameters for the get_actor_components read-only discovery tool.
 *  Lists the components of the first valid target actor, so an agent can learn the
 *  component names/classes to address with set_actor_property / get_actor_properties. */
struct FUE5MCPGetComponentsQuery
{
	/** Cap on returned components; the executor clamps to [1, MaxComponentResults]. */
	int32 MaxComponents = 50;
};

/** One component row returned by get_actor_components. CreationMethod is a stable
 *  machine token ("native" / "blueprint_template" / "construction_script" /
 *  "instance"); EditableInstance reflects whether the editor allows per-instance edits. */
struct FUE5MCPComponentSummary
{
	FString Name;
	FString ClassPath;
	FString CreationMethod;
	FString AttachParent;
	TArray<FName> ComponentTags;
	bool bEditableInstance = false;
};

/** A typed value for set_actor_property, tagged by the JSON kind the client sent.
 *  The validator checks this kind against the allowlist entry's declared type; the
 *  executor writes it through reflection only after that match holds. */
struct FUE5MCPPropertyValue
{
	enum class EKind : uint8 { None, Number, Bool, Vector, Color, Name };

	EKind Kind = EKind::None;
	double Number = 0.0;
	bool Bool = false;
	FVector Vector = FVector::ZeroVector;
	/** RGBA, linear [0..1]; converted to FColor (sRGB) when the target property is FColor. */
	FLinearColor Color = FLinearColor::White;
	FString Name;

	bool IsSet() const { return Kind != EKind::None; }
};

/** Optional absolute transform components for set_actor_transform. Each field is
 *  applied only when its bHas* flag is set; unset components are left unchanged.
 *  Rotation is carried as Euler degrees, matching the context pack's rotation output. */
struct FUE5MCPTransformDelta
{
	bool bHasLocation = false;
	bool bHasRotation = false;
	bool bHasScale = false;
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	FVector Scale = FVector::OneVector;

	bool IsEmpty() const { return !bHasLocation && !bHasRotation && !bHasScale; }
};

/** One requested spawn placement. Location is required by the validator;
 *  rotation/scale default to identity when the client omits them. */
struct FUE5MCPSpawnInstance
{
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	FVector Scale = FVector::OneVector;
};

struct FUE5MCPAction
{
	FString Id;
	EUE5MCPActionType Type = EUE5MCPActionType::GetSelectionContext;
	EUE5MCPRiskLevel Risk = EUE5MCPRiskLevel::ReadOnly;
	FName NewFolderPath;
	/** set_actor_label target label (display label; the validator rejects an empty one). */
	FString NewLabel;
	/** add_actor_tags / remove_actor_tags tag set (the validator rejects an empty list). */
	TArray<FName> Tags;
	/** set_actor_property: target property name, optional owning component class path
	 *  (empty = the actor itself), and the typed value. */
	FString PropertyName;
	FString PropertyComponentClass;
	FUE5MCPPropertyValue PropertyValue;
	FUE5MCPFindActorsQuery FindQuery;
	FUE5MCPReadLogsQuery ReadLogsQuery;
	FUE5MCPPackageStatusQuery PackageQuery;
	FUE5MCPGetPropertiesQuery GetPropertiesQuery;
	FUE5MCPGetComponentsQuery GetComponentsQuery;
	FUE5MCPTransformDelta Transform;
	FVector DuplicateOffset = FVector::ZeroVector;
	FString SpawnClassPath;
	TArray<FUE5MCPSpawnInstance> SpawnInstances;
	FString SpawnMeshPath;
	FString SpawnLabelBase;
	TArray<TWeakObjectPtr<AActor>> TargetActors;
};

struct FUE5MCPResolvedAction
{
	FUE5MCPAction Action;
	FString PreviewText;
	TArray<FString> TargetLabels;
	TArray<FString> Warnings;
};

struct FUE5MCPValidatedPlan
{
	FString Summary;
	TArray<FUE5MCPResolvedAction> Actions;
	TArray<FString> Warnings;
	FString ContextWorldName;
	TArray<FString> SelectedActorPathsAtGeneration;
	bool bIsValid = false;
	bool bRequiresApproval = true;
	bool bRequiresSecondConfirmation = false;
};

struct FUE5MCPActionResult
{
	FString ActionId;
	bool bSuccess = false;
	FString Message;
	FString RefusalCode;
	int32 ChangedCount = 0;
	TArray<FUE5MCPActorSummary> FoundActors;
	/** Populated by read_logs only: the matching structured log lines, oldest→newest. */
	TArray<FString> LogLines;
	/** Populated by get_package_status only. */
	bool bHasPackageStatus = false;
	FUE5MCPSourceControlSummary SourceControl;
	TArray<FUE5MCPPackageState> Packages;
	bool bPackagesTruncated = false;
	/** Populated by get_actor_properties only: the inspected owner's class path, the
	 *  reflected-property rows, and whether the list was capped. */
	bool bHasProperties = false;
	FString InspectedOwnerClass;
	TArray<FUE5MCPPropertySummary> Properties;
	bool bPropertiesTruncated = false;
	/** Populated by get_actor_components only: the inspected actor's class path is
	 *  reported via InspectedOwnerClass; these carry the component rows + cap flag. */
	bool bHasComponents = false;
	TArray<FUE5MCPComponentSummary> Components;
	bool bComponentsTruncated = false;
};

struct FUE5MCPExecutionResult
{
	bool bSuccess = false;
	/** Set when the executor refused the whole plan before running anything
	 *  (e.g. play_mode_active); ActionResults is empty in that case. */
	FString UpfrontRefusalCode;
	TArray<FUE5MCPActionResult> ActionResults;
	TArray<FString> UserVisibleLogLines;
};

enum class EUE5MCPPlanSource : uint8
{
	Panel,
	Bridge
};

enum class EUE5MCPPlanStatus : uint8
{
	PendingApproval,
	Invalid,
	Executed,
	Failed,
	RefusedStale,
	Superseded,
	/** Validated and previewed only — never executed, never occupies the approval slot. */
	PreviewedOnly
};

/** Full lifecycle record for one submitted plan. Records are kept (FIFO-capped)
 *  so external clients can poll terminal states after the human decides. */
struct FUE5MCPPlanRecord
{
	FString PlanId;
	EUE5MCPPlanSource Source = EUE5MCPPlanSource::Panel;
	EUE5MCPPlanStatus Status = EUE5MCPPlanStatus::Invalid;
	FString RefusalCode;
	FUE5MCPValidatedPlan Plan;
	FUE5MCPExecutionResult Result;
	FDateTime CreatedUtc;
	/** True when the human approval for this plan happened in the external agent
	 *  session (the MCP client's native tool-permission prompt) rather than as an
	 *  in-editor panel click. Audit trail for every externally approved mutation. */
	bool bExternalSessionApproval = false;
};

/** Scene identity + exact selected set captured when a preview is generated;
 *  re-checked at approval time (the stale-preview guard). */
struct FUE5MCPContextFingerprint
{
	FString SceneName;
	TArray<FString> SelectedActorPaths;
};

/** One action as requested by a client (panel or bridge), before validation.
 *  ProvidedParamKeys records every param key the client sent so the validator
 *  can reject unknown params even though parsing is typed. */
struct FUE5MCPActionRequest
{
	FString Id;
	FString ToolName;
	FString RiskString;
	TArray<FString> TargetPaths;
	FName FolderPath;
	FString NewLabel;
	TArray<FName> Tags;
	FString PropertyName;
	FString PropertyComponentClass;
	FUE5MCPPropertyValue PropertyValue;
	FUE5MCPFindActorsQuery FindQuery;
	FUE5MCPReadLogsQuery ReadLogsQuery;
	FUE5MCPPackageStatusQuery PackageQuery;
	FUE5MCPGetPropertiesQuery GetPropertiesQuery;
	FUE5MCPGetComponentsQuery GetComponentsQuery;
	FUE5MCPTransformDelta Transform;
	bool bHasDuplicateOffset = false;
	FVector DuplicateOffset = FVector::ZeroVector;
	FString SpawnClassPath;
	TArray<FUE5MCPSpawnInstance> SpawnInstances;
	FString SpawnMeshPath;
	FString SpawnLabelBase;
	TArray<FString> ProvidedParamKeys;
};

/** A full plan envelope as requested by a client, before validation. */
struct FUE5MCPPlanRequest
{
	int32 SchemaVersion = 0;
	FString Summary;
	bool bRequiresApproval = false;
	bool bRequiresSecondConfirmation = false;
	bool bHasContextFingerprint = false;
	FUE5MCPContextFingerprint Fingerprint;
	TArray<FUE5MCPActionRequest> Actions;
};
