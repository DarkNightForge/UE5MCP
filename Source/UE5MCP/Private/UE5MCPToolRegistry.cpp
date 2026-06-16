// Copyright Epic Games, Inc. All Rights Reserved.

#include "UE5MCPToolRegistry.h"

const TArray<FUE5MCPToolDescriptor>& FUE5MCPToolRegistry::GetTools()
{
	static const TArray<FUE5MCPToolDescriptor> Tools = {
		{ TEXT("get_selection_context"), EUE5MCPActionType::GetSelectionContext, EUE5MCPRiskLevel::ReadOnly,
			{ TEXT("max_objects") }, /*bRequiresTargets=*/false, /*bAcceptsTargets=*/false },
		{ TEXT("find_actors"), EUE5MCPActionType::FindActors, EUE5MCPRiskLevel::ReadOnly,
			{ TEXT("class_path"), TEXT("label_contains"), TEXT("tag"), TEXT("folder_path"), TEXT("selected_only"), TEXT("max_results") }, /*bRequiresTargets=*/false, /*bAcceptsTargets=*/false },
		{ TEXT("read_logs"), EUE5MCPActionType::ReadLogs, EUE5MCPRiskLevel::ReadOnly,
			{ TEXT("max_lines"), TEXT("contains") }, /*bRequiresTargets=*/false, /*bAcceptsTargets=*/false },
		{ TEXT("get_package_status"), EUE5MCPActionType::GetPackageStatus, EUE5MCPRiskLevel::ReadOnly,
			{ TEXT("max_packages"), TEXT("dirty_only") }, /*bRequiresTargets=*/false, /*bAcceptsTargets=*/false },
		{ TEXT("select_actors"), EUE5MCPActionType::SelectActors, EUE5MCPRiskLevel::LowMutation,
			{}, /*bRequiresTargets=*/true },
		{ TEXT("set_actor_folder"), EUE5MCPActionType::SetActorFolder, EUE5MCPRiskLevel::LowMutation,
			{ TEXT("folder_path") }, /*bRequiresTargets=*/true },
		{ TEXT("set_actor_label"), EUE5MCPActionType::SetActorLabel, EUE5MCPRiskLevel::LowMutation,
			{ TEXT("label") }, /*bRequiresTargets=*/true },
		{ TEXT("add_actor_tags"), EUE5MCPActionType::AddActorTags, EUE5MCPRiskLevel::LowMutation,
			{ TEXT("tags") }, /*bRequiresTargets=*/true },
		{ TEXT("remove_actor_tags"), EUE5MCPActionType::RemoveActorTags, EUE5MCPRiskLevel::LowMutation,
			{ TEXT("tags") }, /*bRequiresTargets=*/true },
		{ TEXT("set_actor_property"), EUE5MCPActionType::SetActorProperty, EUE5MCPRiskLevel::LowMutation,
			{ TEXT("property"), TEXT("value"), TEXT("component") }, /*bRequiresTargets=*/true },
		{ TEXT("set_actor_transform"), EUE5MCPActionType::SetActorTransform, EUE5MCPRiskLevel::LowMutation,
			{ TEXT("location"), TEXT("rotation"), TEXT("scale") }, /*bRequiresTargets=*/true },
		{ TEXT("duplicate_actor_with_offset"), EUE5MCPActionType::DuplicateActorWithOffset, EUE5MCPRiskLevel::LowMutation,
			{ TEXT("offset") }, /*bRequiresTargets=*/true },
		{ TEXT("spawn_actor_from_class"), EUE5MCPActionType::SpawnActorFromClass, EUE5MCPRiskLevel::LowMutation,
			{ TEXT("class_path"), TEXT("transforms"), TEXT("static_mesh"), TEXT("label_base") }, /*bRequiresTargets=*/false, /*bAcceptsTargets=*/false },
		{ TEXT("delete_actor"), EUE5MCPActionType::DeleteActor, EUE5MCPRiskLevel::Destructive,
			{}, /*bRequiresTargets=*/true },
	};
	return Tools;
}

const FUE5MCPToolDescriptor* FUE5MCPToolRegistry::FindByName(const FString& ToolName)
{
	return GetTools().FindByPredicate([&ToolName](const FUE5MCPToolDescriptor& Tool)
	{
		return Tool.ToolName == ToolName;
	});
}

const FUE5MCPToolDescriptor* FUE5MCPToolRegistry::FindByType(EUE5MCPActionType ActionType)
{
	return GetTools().FindByPredicate([ActionType](const FUE5MCPToolDescriptor& Tool)
	{
		return Tool.ActionType == ActionType;
	});
}

FString FUE5MCPToolRegistry::RiskToString(EUE5MCPRiskLevel Risk)
{
	switch (Risk)
	{
	case EUE5MCPRiskLevel::ReadOnly: return TEXT("read_only");
	case EUE5MCPRiskLevel::LowMutation: return TEXT("low_risk");
	case EUE5MCPRiskLevel::Destructive: return TEXT("destructive");
	}
	return TEXT("unknown");
}

bool FUE5MCPToolRegistry::ParseRisk(const FString& RiskString, EUE5MCPRiskLevel& OutRisk)
{
	if (RiskString == TEXT("read_only")) { OutRisk = EUE5MCPRiskLevel::ReadOnly; return true; }
	if (RiskString == TEXT("low_risk")) { OutRisk = EUE5MCPRiskLevel::LowMutation; return true; }
	if (RiskString == TEXT("destructive")) { OutRisk = EUE5MCPRiskLevel::Destructive; return true; }
	return false;
}
