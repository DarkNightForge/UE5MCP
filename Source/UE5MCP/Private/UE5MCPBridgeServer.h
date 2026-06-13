// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpRouteHandle.h"

class IHttpRouter;

struct FUE5MCPBridgeResponse
{
	int32 Code = 200;
	FString Body;
};

/**
 * Bridge v1: a localhost-only typed tool-call transport. The bridge is transport,
 * not policy — every request terminates in FUE5MCPEditorService, behind the same
 * validation/preview/policy/transaction/log path as the panel. POST /plan modes:
 * "submit" (mutations pend for the in-editor panel click), "preview" (typed
 * preview only, never executes), and "execute_external" (the human approval
 * happened inline in the agent session as the MCP client's tool-permission
 * prompt; the service re-validates, enforces allowlists/PIE guards, and retains
 * final refusal authority — gated by bAllowExternalSessionApproval, off by
 * default). The bridge itself never reaches the executor or the approval click.
 *
 * HTTP handlers run synchronously on the game thread (FHttpServerModule ticks
 * them); they must never be moved off-thread or deferred without redesigning the
 * service's (lock-free, game-thread-only) concurrency model.
 */
class FUE5MCPBridgeServer
{
public:
	~FUE5MCPBridgeServer();

	bool Start();
	void Stop();
	bool IsRunning() const { return bRunning; }
	int32 GetBoundPort() const { return BoundPort; }

	// Plain, socket-free request handlers; the HTTP layer is a thin shim over
	// these so automation tests can drive them in-process.
	FUE5MCPBridgeResponse HandleGetStatus() const;
	FUE5MCPBridgeResponse HandleGetContext() const;
	FUE5MCPBridgeResponse HandlePostPlan(const FString& Body);
	FUE5MCPBridgeResponse HandleGetPlan(const FString& PlanId) const;

	/** True iff the host/origin string denotes loopback (localhost / 127.0.0.x /
	 *  ::1), tolerating scheme, port, and IPv6-bracket forms. The bind-address
	 *  guard and the per-request Origin/Host guard both rely on this. */
	static bool IsLoopbackHostString(const FString& Value);

private:
	bool bRunning = false;
	int32 BoundPort = 0;
	TSharedPtr<IHttpRouter> Router;
	TArray<FHttpRouteHandle> RouteHandles;
};
