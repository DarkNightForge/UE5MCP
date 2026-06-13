// Copyright Epic Games, Inc. All Rights Reserved.

#include "SUE5MCPCopilotPanel.h"

#include "UE5MCP.h"
#include "UE5MCPEditorService.h"
#include "Modules/ModuleManager.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SUE5MCPCopilotPanel"

void SUE5MCPCopilotPanel::Construct(const FArguments& InArgs)
{
	FUE5MCPEditorService& Service = FUE5MCPEditorService::Get();

	// Backfill log lines emitted before this panel existed, then subscribe.
	for (const FString& Line : Service.GetLog().GetLines())
	{
		LogLines.Add(MakeShared<FString>(Line));
	}
	StateChangedHandle = Service.OnStateChanged.AddSP(this, &SUE5MCPCopilotPanel::HandleServiceStateChanged);
	LogLineHandle = Service.GetLog().OnLine.AddSP(this, &SUE5MCPCopilotPanel::HandleServiceLogLine);

	AppendLog(TEXT("UE5MCP first proof loaded. Generate Plan creates a typed set_actor_folder preview for selected actors only."));

	ChildSlot
	[
		SNew(SBorder)
		.Padding(12.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Title", "UE5 Editor Copilot — Safe First Proof"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(this, &SUE5MCPCopilotPanel::GetContextSummaryText)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f)
			[
				SAssignNew(RequestTextBox, SMultiLineEditableTextBox)
				.HintText(LOCTEXT("PromptHint", "Folder path for selected actors, e.g. Lighting/KeyLights"))
				.AutoWrapText(true)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("GeneratePlan", "Generate Plan"))
					.OnClicked(this, &SUE5MCPCopilotPanel::OnGeneratePlanClicked)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("Approve", "Approve"))
					.IsEnabled(this, &SUE5MCPCopilotPanel::IsApproveEnabled)
					.OnClicked(this, &SUE5MCPCopilotPanel::OnApproveClicked)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("Clear", "Clear"))
					.OnClicked(this, &SUE5MCPCopilotPanel::OnClearClicked)
				]
			]

			+ SVerticalBox::Slot()
			.FillHeight(0.55f)
			.Padding(0.0f, 4.0f)
			[
				SAssignNew(PlanListView, SListView<TSharedPtr<FUE5MCPResolvedAction>>)
				.ListItemsSource(&PlanRows)
				.OnGenerateRow(this, &SUE5MCPCopilotPanel::OnGeneratePlanRow)
			]

			+ SVerticalBox::Slot()
			.FillHeight(0.45f)
			.Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				SAssignNew(LogListView, SListView<TSharedPtr<FString>>)
				.ListItemsSource(&LogLines)
				.OnGenerateRow(this, &SUE5MCPCopilotPanel::OnGenerateLogRow)
			]
		]
	];

	RefreshFromService();
}

SUE5MCPCopilotPanel::~SUE5MCPCopilotPanel()
{
	if (FUE5MCPModule* Module = FModuleManager::GetModulePtr<FUE5MCPModule>(TEXT("UE5MCP")))
	{
		FUE5MCPEditorService& Service = Module->GetService();
		Service.OnStateChanged.Remove(StateChangedHandle);
		Service.GetLog().OnLine.Remove(LogLineHandle);
	}
}

FReply SUE5MCPCopilotPanel::OnGeneratePlanClicked()
{
	FUE5MCPEditorService::Get().GeneratePanelFolderPlan(GetRequestedFolderPath());
	return FReply::Handled();
}

FReply SUE5MCPCopilotPanel::OnApproveClicked()
{
	FUE5MCPEditorService::Get().ApproveCurrentPlan();
	return FReply::Handled();
}

FReply SUE5MCPCopilotPanel::OnClearClicked()
{
	FUE5MCPEditorService::Get().ClearCurrentPlan();
	return FReply::Handled();
}

bool SUE5MCPCopilotPanel::IsApproveEnabled() const
{
	return FUE5MCPEditorService::Get().IsApprovalAvailable();
}

FText SUE5MCPCopilotPanel::GetContextSummaryText() const
{
	return FText::FromString(FString::Printf(
		TEXT("Editor world: %s | Selected actors: %d | Loaded actors in context: %d"),
		Context.WorldName.IsEmpty() ? TEXT("<none>") : *Context.WorldName,
		Context.SelectedActors.Num(),
		Context.LoadedActorsCapped.Num()));
}

FName SUE5MCPCopilotPanel::GetRequestedFolderPath() const
{
	FString RequestedFolder = RequestTextBox.IsValid() ? RequestTextBox->GetText().ToString().TrimStartAndEnd() : FString();
	if (RequestedFolder.IsEmpty())
	{
		RequestedFolder = TEXT("UE5MCP/Organized");
	}
	RequestedFolder.ReplaceInline(TEXT("\\"), TEXT("/"));
	return FName(*RequestedFolder);
}

void SUE5MCPCopilotPanel::RefreshFromService()
{
	FUE5MCPEditorService& Service = FUE5MCPEditorService::Get();
	Context = Service.CollectContext();

	PlanRows.Reset();
	const TSharedPtr<const FUE5MCPPlanRecord> Record = Service.GetCurrentPlanRecord();
	if (Record.IsValid() && Record->Status == EUE5MCPPlanStatus::PendingApproval)
	{
		for (const FUE5MCPResolvedAction& Resolved : Record->Plan.Actions)
		{
			PlanRows.Add(MakeShared<FUE5MCPResolvedAction>(Resolved));
		}
	}
	if (PlanListView.IsValid())
	{
		PlanListView->RequestListRefresh();
	}
}

void SUE5MCPCopilotPanel::HandleServiceStateChanged()
{
	RefreshFromService();
}

void SUE5MCPCopilotPanel::HandleServiceLogLine(const FString& Line)
{
	AppendLog(Line);
}

void SUE5MCPCopilotPanel::AppendLog(const FString& Line)
{
	LogLines.Add(MakeShared<FString>(Line));
	if (LogListView.IsValid())
	{
		LogListView->RequestListRefresh();
		LogListView->RequestScrollIntoView(LogLines.Last());
	}
}

TSharedRef<ITableRow> SUE5MCPCopilotPanel::OnGeneratePlanRow(TSharedPtr<FUE5MCPResolvedAction> Item, const TSharedRef<STableViewBase>& OwnerTable) const
{
	const FString RowText = Item.IsValid() ? Item->PreviewText : TEXT("<invalid action>");
	return SNew(STableRow<TSharedPtr<FUE5MCPResolvedAction>>, OwnerTable)
	[
		SNew(STextBlock).Text(FText::FromString(RowText))
	];
}

TSharedRef<ITableRow> SUE5MCPCopilotPanel::OnGenerateLogRow(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& OwnerTable) const
{
	return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
	[
		SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : FString()))
	];
}

#undef LOCTEXT_NAMESPACE
