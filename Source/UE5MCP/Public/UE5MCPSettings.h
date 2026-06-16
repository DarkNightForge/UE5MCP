// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UE5MCPSettings.generated.h"

/** One allowlisted property the `set_actor_property` tool may write. A property is
 *  writable ONLY if a matching entry exists here — there is no implicit/arbitrary
 *  property write. The owning class may be an actor class or a component class;
 *  the target object must be (or, for a component class, contain) that class. */
USTRUCT()
struct FUE5MCPPropertyAllowEntry
{
	GENERATED_BODY()

	/** Owning class path: an actor class, or a component class found on the target.
	 *  e.g. "/Script/Engine.PointLightComponent". */
	UPROPERTY(config, EditAnywhere, Category = "Property Policy")
	FString ClassPath;

	/** Reflected property name on that class (or inherited), e.g. "Intensity". May be a
	 *  dotted sub-path into struct members, e.g. "PostProcessSettings.BloomIntensity". */
	UPROPERTY(config, EditAnywhere, Category = "Property Policy")
	FName PropertyName;

	/** Declared value type — one of: float, int, bool, vector, color, name, enum, asset.
	 *  `enum` and `asset` both take a JSON string: an enum value name, or an asset path.
	 *  The validator/executor refuse a value whose JSON kind does not match this. */
	UPROPERTY(config, EditAnywhere, Category = "Property Policy")
	FString Type;

	/** Optional inclusive numeric range for float/int (ignored for other types). */
	UPROPERTY(config, EditAnywhere, Category = "Property Policy")
	bool bHasRange = false;

	UPROPERTY(config, EditAnywhere, Category = "Property Policy", meta = (EditCondition = "bHasRange"))
	double Min = 0.0;

	UPROPERTY(config, EditAnywhere, Category = "Property Policy", meta = (EditCondition = "bHasRange"))
	double Max = 0.0;

	/** For Type `asset`: the assigned asset must be (IsA) this class path, e.g.
	 *  "/Script/Engine.StaticMesh". Empty means any class (not recommended). */
	UPROPERTY(config, EditAnywhere, Category = "Property Policy")
	FString AssetClass;

	/** Optional sibling bool, in the SAME struct as the (leaf) property, that the executor
	 *  also sets true when this property is written — e.g. "bOverride_BloomIntensity" so an
	 *  FPostProcessSettings member actually takes effect. Empty means none. */
	UPROPERTY(config, EditAnywhere, Category = "Property Policy")
	FString OverrideFlag;
};

/** Project settings for the UE5MCP tool host (Project Settings > Plugins > UE5MCP). */
UCLASS(config = Editor, defaultconfig, meta = (DisplayName = "UE5MCP"))
class UUE5MCPSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UUE5MCPSettings();

	virtual FName GetContainerName() const override { return TEXT("Project"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** The local typed tool-call bridge only runs when explicitly enabled. It binds
	 *  to localhost only; external machines can never reach it. */
	UPROPERTY(config, EditAnywhere, Category = "Bridge", meta = (DisplayName = "Enable local bridge (localhost only)"))
	bool bEnableBridge = false;

	UPROPERTY(config, EditAnywhere, Category = "Bridge", meta = (ClampMin = 1024, ClampMax = 65535))
	int32 BridgePort = 30110;

	/** Hard cap on loaded actors included in any context snapshot. */
	UPROPERTY(config, EditAnywhere, Category = "Context", meta = (ClampMin = 1, ClampMax = 1000))
	int32 MaxContextActors = 200;

	/** When enabled, mutating plans posted to the bridge with mode=execute_external run
	 *  without an in-editor click: the human approval already happened inline in the
	 *  external agent session as the MCP client's native tool-permission prompt. The
	 *  plugin remains the enforcement boundary — it re-validates schema/policy/allowlists,
	 *  blocks during PIE, wraps the batch in one undoable transaction, logs every action,
	 *  and refuses policy violations even after a user-approved call. Off by default. */
	UPROPERTY(config, EditAnywhere, Category = "External Approval",
		meta = (DisplayName = "Allow external-session approval (client permission prompt is the human gate)"))
	bool bAllowExternalSessionApproval = false;

	/** Belt-and-suspenders: when enabled, DESTRUCTIVE external plans additionally pend
	 *  for an in-editor approval click on top of the in-session prompt. Off by default —
	 *  the always-prompting in-session permission is the gate. */
	UPROPERTY(config, EditAnywhere, Category = "External Approval",
		meta = (DisplayName = "Also require in-editor confirm for destructive external plans"))
	bool bRequireInEditorConfirmForDestructive = false;

	/** Exact actor class paths spawn_actor_from_class may instantiate. Anything not
	 *  listed here is refused by the validator AND re-checked by the executor. */
	UPROPERTY(config, EditAnywhere, Category = "Spawn Policy")
	TArray<FString> SpawnClassAllowlist;

	/** Exact static-mesh asset paths spawn_actor_from_class may assign to a freshly
	 *  spawned StaticMeshActor. */
	UPROPERTY(config, EditAnywhere, Category = "Spawn Policy")
	TArray<FString> SpawnMeshAllowlist;

	/** Exact (class, property, type) tuples set_actor_property may write. Anything not
	 *  listed here is refused by the validator AND re-checked by the executor — there is
	 *  no arbitrary property write. Projects widen this consciously, never implicitly. */
	UPROPERTY(config, EditAnywhere, Category = "Property Policy")
	TArray<FUE5MCPPropertyAllowEntry> PropertyAllowlist;

	/** When enabled (default), the executor refuses any mutation that would dirty a
	 *  package it could not save: a package whose on-disk file is read-only (e.g. an
	 *  unchecked-out Perforce file) or that source control reports checked out by
	 *  someone else. New/unsaved packages and writable files are unaffected, so solo
	 *  / no-source-control workflows are never blocked. Disable to let mutations dirty
	 *  packages regardless of writability (e.g. when an external save pipeline handles
	 *  checkout). */
	UPROPERTY(config, EditAnywhere, Category = "Package Policy",
		meta = (DisplayName = "Block mutations to unwritable / not-checked-out packages"))
	bool bBlockMutationsToUnwritablePackages = true;

	/** Gates the `check_out_package` tool — the only action that issues a real source
	 *  control operation (a checkout is a network write and is NOT editor-undoable;
	 *  revert via source control). Off by default: a project opts in consciously before
	 *  any AI-driven plan may check files out. Even when enabled, every checkout is
	 *  previewed and approved like any other mutation, and the plugin still refuses if
	 *  source control is unavailable or the package is not controlled / checked out by
	 *  another user. */
	UPROPERTY(config, EditAnywhere, Category = "Package Policy",
		meta = (DisplayName = "Allow check_out_package (issues a real source-control checkout)"))
	bool bAllowSourceControlCheckout = false;
};
