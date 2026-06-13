// Copyright Epic Games, Inc. All Rights Reserved.

#include "UE5MCPCommands.h"

#define LOCTEXT_NAMESPACE "FUE5MCPModule"

void FUE5MCPCommands::RegisterCommands()
{
	UI_COMMAND(OpenPluginWindow, "UE5 Copilot", "Open the UE5MCP Copilot panel (typed, policy-checked editor tools for agents)", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control | EModifierKey::Alt, EKeys::U));
}

#undef LOCTEXT_NAMESPACE
