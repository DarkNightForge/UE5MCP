// Copyright Epic Games, Inc. All Rights Reserved.

#include "UE5MCPBridgeServer.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Misc/ConfigCacheIni.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UE5MCPEditorService.h"
#include "UE5MCPJson.h"
#include "UE5MCPLog.h"
#include "UE5MCPSettings.h"

namespace
{
	// The browser-facing custom header that forces a CORS preflight for any
	// cross-origin request. A page in the user's browser cannot set it on a
	// "simple" cross-site request, so requiring it blocks CSRF / DNS-rebinding
	// attacks against this unauthenticated localhost service. The MCP server sends it.
	const TCHAR* const ClientHeaderName = TEXT("x-ue5mcp-client");

	TUniquePtr<FHttpServerResponse> MakeJsonResponse(const FUE5MCPBridgeResponse& BridgeResponse)
	{
		TUniquePtr<FHttpServerResponse> Response =
			FHttpServerResponse::Create(BridgeResponse.Body, TEXT("application/json"));
		Response->Code = static_cast<EHttpServerResponseCodes>(BridgeResponse.Code);
		return Response;
	}

	FString RequestBodyToString(const FHttpServerRequest& Request)
	{
		return FString(FUTF8ToTCHAR(
			reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num()));
	}

	const FString* FindHeader(const FHttpServerRequest& Request, const TCHAR* Name)
	{
		for (const TPair<FString, TArray<FString>>& Pair : Request.Headers)
		{
			if (Pair.Key.Equals(Name, ESearchCase::IgnoreCase) && Pair.Value.Num() > 0)
			{
				return &Pair.Value[0];
			}
		}
		return nullptr;
	}

	/** Rejects browser-originated and cross-host requests on an unauthenticated
	 *  localhost service. Returns false (and fills OutRefusal) when the request is
	 *  not safe to serve. Applied to every route. */
	bool IsRequestOriginSafe(const FHttpServerRequest& Request, FUE5MCPBridgeResponse& OutRefusal)
	{
		// An Origin header is sent by browsers on cross-origin and all non-GET
		// requests. Anything not loopback (or the opaque "null") is rejected.
		if (const FString* Origin = FindHeader(Request, TEXT("origin")))
		{
			if (!Origin->Equals(TEXT("null"), ESearchCase::IgnoreCase) && !FUE5MCPBridgeServer::IsLoopbackHostString(*Origin))
			{
				OutRefusal = { 403, UE5MCPJson::SerializeError(TEXT("forbidden_origin"),
					TEXT("Request rejected: non-loopback Origin. The UE5MCP bridge only serves local tooling.")) };
				return false;
			}
		}
		// Host must be a loopback name (defends against DNS-rebinding, where a
		// malicious page resolves an attacker domain to 127.0.0.1).
		if (const FString* Host = FindHeader(Request, TEXT("host")))
		{
			if (!FUE5MCPBridgeServer::IsLoopbackHostString(*Host))
			{
				OutRefusal = { 403, UE5MCPJson::SerializeError(TEXT("forbidden_host"),
					TEXT("Request rejected: non-loopback Host header.")) };
				return false;
			}
		}
		return true;
	}
}

bool FUE5MCPBridgeServer::IsLoopbackHostString(const FString& InValue)
{
	FString Value = InValue;
	// Strip scheme and trailing path/port so "http://localhost:30110/x" -> "localhost".
	int32 SchemePos = INDEX_NONE;
	if (Value.FindChar(TEXT(':'), SchemePos) && Value.Mid(SchemePos).StartsWith(TEXT("://")))
	{
		Value.RightChopInline(SchemePos + 3);
	}
	int32 SlashPos = INDEX_NONE;
	if (Value.FindChar(TEXT('/'), SlashPos))
	{
		Value.LeftInline(SlashPos);
	}
	Value.TrimStartAndEndInline();
	// IPv6 literal: [::1]:port
	if (Value.StartsWith(TEXT("[")))
	{
		int32 Close = INDEX_NONE;
		if (Value.FindChar(TEXT(']'), Close))
		{
			Value = Value.Mid(1, Close - 1);
		}
	}
	else
	{
		// Only treat a trailing ":NNNN" as a port when there is exactly one colon —
		// a bare IPv6 literal like "::1" has several colons and no port to strip.
		int32 FirstColon = INDEX_NONE;
		int32 LastColon = INDEX_NONE;
		Value.FindChar(TEXT(':'), FirstColon);
		Value.FindLastChar(TEXT(':'), LastColon);
		if (FirstColon != INDEX_NONE && FirstColon == LastColon)
		{
			Value.LeftInline(FirstColon);
		}
	}
	Value.TrimStartAndEndInline();
	return Value.Equals(TEXT("localhost"), ESearchCase::IgnoreCase)
		|| Value == TEXT("::1")
		|| Value.StartsWith(TEXT("127."));
}

FUE5MCPBridgeServer::~FUE5MCPBridgeServer()
{
	Stop();
}

bool FUE5MCPBridgeServer::Start()
{
	check(IsInGameThread());
	if (bRunning)
	{
		return true;
	}

	const UUE5MCPSettings* Settings = GetDefault<UUE5MCPSettings>();
	const int32 Port = Settings->BridgePort;

	// Loopback enforcement: the engine ini ([HTTPServer.Listeners]) can rebind
	// listeners to a non-loopback address. The bridge refuses to start on anything
	// but localhost — external machines must never reach the tool host. We resolve
	// the EFFECTIVE bind address the same way the engine does: DefaultBindAddress
	// first, then the per-port ListenerOverrides entry (higher precedence) for OUR
	// port. Checking only DefaultBindAddress would miss a per-port "any" override.
	FString BindAddress = TEXT("localhost");
	GConfig->GetString(TEXT("HTTPServer.Listeners"), TEXT("DefaultBindAddress"), BindAddress, GEngineIni);
	{
		TArray<FString> ListenerOverrides;
		if (GConfig->GetArray(TEXT("HTTPServer.Listeners"), TEXT("ListenerOverrides"), ListenerOverrides, GEngineIni))
		{
			for (FString Override : ListenerOverrides)
			{
				Override.ReplaceInline(TEXT("("), TEXT(""));
				Override.ReplaceInline(TEXT(")"), TEXT(""));
				uint32 OverridePort = 0;
				if (FParse::Value(*Override, TEXT("Port="), OverridePort) && OverridePort == static_cast<uint32>(Port))
				{
					FParse::Value(*Override, TEXT("BindAddress="), BindAddress);
					break;
				}
			}
		}
	}
	if (!IsLoopbackHostString(BindAddress))
	{
		UE_LOG(LogUE5MCP, Error,
			TEXT("[Safety] Bridge refused to start: effective HTTP bind address for port %d is '%s', not loopback."),
			Port, *BindAddress);
		return false;
	}

	Router = FHttpServerModule::Get().GetHttpRouter(Port, /*bFailOnBindFailure=*/true);
	if (!Router.IsValid())
	{
		UE_LOG(LogUE5MCP, Error, TEXT("Bridge could not bind port %d (already in use?)."), Port);
		return false;
	}

	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/status")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda(
			[this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
			{
				FUE5MCPBridgeResponse Refusal;
				if (!IsRequestOriginSafe(Request, Refusal)) { OnComplete(MakeJsonResponse(Refusal)); return true; }
				OnComplete(MakeJsonResponse(HandleGetStatus()));
				return true;
			})));

	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/context")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda(
			[this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
			{
				FUE5MCPBridgeResponse Refusal;
				if (!IsRequestOriginSafe(Request, Refusal)) { OnComplete(MakeJsonResponse(Refusal)); return true; }
				OnComplete(MakeJsonResponse(HandleGetContext()));
				return true;
			})));

	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/plan")), EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda(
			[this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
			{
				FUE5MCPBridgeResponse Refusal;
				if (!IsRequestOriginSafe(Request, Refusal)) { OnComplete(MakeJsonResponse(Refusal)); return true; }
				// The mutation-submitting route additionally requires the custom client
				// header — a browser cannot set it cross-origin without a CORS preflight
				// (which this server never grants), so CSRF/rebinding POSTs are rejected.
				if (!FindHeader(Request, ClientHeaderName))
				{
					OnComplete(MakeJsonResponse({ 403, UE5MCPJson::SerializeError(TEXT("forbidden_client"),
						TEXT("POST /plan requires the X-UE5MCP-Client header (sent by the UE5MCP MCP server).")) }));
					return true;
				}
				OnComplete(MakeJsonResponse(HandlePostPlan(RequestBodyToString(Request))));
				return true;
			})));

	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/plan/:id")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda(
			[this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
			{
				FUE5MCPBridgeResponse Refusal;
				if (!IsRequestOriginSafe(Request, Refusal)) { OnComplete(MakeJsonResponse(Refusal)); return true; }
				const FString* PlanId = Request.PathParams.Find(TEXT("id"));
				OnComplete(MakeJsonResponse(HandleGetPlan(PlanId ? *PlanId : FString())));
				return true;
			})));

	FHttpServerModule::Get().StartAllListeners();
	bRunning = true;
	BoundPort = Port;
	UE_LOG(LogUE5MCP, Log, TEXT("Bridge listening on localhost:%d (typed tool-call transport; human approval via panel click or external-session prompt)."), Port);
	FUE5MCPEditorService::Get().GetLog().Append(
		FString::Printf(TEXT("Local bridge started on localhost:%d."), Port));
	return true;
}

void FUE5MCPBridgeServer::Stop()
{
	if (!bRunning)
	{
		return;
	}

	// Unbind only our own routes. Never call StopAllListeners(): the HTTP server
	// module is shared and other plugins may be listening on other ports.
	if (Router.IsValid())
	{
		for (const FHttpRouteHandle& Handle : RouteHandles)
		{
			Router->UnbindRoute(Handle);
		}
	}
	RouteHandles.Empty();
	Router.Reset();
	bRunning = false;
	UE_LOG(LogUE5MCP, Log, TEXT("Bridge stopped (port %d routes unbound)."), BoundPort);
	BoundPort = 0;
}

FUE5MCPBridgeResponse FUE5MCPBridgeServer::HandleGetStatus() const
{
	check(IsInGameThread());
	FUE5MCPEditorService& Service = FUE5MCPEditorService::Get();

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema_version"), 1);
	Root->SetStringField(TEXT("plugin"), TEXT("UE5MCP"));
	Root->SetBoolField(TEXT("play_mode_active"), GEditor && GEditor->PlayWorld);
	const TSharedPtr<const FUE5MCPPlanRecord> Pending = Service.GetCurrentPlanRecord();
	if (Pending.IsValid() && Pending->Status == EUE5MCPPlanStatus::PendingApproval)
	{
		Root->SetStringField(TEXT("pending_plan_id"), Pending->PlanId);
	}

	FString Body;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Body);
	FJsonSerializer::Serialize(Root, Writer);
	return { 200, Body };
}

FUE5MCPBridgeResponse FUE5MCPBridgeServer::HandleGetContext() const
{
	check(IsInGameThread());
	const int32 MaxActors = GetDefault<UUE5MCPSettings>()->MaxContextActors;
	const FUE5MCPContextPack Context = FUE5MCPEditorService::Get().CollectContext(MaxActors);
	return { 200, UE5MCPJson::SerializeContextPack(Context) };
}

FUE5MCPBridgeResponse FUE5MCPBridgeServer::HandlePostPlan(const FString& Body)
{
	check(IsInGameThread());

	// Transport-level "mode" selects how human approval is (or was) obtained:
	//   submit           — legacy: mutations pend for an in-editor panel click.
	//   preview          — validate + typed preview only; never executes.
	//   execute_external — the human already approved this call inline in the agent
	//                      session (client tool-permission prompt); the service
	//                      re-validates and retains final refusal authority.
	FString Mode = TEXT("submit");
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Body);
		if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
		{
			Root->TryGetStringField(TEXT("mode"), Mode);
		}
	}
	if (Mode != TEXT("submit") && Mode != TEXT("preview") && Mode != TEXT("execute_external"))
	{
		return { 400, UE5MCPJson::SerializeError(TEXT("unknown_mode"),
			FString::Printf(TEXT("Unknown plan mode '%s' (expected submit, preview, or execute_external)."), *Mode)) };
	}

	FUE5MCPPlanRequest Request;
	TArray<FString> ParseErrors;
	if (!UE5MCPJson::ParsePlanRequest(Body, Request, ParseErrors))
	{
		return { 400, UE5MCPJson::SerializeError(TEXT("invalid_plan"), FString::Join(ParseErrors, TEXT("; "))) };
	}

	FUE5MCPEditorService& Service = FUE5MCPEditorService::Get();
	FString RefusalCode;

	if (Mode == TEXT("preview"))
	{
		const TSharedPtr<const FUE5MCPPlanRecord> Record =
			Service.PreviewPlanRequest(Request, EUE5MCPPlanSource::Bridge, RefusalCode);
		if (!Record.IsValid())
		{
			return { 400, UE5MCPJson::SerializeError(TEXT("invalid_plan"), TEXT("Preview request could not be validated.")) };
		}
		return { Record->Status == EUE5MCPPlanStatus::Invalid ? 400 : 200, UE5MCPJson::SerializePlanRecord(*Record) };
	}

	if (Mode == TEXT("execute_external"))
	{
		const TSharedPtr<const FUE5MCPPlanRecord> Record = Service.SubmitExternalPlan(Request, RefusalCode);
		if (!Record.IsValid())
		{
			if (RefusalCode == TEXT("external_approval_disabled"))
			{
				return { 403, UE5MCPJson::SerializeError(RefusalCode,
					TEXT("External-session approval is disabled in UE5MCP project settings (bAllowExternalSessionApproval).")) };
			}
			return { 409, UE5MCPJson::SerializeError(
				RefusalCode.IsEmpty() ? TEXT("plan_pending") : *RefusalCode,
				TEXT("Another plan is pending approval; poll it or wait for the human to decide.")) };
		}
		switch (Record->Status)
		{
		case EUE5MCPPlanStatus::Invalid:
			return { 400, UE5MCPJson::SerializePlanRecord(*Record) };
		case EUE5MCPPlanStatus::PendingApproval:
			// Destructive belt-and-suspenders: accepted, awaiting the in-editor confirm.
			return { 202, UE5MCPJson::SerializePlanRecord(*Record) };
		case EUE5MCPPlanStatus::RefusedStale:
			return { 409, UE5MCPJson::SerializePlanRecord(*Record) };
		case EUE5MCPPlanStatus::Failed:
			// Upfront refusal (play mode, no world) reports 409 so clients retry later;
			// per-action failures still return 200 with the structured results.
			return { Record->Result.ActionResults.IsEmpty() ? 409 : 200, UE5MCPJson::SerializePlanRecord(*Record) };
		default:
			return { 200, UE5MCPJson::SerializePlanRecord(*Record) };
		}
	}

	const TSharedPtr<const FUE5MCPPlanRecord> Record =
		Service.SubmitPlanRequest(Request, EUE5MCPPlanSource::Bridge, RefusalCode);

	if (!Record.IsValid())
	{
		return { 409, UE5MCPJson::SerializeError(
			RefusalCode.IsEmpty() ? TEXT("plan_pending") : *RefusalCode,
			TEXT("Another plan is pending approval; poll it or wait for the human to decide.")) };
	}
	if (Record->Status == EUE5MCPPlanStatus::Invalid)
	{
		return { 400, UE5MCPJson::SerializePlanRecord(*Record) };
	}
	return { 200, UE5MCPJson::SerializePlanRecord(*Record) };
}

FUE5MCPBridgeResponse FUE5MCPBridgeServer::HandleGetPlan(const FString& PlanId) const
{
	check(IsInGameThread());
	const TSharedPtr<const FUE5MCPPlanRecord> Record = FUE5MCPEditorService::Get().FindPlanRecord(PlanId);
	if (!Record.IsValid())
	{
		return { 404, UE5MCPJson::SerializeError(TEXT("plan_not_found"),
			FString::Printf(TEXT("No plan record with id '%s'."), *PlanId)) };
	}
	return { 200, UE5MCPJson::SerializePlanRecord(*Record) };
}
