// Copyright DreamMoon. All Rights Reserved.

#include "DreamLocTranslateModule.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "DreamLocInContext.h"
#include "HttpModule.h"
#include "ISettingsModule.h"
#include "Modules/ModuleManager.h"
#include "SDreamLocTranslateSettings.h"
#include "SDreamLocTranslateWindow.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWindow.h"

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
			// Without it a new window for the tab takes the tab manager's own default.
			FTabManager::RegisterDefaultTabWindowSize(FTabId(DreamLocTranslateModuleLocal::TabName), DreamLocTranslateWindow::GetDefaultSize());
			// The translators are the user's (translators.ini), the project's choice among them is dloc.ini's: each page
			// reads and writes its own file, nothing goes to the engine's settings files. A section holds its widget weakly
			// (FSettingsSection::CustomWidget): the module keeps them, or the pages are gone before anyone opens them.
			if (ISettingsModule* Settings = FModuleManager::LoadModulePtr<ISettingsModule>(TEXT("Settings")))
			{
				TranslatorsPage = SNew(SDreamLocTranslatorSettings);
				ProjectPage = SNew(SDreamLocProjectTranslateSettings);
				Settings->RegisterSettings(TEXT("Editor"), TEXT("Plugins"), DreamLocTranslateWindow::GetTranslatorsSettingsName(),
					LOCTEXT("TranslatorsSettings", "DreamLoc translators"),
					LOCTEXT("TranslatorsSettingsTip", "The large models and machine translators DreamLoc can use, yours for every project (%APPDATA%/DreamLoc/translators.ini)"),
					TranslatorsPage.ToSharedRef());
				Settings->RegisterSettings(TEXT("Project"), TEXT("Plugins"), TEXT("DreamLoc"), LOCTEXT("ProjectSettings", "DreamLoc"),
					LOCTEXT("ProjectSettingsTip", "How this project translates: dloc.ini's [Translate]"), ProjectPage.ToSharedRef());
			}
			// Alt+click a text in the editor or PIE: the translation window at its entry.
			DreamLocInContext::Register();
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
		DreamLocInContext::Unregister();
		if (FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(DreamLocTranslateModuleLocal::TabName);
			FTabManager::UnregisterDefaultTabWindowSize(FTabId(DreamLocTranslateModuleLocal::TabName));
		}
		if (ISettingsModule* Settings = FModuleManager::GetModulePtr<ISettingsModule>(TEXT("Settings")))
		{
			Settings->UnregisterSettings(TEXT("Editor"), TEXT("Plugins"), DreamLocTranslateWindow::GetTranslatorsSettingsName());
			Settings->UnregisterSettings(TEXT("Project"), TEXT("Plugins"), TEXT("DreamLoc"));
		}
		TranslatorsPage.Reset();
		ProjectPage.Reset();
	}

private:
	TSharedPtr<SWidget> TranslatorsPage;
	TSharedPtr<SWidget> ProjectPage;

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

FName DreamLocTranslateWindow::GetTranslatorsSettingsName()
{
	return TEXT("DreamLocTranslators");
}

void DreamLocTranslateWindow::OpenTranslatorsSettings()
{
	if (ISettingsModule* Settings = FModuleManager::LoadModulePtr<ISettingsModule>(TEXT("Settings")))
	{
		Settings->ShowViewer(TEXT("Editor"), TEXT("Plugins"), GetTranslatorsSettingsName());
	}
}

FVector2D DreamLocTranslateWindow::GetDefaultSize()
{
	return FVector2D(1280.0, 760.0);
}

TOptional<FSlateRect> DreamLocTranslateWindow::FitRect(const FVector2D& InSize, bool bInMaximized, const FSlateRect& InWorkArea, float InDPIScale)
{
	const FVector2D Area(InWorkArea.GetSize());
	if (!bInMaximized && InSize.X <= Area.X * 0.9 && InSize.Y <= Area.Y * 0.9)
	{
		return TOptional<FSlateRect>();
	}
	const FVector2D Wanted = FVector2D::Min(GetDefaultSize() * InDPIScale, Area * 0.9);
	const FVector2D TopLeft = FVector2D(InWorkArea.GetTopLeft()) + (Area - Wanted) * 0.5;
	return FSlateRect(TopLeft, TopLeft + Wanted);
}

bool DreamLocTranslateWindow::FitWindow(SWindow& InWindow)
{
	const bool bMaximized = InWindow.IsWindowMaximized();
	const TOptional<FSlateRect> Fitted = FitRect(FVector2D(InWindow.GetSizeInScreen()), bMaximized, FSlateApplication::Get().GetWorkArea(InWindow.GetRectInScreen()),
		InWindow.GetDPIScaleFactor());
	if (!Fitted.IsSet())
	{
		return false;
	}
	if (bMaximized)
	{
		InWindow.Restore();
	}
	InWindow.ReshapeWindow(Fitted.GetValue());
	return true;
}

TSharedPtr<SDreamLocTranslateWindow> DreamLocTranslateWindow::Open()
{
	if (!FSlateApplication::IsInitialized())
	{
		return nullptr;
	}
	const bool bWasOpen = FGlobalTabmanager::Get()->FindExistingLiveTab(DreamLocTranslateModuleLocal::TabName).IsValid();
	const TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->TryInvokeTab(DreamLocTranslateModuleLocal::TabName);
	if (!Tab.IsValid())
	{
		return nullptr;
	}
	// A window of its own that comes back filling the screen (a size the layout kept, or maximized) opens at the
	// default size instead; one docked in the editor's main window, or already open, is left as it is.
	const TSharedPtr<SWindow> Window = Tab->GetParentWindow();
	if (!bWasOpen && Window.IsValid() && Window != FGlobalTabmanager::Get()->GetRootWindow() && FitWindow(*Window))
	{
		UE_LOG(LogDreamLocTranslate, Log, TEXT("The translation window came back filling the screen; opened at %.0f x %.0f instead"),
			GetDefaultSize().X, GetDefaultSize().Y);
	}
	return StaticCastSharedRef<SDreamLocTranslateWindow>(Tab->GetContent());
}

IMPLEMENT_MODULE(FDreamLocTranslateModule, DreamLocTranslate)

#undef LOCTEXT_NAMESPACE
