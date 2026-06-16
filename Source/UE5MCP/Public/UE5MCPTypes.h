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
	SelectActors,
	SetActorFolder,
	SetActorLabel,
	AddActorTags,
	RemoveActorTags,
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
	FUE5MCPFindActorsQuery FindQuery;
	FUE5MCPReadLogsQuery ReadLogsQuery;
	FUE5MCPPackageStatusQuery PackageQuery;
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
	FUE5MCPFindActorsQuery FindQuery;
	FUE5MCPReadLogsQuery ReadLogsQuery;
	FUE5MCPPackageStatusQuery PackageQuery;
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
