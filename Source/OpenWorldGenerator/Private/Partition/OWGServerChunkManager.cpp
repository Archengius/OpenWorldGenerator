// Copyright Nikita Zolotukhin. All Rights Reserved.

#include "Partition/OWGServerChunkManager.h"
#include "DisplayDebugHelpers.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "OpenWorldGeneratorSettings.h"
#include "OpenWorldGeneratorSubsystem.h"
#include "Engine/Canvas.h"
#include "GameFramework/HUD.h"
#include "Generation/OWGNoiseGenerator.h"
#include "Generation/OWGWorldGeneratorConfiguration.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Partition/OWGChunk.h"
#include "Partition/OWGRegionContainer.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

DEFINE_LOG_CATEGORY( LogServerChunkManager );

static TAutoConsoleVariable CVarFreezeServerChunkStreaming(
	TEXT("owg.FreezeServerChunkStreaming"),
	false,
	TEXT("Whenever server chunk streaming should be frozen. When streaming is frozen, no chunks are streamed in or out. 1 = streaming frozen; 0 = streaming active (default)"),
	ECVF_Cheat
);

namespace OpenWorldGeneratorSaveGame
{
	static const TCHAR* SaveGameExtension = TEXT("owgsav");
	static constexpr int32 SaveGameMagic = 0x56534757;
}

void UOWGServerChunkManager::Initialize()
{
}

void UOWGServerChunkManager::BeginPlay()
{
	for ( const TSoftClassPtr<UObject>& StreamingProviderSoftClass : UOpenWorldGeneratorSettings::Get()->ChunkStreamingProviders )
	{
		const UClass* StreamingProviderClass = StreamingProviderSoftClass.LoadSynchronous();
		if ( StreamingProviderClass != nullptr && StreamingProviderClass->ImplementsInterface( UOWGChunkStreamingProvider::StaticClass() ) )
		{
			UObject* StreamingProvider = NewObject<UObject>( this, StreamingProviderClass, NAME_None, RF_Transient );
			RegisterStreamingProvider( StreamingProvider );
		}
	}
}

void UOWGServerChunkManager::Tick( float DeltaTime )
{
	if ( !CVarFreezeServerChunkStreaming.GetValueOnGameThread() )
	{
		TickChunkStreaming( DeltaTime );
	}
	TickChunkGeneration();
}

void UOWGServerChunkManager::Deinitialize()
{
	// Write all regions to the region files
	// TODO @open-world-generator: We should periodically write these files in background, and also cleanup regions that have been unloaded for a while
	if (!RegionFolderLocation.IsEmpty())
	{
		for (const TPair<FChunkCoord, UOWGRegionContainer*>& LoadedRegion : LoadedRegions)
		{
			const FString RegionFilename = GetFilenameForRegionCoord(LoadedRegion.Key);
			if (FArchive* WriterArchive = IFileManager::Get().CreateFileWriter(*RegionFilename))
			{
				LoadedRegion.Value->SerializeRegionContainerToFile(*WriterArchive);
			}
		}
	}
}

void UOWGServerChunkManager::RequestChunkGeneration( AOWGChunk* Chunk )
{
	check( Chunk );
	ChunksPendingGeneration.AddUnique( Chunk );
}

void UOWGServerChunkManager::TickChunkGeneration()
{
	// TRACE_CPUPROFILER_EVENT_SCOPE(UOWGServerChunkManager::TickChunkGeneration);

	// Sort by distance to the player
	ChunksPendingGeneration.StableSort( []( const AOWGChunk& A, const AOWGChunk& B )
	{
		return A.DistanceToClosestStreamingSource > B.DistanceToClosestStreamingSource;
	} );
	
	for ( int32 i = ChunksPendingGeneration.Num() - 1; i >= 0; i-- )
	{
		if ( !IsValid( ChunksPendingGeneration[ i ] ) || !ChunksPendingGeneration[ i ]->ProcessChunkGeneration() )
		{
			ChunksPendingGeneration.RemoveAt( i );
		}
	}
}

void UOWGServerChunkManager::DrawDebugHUD( AHUD* HUD, UCanvas* Canvas, const FDebugDisplayInfo& DisplayInfo )
{
	const FChunkCoord PlayerChunkCoord = FChunkCoord::FromWorldLocation( HUD->GetOwningPawn()->GetActorLocation() );
	FDisplayDebugManager& DisplayDebugManager = Canvas->DisplayDebugManager;

	if ( DisplayInfo.IsDisplayOn( TEXT("OWG_ChunkLoading") ) )
	{
		const int32 DebugOverlaySize = 15;
		constexpr float ChunkElementSize = 30.0f;
		constexpr float ChunkElementSpacing = 4.0f;
		constexpr float ChunkElementLineThickness = 2.0f;

		DisplayDebugManager.DrawString( TEXT("OWG: Chunk Loading Visualization") );
		DisplayDebugManager.DrawString( TEXT("Red = Non-Existent; Yellow = Unloaded; Orange = Idle; Green = Loaded; Blue = Player Chunk") );
		DisplayDebugManager.GetYPosRef() += 5.0f;
		
		for ( int32 Y = 0; Y < DebugOverlaySize; Y++ )
		{
			constexpr float ElementSizePlusSpacing = ChunkElementSize + ChunkElementSpacing;
			for ( int32 X = 0; X < DebugOverlaySize; X++ )
			{
				const float XPos = DisplayDebugManager.GetXPos() + X * ElementSizePlusSpacing;
				const float YPos = DisplayDebugManager.GetYPos();
				const FChunkCoord LocalChunkCoord( PlayerChunkCoord.PosX + X - DebugOverlaySize / 2, PlayerChunkCoord.PosY + Y - DebugOverlaySize / 2 );

				FLinearColor ChunkColor = FLinearColor::Red;
				if ( const AOWGChunk* Chunk = FindChunk( LocalChunkCoord ) )
				{
					const bool bIsPlayerChunk = PlayerChunkCoord == LocalChunkCoord;
					ChunkColor = bIsPlayerChunk ? FLinearColor::Blue : ( Chunk->IsChunkIdle() ? FLinearColor( FColor::Orange ) : FLinearColor::Green );
				}
				else if ( DoesChunkExistSync( LocalChunkCoord ) == EChunkExists::ChunkExists )
				{
					ChunkColor = FLinearColor::Yellow;
				}
				Canvas->K2_DrawBox( FVector2D( XPos, YPos ), FVector2D( ChunkElementSize ), ChunkElementLineThickness, ChunkColor );
			}
			DisplayDebugManager.GetYPosRef() += ElementSizePlusSpacing;
		}
	}

	if ( DisplayInfo.IsDisplayOn( TEXT("OWG_ChunkLODs") ) )
	{
		const int32 DebugOverlaySize = 15;
		constexpr float ChunkElementSize = 30.0f;
		constexpr float ChunkElementSpacing = 4.0f;
		constexpr float ChunkElementLineThickness = 2.0f;

		DisplayDebugManager.DrawString( TEXT("OWG: Chunk LODs Visualization") );
		DisplayDebugManager.DrawString( TEXT("Red = LOD0; Yellow = LOD1; Blue = LOD2; Green = LOD3; ") );
		DisplayDebugManager.GetYPosRef() += 5.0f;
		const FLinearColor LODDisplayColors[] { FLinearColor::Red, FLinearColor::Yellow, FLinearColor::Blue, FLinearColor::Green };
		
		for ( int32 Y = 0; Y < DebugOverlaySize; Y++ )
		{
			constexpr float ElementSizePlusSpacing = ChunkElementSize + ChunkElementSpacing;
			for ( int32 X = 0; X < DebugOverlaySize; X++ )
			{
				const float XPos = DisplayDebugManager.GetXPos() + X * ElementSizePlusSpacing;
				const float YPos = DisplayDebugManager.GetYPos();
				const FChunkCoord LocalChunkCoord( PlayerChunkCoord.PosX + X - DebugOverlaySize / 2, PlayerChunkCoord.PosY + Y - DebugOverlaySize / 2 );

				FLinearColor ChunkColor = FLinearColor::Transparent;
				if ( const AOWGChunk* Chunk = FindChunk( LocalChunkCoord ) )
				{
					ChunkColor = LODDisplayColors[ FMath::Clamp( Chunk->CurrentChunkLOD, 0, UE_ARRAY_COUNT( LODDisplayColors ) - 1 ) ];
				}
				Canvas->K2_DrawBox( FVector2D( XPos, YPos ), FVector2D( ChunkElementSize ), ChunkElementLineThickness, ChunkColor );
			}
			DisplayDebugManager.GetYPosRef() += ElementSizePlusSpacing;
		}
	}

	if ( DisplayInfo.IsDisplayOn( TEXT("OWG_ChunkData") ) )
	{
		const FChunkCoord RegionCoord = PlayerChunkCoord.ToRegionCoord();
		const AOWGChunk* LoadedChunk = FindChunk( PlayerChunkCoord );

		DisplayDebugManager.DrawString( TEXT("OWG: Chunk Data") );
		DisplayDebugManager.DrawString( FString::Printf( TEXT("Chunk: %d,%d (%s)"), PlayerChunkCoord.PosX, PlayerChunkCoord.PosY, *GetNameSafe( LoadedChunk ) ) );
		DisplayDebugManager.DrawString( FString::Printf( TEXT("Region: %d,%d"), RegionCoord.PosX, RegionCoord.PosY ) );

		if ( LoadedChunk != nullptr )
		{
			LoadedChunk->DrawDebugHUD( HUD, Canvas, DisplayInfo );
		}
	}
}

void UOWGServerChunkManager::TickChunkStreaming( float DeltaTime )
{
	// TRACE_CPUPROFILER_EVENT_SCOPE(UOWGServerChunkManager::TickChunkStreaming);
	
	// Collect all streaming sources
	TArray<FChunkStreamingSource> StreamingSources;
	for ( const TScriptInterface<IOWGChunkStreamingProvider>& StreamingProvider : RegisteredStreamingProviders )
	{
		if ( StreamingProvider )
		{
			StreamingProvider->GetStreamingSources( StreamingSources );
		}
	}

	// Generate a list of chunks we want loaded
	TMap<FChunkCoord, FLoadedChunkInfo> ChunkToLoadToGeneratorStageMap;
	for ( const FChunkStreamingSource& ChunkStreamingSource : StreamingSources )
	{
		ChunkStreamingSource.GetLoadedChunkCoords( ChunkToLoadToGeneratorStageMap );
	}

	TArray<FChunkCoord> AllChunksThatShouldBeLoaded;
	ChunkToLoadToGeneratorStageMap.GenerateKeyArray( AllChunksThatShouldBeLoaded );

	TArray<FChunkCoord> CurrentlyLoadedRegions;
	LoadedRegions.GenerateKeyArray( CurrentlyLoadedRegions );

	// Unload individual chunks in currently loaded regions
	TMap<FChunkCoord, UOWGRegionContainer*> RegionsThatShouldBeLoaded;
	for ( const FChunkCoord& ChunkCoord : AllChunksThatShouldBeLoaded )
	{
		const FChunkCoord SectionCoord = ChunkCoord.ToRegionCoord();
		RegionsThatShouldBeLoaded.Add( SectionCoord, LoadOrCreateRegionContainerSync( SectionCoord ) );
	}

	// Unload chunks in the regions that still have chunks that should be loaded in them
	TSet<FChunkCoord> ChunksThatShouldBeUnloaded;
	for ( const TPair<FChunkCoord, UOWGRegionContainer*>& Pair : RegionsThatShouldBeLoaded )
	{
		for ( const FChunkCoord& ChunkCoord : Pair.Value->GetLoadedChunkCoords() )
		{
			if ( !ChunkToLoadToGeneratorStageMap.Contains( ChunkCoord ) )
			{
				ChunksThatShouldBeUnloaded.Add( ChunkCoord );
			}
		}
	}

	// Unload entire regions first in case no chunks are loaded in them
	for ( const FChunkCoord& LoadedRegionCoord : CurrentlyLoadedRegions )
	{
		if ( !RegionsThatShouldBeLoaded.Contains( LoadedRegionCoord ) )
		{
			UOWGRegionContainer* LoadedRegion = LoadedRegions.FindChecked( LoadedRegionCoord );
			ChunksThatShouldBeUnloaded.Append( LoadedRegion->GetLoadedChunkCoords() );
		}
	}

	// Load chunks that need to be loaded
	TArray<AOWGChunk*> ChunksThatHaveBeenLoaded;
	for ( const FChunkCoord& ChunkCoord : AllChunksThatShouldBeLoaded )
	{
		UOWGRegionContainer* RegionContainer = RegionsThatShouldBeLoaded.FindChecked( ChunkCoord.ToRegionCoord() );
		if ( AOWGChunk* Chunk = RegionContainer->LoadOrCreateChunk( ChunkCoord ) )
		{
			ChunksThatHaveBeenLoaded.Add( Chunk );
		}
	}

	// Generate the loaded chunks up to a required point
	for ( AOWGChunk* Chunk : ChunksThatHaveBeenLoaded )
	{
		const FLoadedChunkInfo LoadedChunkInfo = ChunkToLoadToGeneratorStageMap.FindChecked( Chunk->GetChunkCoord() );
		Chunk->ElapsedIdleTime = 0.0f;
		Chunk->bPendingToBeUnloaded = false;
		Chunk->RequestChunkGeneration( LoadedChunkInfo.GeneratorStage );
		Chunk->RequestChunkLOD( LoadedChunkInfo.ChunkLOD );
		Chunk->DistanceToClosestStreamingSource = LoadedChunkInfo.DistanceToChunk;
	}

	const float IdleTimeBeforeChunkUnload = UOpenWorldGeneratorSettings::Get()->ChunkUnloadIdleTime;

	// Unload the chunks that we no longer need loaded
	for ( const FChunkCoord& ChunkToUnload : ChunksThatShouldBeUnloaded )
	{
		UOWGRegionContainer* RegionContainer = LoadedRegions.FindChecked( ChunkToUnload.ToRegionCoord() );
		if ( AOWGChunk* LoadedChunk = RegionContainer->FindChunk( ChunkToUnload ) )
		{
			LoadedChunk->ElapsedIdleTime += DeltaTime;
			LoadedChunk->DistanceToClosestStreamingSource = UE_BIG_NUMBER;

			// Unload the chunk if it has elapsed it's idle time and we are not trying to defer it's unloading because we have some pending tasks
			LoadedChunk->bPendingToBeUnloaded = LoadedChunk->ElapsedIdleTime >= IdleTimeBeforeChunkUnload;

			if ( LoadedChunk->bPendingToBeUnloaded && !LoadedChunk->ShouldDeferChunkUnloading() )
			{
				UE_LOG( LogServerChunkManager, Log, TEXT("Unloading chunk '%s' at %d,%d because IdleTime has exceeded the threshold (%.2fs)"),
					*LoadedChunk->GetName(), ChunkToUnload.PosX, ChunkToUnload.PosY, IdleTimeBeforeChunkUnload );

				LoadedChunk->ElapsedIdleTime = 0.0f;
				RegionContainer->UnloadChunk( LoadedChunk->GetChunkCoord() );
			}
		}
	}
}

void UOWGServerChunkManager::RegisterStreamingProvider( const TScriptInterface<IOWGChunkStreamingProvider>& StreamingProvider )
{
	if ( StreamingProvider )
	{
		RegisteredStreamingProviders.Add( StreamingProvider );
	}
}

void UOWGServerChunkManager::UnregisterStreamingProvider( const TScriptInterface<IOWGChunkStreamingProvider>& StreamingProvider )
{
	RegisteredStreamingProviders.Remove( StreamingProvider );
}

AOWGChunk* UOWGServerChunkManager::FindChunk( const FChunkCoord& ChunkCoord ) const
{
	if ( const UOWGRegionContainer* const* RegionContainer = LoadedRegions.Find( ChunkCoord.ToRegionCoord() ) )
	{
		return (*RegionContainer)->FindChunk( ChunkCoord );
	}
	return nullptr;
}

EChunkExists UOWGServerChunkManager::DoesChunkExistSync( const FChunkCoord& ChunkCoord ) const
{
	const FChunkCoord RegionCoord = ChunkCoord.ToRegionCoord();
	
	// Check if there is a loaded region container
	if ( const UOWGRegionContainer* const* RegionContainer = LoadedRegions.Find(RegionCoord) )
	{
		if ( (*RegionContainer)->ChunkExists( ChunkCoord ) )
		{
			return EChunkExists::ChunkExists;
		}
		return EChunkExists::DoesNotExist;
	}
	// Check if there is a region file that we have not loaded yet. We can check for file existence, and then read chunk list from the header quickly without decompressing the data
	if (!RegionFolderLocation.IsEmpty() && !UnloadedRegionExistenceCache.Contains(RegionCoord))
	{
		TArray<FChunkCoord> ResultChunkListForRegion;
		const FString RegionFilename = GetFilenameForRegionCoord(RegionCoord);

		if (IFileManager::Get().FileExists(*RegionFilename))
		{
			if (FArchive* RegionFileReader = IFileManager::Get().CreateFileReader(*RegionFilename))
			{
				UOWGRegionContainer::ReadRegionContainerChunkListFromFile(*RegionFileReader, ResultChunkListForRegion);
				delete RegionFileReader;
			}
		}
		UnloadedRegionExistenceCache.Add(RegionCoord, ResultChunkListForRegion);
	}
	// Check the region file cache, now that we have potentially populated it
	if (const TArray<FChunkCoord>* UnloadedRegionChunkList = UnloadedRegionExistenceCache.Find(RegionCoord))
	{
		return UnloadedRegionChunkList->Contains(ChunkCoord) ? EChunkExists::ChunkExists : EChunkExists::DoesNotExist;
	}
	// There is no loaded region container, and no region file, so the chunk does not exist
	return EChunkExists::DoesNotExist;
}

FString UOWGServerChunkManager::GetFilenameForRegionCoord(const FChunkCoord& RegionCoord) const
{
	return FPaths::Combine(RegionFolderLocation, FString::Printf(TEXT("%d_%d.owgr"), RegionCoord.PosX, RegionCoord.PosY));
}

AOWGChunk* UOWGServerChunkManager::LoadChunk( const FChunkCoord& ChunkCoord )
{
	if ( UOWGRegionContainer* RegionContainer = LoadRegionContainerSync( ChunkCoord.ToRegionCoord() ) )
	{
		return RegionContainer->LoadChunk( ChunkCoord );
	}
	return nullptr;
}

AOWGChunk* UOWGServerChunkManager::LoadOrCreateChunk( const FChunkCoord& ChunkCoord )
{
	if ( UOWGRegionContainer* RegionContainer = LoadOrCreateRegionContainerSync( ChunkCoord.ToRegionCoord() ) )
	{
		return RegionContainer->LoadOrCreateChunk( ChunkCoord );
	}
	return nullptr;
}

UOWGRegionContainer* UOWGServerChunkManager::LoadRegionContainerSync(const FChunkCoord& RegionCoord)
{
	// Attempt to find an existing container
	if ( UOWGRegionContainer* const* ExistingContainer = LoadedRegions.Find( RegionCoord ) )
	{
		return *ExistingContainer;
	}
	
	const FString RegionFilename = GetFilenameForRegionCoord(RegionCoord);

	if (IFileManager::Get().FileExists(*RegionFilename))
	{
		if (FArchive* RegionFileReader = IFileManager::Get().CreateFileReader(*RegionFilename))
		{
			UOWGRegionContainer* NewRegionContainer = NewObject<UOWGRegionContainer>(this);
			NewRegionContainer->LoadRegionContainerFromFile(*RegionFileReader);

			LoadedRegions.Add(RegionCoord, NewRegionContainer);
			UnloadedRegionExistenceCache.Remove(RegionCoord);
			return NewRegionContainer;
		}
	}
	return nullptr;
}

UOWGRegionContainer* UOWGServerChunkManager::LoadOrCreateRegionContainerSync( const FChunkCoord& ChunkCoord )
{
	// Attempt to find an existing container
	if ( UOWGRegionContainer* const* ExistingContainer = LoadedRegions.Find( ChunkCoord ) )
	{
		return *ExistingContainer;
	}
	// Attempt to load the region container from the file system
	if (UOWGRegionContainer* LoadedContainer = LoadRegionContainerSync(ChunkCoord))
	{
		return LoadedContainer;
	}

	// Create a new region container now
	FActorSpawnParameters SpawnParameters{};
	SpawnParameters.Name = *FString::Printf( TEXT("OWGRegionContainer_%d_%d"), ChunkCoord.PosX, ChunkCoord.PosY );
	
	SpawnParameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
	SpawnParameters.bDeferConstruction = true;

	UOWGRegionContainer* NewRegionContainer = NewObject<UOWGRegionContainer>(this);
	check( NewRegionContainer );

	NewRegionContainer->SetRegionCoord( ChunkCoord );
	LoadedRegions.Add( ChunkCoord, NewRegionContainer );
	UnloadedRegionExistenceCache.Remove(ChunkCoord);

	return NewRegionContainer;
}

UOpenWorldGeneratorSubsystem* UOWGServerChunkManager::GetOwnerSubsystem() const
{
	return CastChecked<UOpenWorldGeneratorSubsystem>( GetOuter() );
}

void UOWGServerChunkManager::SetRegionFolderPath(const FString& InRegionFolderPath)
{
	RegionFolderLocation = InRegionFolderPath;
}
