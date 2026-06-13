// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UE5MCPSettings.generated.h"

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
};
