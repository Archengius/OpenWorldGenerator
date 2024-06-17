// Copyright Nikita Zolotukhin. All Rights Reserved.

#include "OpenWorldGeneratorModule.h"
#include "OpenWorldGeneratorSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "Partition/OWGServerChunkManager.h"

OPENWORLDGENERATOR_API DEFINE_LOG_CATEGORY(LogOpenWorldGenerator);

void FOpenWorldGeneratorModule::StartupModule()
{
	// Debug HUD
	AHUD::OnShowDebugInfo.AddLambda( []( AHUD* HUD, UCanvas* Canvas, const FDebugDisplayInfo& DisplayInfo, float& YL, float& YPos )
	{
		if (const UOpenWorldGeneratorSubsystem* OpenWorldGeneratorSubsystem = UOpenWorldGeneratorSubsystem::Get( HUD ) )
		{
			if (const TScriptInterface<IOWGChunkManagerInterface> ChunkManager = OpenWorldGeneratorSubsystem->GetChunkManager() )
			{
				ChunkManager->DrawDebugHUD( HUD, Canvas, DisplayInfo );
			}
		}
	} );
}

void FOpenWorldGeneratorModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FOpenWorldGeneratorModule, OpenWorldGenerator)
