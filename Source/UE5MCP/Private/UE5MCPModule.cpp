// Copyright Epic Games, Inc. All Rights Reserved.

#include "UE5MCP.h"

#include "SUE5MCPCopilotPanel.h"
#include "UE5MCPBridgeServer.h"
#include "UE5MCPCommands.h"
#include "UE5MCPEditorService.h"
#include "UE5MCPLog.h"
#include "UE5MCPSettings.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

static const FName UE5MCPTabName(TEXT("UE5MCP"));

#define LOCTEXT_NAMESPACE "FUE5MCPModule"

FUE5MCPModule& FUE5MCPModule::Get()
{
	return FModuleManager::LoadModuleChecked<FUE5MCPModule>(TEXT("UE5MCP"));
}

void FUE5MCPModule::StartupModule()
{
	EditorService = MakeUnique<FUE5MCPEditorService>();
	BridgeServer = MakeUnique<FUE5MCPBridgeServer>();

	// The bridge is opt-in (off by default) and starts only after engine init.
	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FUE5MCPModule::StartBridgeIfEnabled);

	ConsoleCommands.Add(MakeUnique<FAutoConsoleCommand>(
		TEXT("UE5MCP.Bridge.Start"),
		TEXT("Start the UE5MCP local typed tool-call bridge (localhost only)."),
		FConsoleCommandDelegate::CreateLambda([this]() { BridgeServer->Start(); })));
	ConsoleCommands.Add(MakeUnique<FAutoConsoleCommand>(
		TEXT("UE5MCP.Bridge.Stop"),
		TEXT("Stop the UE5MCP local bridge."),
		FConsoleCommandDelegate::CreateLambda([this]() { BridgeServer->Stop(); })));
	ConsoleCommands.Add(MakeUnique<FAutoConsoleCommand>(
		TEXT("UE5MCP.Bridge.Status"),
		TEXT("Log the UE5MCP bridge status."),
		FConsoleCommandDelegate::CreateLambda([this]()
		{
			UE_LOG(LogUE5MCP, Display, TEXT("Bridge %s%s"),
				BridgeServer->IsRunning() ? TEXT("running on localhost:") : TEXT("stopped"),
				BridgeServer->IsRunning() ? *FString::FromInt(BridgeServer->GetBoundPort()) : TEXT(""));
		})));

	FUE5MCPCommands::Register();

	PluginCommands = MakeShared<FUICommandList>();
	PluginCommands->MapAction(
		FUE5MCPCommands::Get().OpenPluginWindow,
		FExecuteAction::CreateRaw(this, &FUE5MCPModule::OpenPluginWindow),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUE5MCPModule::RegisterMenus));

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(UE5MCPTabName, FOnSpawnTab::CreateRaw(this, &FUE5MCPModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("UE5MCPTabTitle", "UE5 Copilot"))
		.SetTooltipText(LOCTEXT("UE5MCPTabTooltip", "Open the safe editor-only UE5 Copilot proof panel"))
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Comment")))
		.SetMenuType(ETabSpawnerMenuType::Hidden);
}

void FUE5MCPModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	FUE5MCPCommands::Unregister();
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(UE5MCPTabName);

	FCoreDelegates::OnPostEngineInit.RemoveAll(this);
	ConsoleCommands.Empty();
	BridgeServer.Reset();
	EditorService.Reset();
}

void FUE5MCPModule::StartBridgeIfEnabled()
{
	if (GetDefault<UUE5MCPSettings>()->bEnableBridge)
	{
		BridgeServer->Start();
	}
}

void FUE5MCPModule::OpenPluginWindow()
{
	FGlobalTabmanager::Get()->TryInvokeTab(UE5MCPTabName);
}

TSharedRef<SDockTab> FUE5MCPModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SUE5MCPCopilotPanel)
		];
}

void FUE5MCPModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	// Tools menu (not the bottom of Window): this is a tool host, and Tools is
	// where users look for it. The hotkey (Ctrl+Alt+U) rides on the command.
	UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
	FToolMenuSection& ToolsSection = ToolsMenu->FindOrAddSection(TEXT("Programming"));
	FToolMenuEntry& MenuEntry = ToolsSection.AddMenuEntryWithCommandList(FUE5MCPCommands::Get().OpenPluginWindow, PluginCommands);
	MenuEntry.Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Comment"));

	UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.LevelEditorToolBar.PlayToolBar"));
	FToolMenuSection& ToolbarSection = ToolbarMenu->FindOrAddSection(TEXT("PluginTools"));
	FToolMenuEntry& Entry = ToolbarSection.AddEntry(FToolMenuEntry::InitToolBarButton(
		FUE5MCPCommands::Get().OpenPluginWindow,
		TAttribute<FText>(),
		TAttribute<FText>(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Comment"))));
	Entry.SetCommandList(PluginCommands);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUE5MCPModule, UE5MCP)
