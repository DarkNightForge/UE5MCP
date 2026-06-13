// Copyright Epic Games, Inc. All Rights Reserved.

#include "UE5MCPLog.h"

DEFINE_LOG_CATEGORY(LogUE5MCP);

void FUE5MCPLog::Append(const FString& Line)
{
	UE_LOG(LogUE5MCP, Log, TEXT("%s"), *Line);

	Lines.Add(Line);
	if (Lines.Num() > MaxBufferedLines)
	{
		Lines.RemoveAt(0, Lines.Num() - MaxBufferedLines);
	}

	OnLine.Broadcast(Line);
}
