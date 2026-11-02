// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FDreamLocTextSource;
class IPlugin;

/**
 * Registers DreamLoc's text source with the engine at start-up, before the engine first loads game text, so the
 * first load already has DreamLoc's translations in it.
 */
class DREAMLOC_API FDreamLocModule : public IModuleInterface
{
public:
	static FDreamLocModule& Get();
	static bool IsAvailable();

	/** The source the engine loads translations from. Valid between start-up and shut-down. */
	TSharedPtr<FDreamLocTextSource> GetTextSource() const { return TextSource; }

	/**
	 * Hands DreamLoc's translations to the engine again, after .dloc files changed.
	 *
	 * In the editor: for the game localization preview when it is on; nothing when it is off, since the editor then
	 * shows the source text and DreamLoc has nothing loaded. In a game: for the current language. The engine's
	 * RefreshResources would be wrong in the editor -- it reloads for the editor's language, which with a preview on
	 * puts the game text back into that language, not the previewed one.
	 *
	 * Asynchronous, as every engine reload is: FTextLocalizationManager::WaitForAsyncTasks waits for it.
	 */
	void ReloadTranslations();

	//~ IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/**
	 * A plugin mounted while the game runs -- a DLC, a mod -- with a DLoc/ folder becomes a layer over everything
	 * before it, and the translations are loaded again. Its cultures join the game's list.
	 */
	void HandlePluginMounted(IPlugin& InPlugin);
	void HandlePluginUnmounted(IPlugin& InPlugin);

	TSharedPtr<FDreamLocTextSource> TextSource;
	/** The next layer a mounted plugin gets: above the mods found at start-up. */
	int32 NextMountedLayer = 2;
	FDelegateHandle MountedHandle;
	FDelegateHandle CreatedHandle;
	FDelegateHandle UnmountedHandle;
};
