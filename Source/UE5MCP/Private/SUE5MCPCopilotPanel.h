// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UE5MCPTypes.h"
#include "Widgets/SCompoundWidget.h"

class ITableRow;
class SMultiLineEditableTextBox;
class STableViewBase;
template <typename ItemType> class SListView;

/** Thin view over FUE5MCPEditorService: renders context, the pending preview,
 *  and the log; buttons forward to the service. No policy logic lives here. */
class SUE5MCPCopilotPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUE5MCPCopilotPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SUE5MCPCopilotPanel() override;

private:
	FReply OnGeneratePlanClicked();
	FReply OnApproveClicked();
	FReply OnClearClicked();
	bool IsApproveEnabled() const;
	FText GetContextSummaryText() const;
	FName GetRequestedFolderPath() const;

	void RefreshFromService();
	void HandleServiceStateChanged();
	void HandleServiceLogLine(const FString& Line);
	void AppendLog(const FString& Line);

	TSharedRef<ITableRow> OnGeneratePlanRow(TSharedPtr<FUE5MCPResolvedAction> Item, const TSharedRef<STableViewBase>& OwnerTable) const;
	TSharedRef<ITableRow> OnGenerateLogRow(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& OwnerTable) const;

private:
	FUE5MCPContextPack Context;
	TArray<TSharedPtr<FUE5MCPResolvedAction>> PlanRows;
	TArray<TSharedPtr<FString>> LogLines;
	TSharedPtr<SMultiLineEditableTextBox> RequestTextBox;
	TSharedPtr<SListView<TSharedPtr<FUE5MCPResolvedAction>>> PlanListView;
	TSharedPtr<SListView<TSharedPtr<FString>>> LogListView;
	FDelegateHandle StateChangedHandle;
	FDelegateHandle LogLineHandle;
};
