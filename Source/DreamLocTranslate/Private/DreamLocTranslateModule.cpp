// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocTranslateModule.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HttpModule.h"
#include "Modules/ModuleManager.h"
#include "SDreamLocTranslateWindow.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "DreamLocTranslate"

namespace DreamLocTranslateModuleLocal
{
	const FName TabName(TEXT("DreamLocTranslate"));
}

class FDreamLocTranslateModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Loaded here, on the game thread, before a worker thread asks for it.
		FHttpModule::Get();
		if (FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->RegisterNomadTabSpawner(DreamLocTranslateModuleLocal::TabName,
				FOnSpawnTab::CreateRaw(this, &FDreamLocTranslateModule::SpawnTab))
				.SetDisplayName(LOCTEXT("TabTitle", "DreamLoc Translate"))
				.SetTooltipText(LOCTEXT("TabTooltip", "Translate the .dloc files with a large model or a machine translator, and review what comes back"))
				.SetMenuType(ETabSpawnerMenuType::Hidden);
		}
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FDreamLocTranslateModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		if (UToolMenus* Menus = UToolMenus::TryGet())
		{
			Menus->UnregisterOwner(this);
		}
		if (FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(DreamLocTranslateModuleLocal::TabName);
		}
	}

private:
	void RegisterMenus()
	{
		FToolMenuOwnerScoped Owner(this);
		UToolMenu* Tools = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
		if (Tools == nullptr)
		{
			return;
		}
		FToolMenuSection& Section = Tools->FindOrAddSection(TEXT("DreamLoc"), LOCTEXT("DreamLocSection", "DreamLoc"));
		Section.AddSubMenu(TEXT("DreamLoc"), LOCTEXT("DreamLocMenu", "DreamLoc"), LOCTEXT("DreamLocMenuTooltip", "DreamLoc's tools"),
			FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
			{
				FToolMenuSection& Inner = InMenu->AddSection(TEXT("DreamLocTranslate"));
				Inner.AddMenuEntry(TEXT("DreamLoc.Translate"), LOCTEXT("TranslateEntry", "Translate"),
					LOCTEXT("TranslateEntryTooltip", "Open the translation window"),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Comment")),
					FUIAction(FExecuteAction::CreateLambda([]() { DreamLocTranslateWindow::Open(); })));
			}));
	}

	TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs&)
	{
		return SNew(SDockTab).TabRole(ETabRole::NomadTab)[SNew(SDreamLocTranslateWindow)];
	}
};

FName DreamLocTranslateWindow::GetTabName()
{
	return DreamLocTranslateModuleLocal::TabName;
}

TSharedPtr<SDreamLocTranslateWindow> DreamLocTranslateWindow::Open()
{
	if (!FSlateApplication::IsInitialized())
	{
		return nullptr;
	}
	const TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->TryInvokeTab(DreamLocTranslateModuleLocal::TabName);
	if (!Tab.IsValid())
	{
		return nullptr;
	}
	return StaticCastSharedRef<SDreamLocTranslateWindow>(Tab->GetContent());
}

IMPLEMENT_MODULE(FDreamLocTranslateModule, DreamLocTranslate)

#undef LOCTEXT_NAMESPACE
