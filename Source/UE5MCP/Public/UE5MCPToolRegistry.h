// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UE5MCPTypes.h"

/** Static descriptor for one allowlisted tool. The registry is the single source of
 *  truth mapping public tool names to action types, fixed risk, and allowed params. */
struct FUE5MCPToolDescriptor
{
	FString ToolName;
	EUE5MCPActionType ActionType = EUE5MCPActionType::GetSelectionContext;
	EUE5MCPRiskLevel Risk = EUE5MCPRiskLevel::ReadOnly;
	TArray<FString> AllowedParams;
	bool bRequiresTargets = false;
	/** Tools that operate on queries or fresh spawns take no targets at all; a
	 *  non-empty targets list on such a tool is a schema violation, not noise. */
	bool bAcceptsTargets = true;
};

class FUE5MCPToolRegistry
{
public:
	static const TArray<FUE5MCPToolDescriptor>& GetTools();
	static const FUE5MCPToolDescriptor* FindByName(const FString& ToolName);
	static const FUE5MCPToolDescriptor* FindByType(EUE5MCPActionType ActionType);

	static FString RiskToString(EUE5MCPRiskLevel Risk);
	static bool ParseRisk(const FString& RiskString, EUE5MCPRiskLevel& OutRisk);
};
