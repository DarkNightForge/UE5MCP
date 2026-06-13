// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FAutoConsoleCommand;
class FSpawnTabArgs;
class FUE5MCPBridgeServer;
class FUE5MCPEditorService;
class FUICommandList;
class SDockTab;

class FUE5MCPModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FUE5MCPModule& Get();
	FUE5MCPEditorService& GetService() const { return *EditorService; }
	FUE5MCPBridgeServer& GetBridge() const { return *BridgeServer; }

private:
	void OpenPluginWindow();
	void RegisterMenus();
	TSharedRef<SDockTab> OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs);
	void StartBridgeIfEnabled();

private:
	TSharedPtr<FUICommandList> PluginCommands;
	TUniquePtr<FUE5MCPEditorService> EditorService;
	TUniquePtr<FUE5MCPBridgeServer> BridgeServer;
	TArray<TUniquePtr<FAutoConsoleCommand>> ConsoleCommands;
};
