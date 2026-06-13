// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"

class FUE5MCPCommands : public TCommands<FUE5MCPCommands>
{
public:
	FUE5MCPCommands()
		: TCommands<FUE5MCPCommands>(
			TEXT("UE5MCP"),
			NSLOCTEXT("Contexts", "UE5MCP", "UE5 Editor Copilot"),
			NAME_None,
			FAppStyle::GetAppStyleSetName())
	{
	}

	virtual void RegisterCommands() override;

public:
	TSharedPtr<FUICommandInfo> OpenPluginWindow;
};
