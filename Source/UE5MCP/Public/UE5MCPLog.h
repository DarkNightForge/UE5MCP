// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUE5MCP, Log, All);

DECLARE_MULTICAST_DELEGATE_OneParam(FOnUE5MCPLogLine, const FString&);

/**
 * Structured result/log sink for the tool host. Every line goes to the LogUE5MCP
 * category, an in-memory buffer (bounded), and any subscribed views (the panel).
 */
class FUE5MCPLog
{
public:
	void Append(const FString& Line);
	const TArray<FString>& GetLines() const { return Lines; }

	FOnUE5MCPLogLine OnLine;

private:
	static constexpr int32 MaxBufferedLines = 512;
	TArray<FString> Lines;
};
