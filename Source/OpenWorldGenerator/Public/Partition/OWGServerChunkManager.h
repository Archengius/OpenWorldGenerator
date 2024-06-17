// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OWGChunkManagerInterface.h"
#include "OWGChunkStreamingProvider.h"
#include "OWGServerChunkManager.generated.h"

class OpenWorldGeneratorSubsystem;
class IOWGChunkStreamingProvider;
class UOWGRegionContainer;
class UOWGWorldGeneratorConfiguration;

DECLARE_LOG_CATEGORY_EXTERN( LogServerChunkManager, All, All );

enum class EOWGSaveGameVersion : uint32
{
	InitialVersion = 0,

	// Add new versions above this line
	LatestVersionPlusOne,
	LatestVersion = LatestVersionPlusOne - 1
};

UCLASS( Within = "OpenWorldGeneratorSubsystem" )
class OPENWORLDGENERATOR_API UOWGServerChunkManager : public UObject, public IOWGChunkManagerInterface
{
	GENERATED_BODY()
public:
	// Begin IOWGChunkManagerInterface
	virtual void Initialize() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void Deinitialize() override;
	virtual AOWGChunk* FindChunk(const FChunkCoord& ChunkCoord) const override;
	virtual EChunkExists DoesChunkExistSync(const FChunkCoord& ChunkCoord) const override;
	virtual AOWGChunk* LoadChunk(const FChunkCoord& ChunkCoord) override;
	virtual AOWGChunk* LoadOrCreateChunk(const FChunkCoord& ChunkCoord) override;
	virtual void RequestChunkGeneration(AOWGChunk* Chunk) override;
	virtual void DrawDebugHUD(AHUD* HUD, UCanvas* Canvas, const FDebugDisplayInfo& DisplayInfo) override;
	// End IOWGChunkManagerInterface

	/** Registers a streaming provider in the chunk manager */
	UFUNCTION( BlueprintCallable, Category = "Chunk Manager" )
	void RegisterStreamingProvider( const TScriptInterface<IOWGChunkStreamingProvider>& StreamingProvider );

	/** Un-registers a streaming provider in the chunk manager */
	UFUNCTION( BlueprintCallable, Category = "Chunk Manager" )
	void UnregisterStreamingProvider( const TScriptInterface<IOWGChunkStreamingProvider>& StreamingProvider );

	void SetRegionFolderPath(const FString& InRegionFolderPath);

	UOpenWorldGeneratorSubsystem* GetOwnerSubsystem() const;
protected:
	UOWGRegionContainer* LoadRegionContainerSync(const FChunkCoord& RegionCoord);
	UOWGRegionContainer* LoadOrCreateRegionContainerSync(const FChunkCoord& RegionCoord);

	void TickChunkStreaming( float DeltaTime );
	void TickChunkGeneration();

	FString GetFilenameForRegionCoord(const FChunkCoord& RegionCoord) const;
protected:
	/** A map of loaded regions in the world */
	UPROPERTY( Transient )
	TMap<FChunkCoord, UOWGRegionContainer*> LoadedRegions;

	/** A cache of region coordinate to the whenever it's file exists or not */
	mutable TMap<FChunkCoord, TArray<FChunkCoord>> UnloadedRegionExistenceCache;

	/** Currently registered chunk streaming providers */
	UPROPERTY( Transient )
	TArray<TScriptInterface<IOWGChunkStreamingProvider>> RegisteredStreamingProviders;

	/** Chunks that are currently being generated */
	UPROPERTY( Transient )
	TArray<AOWGChunk*> ChunksPendingGeneration;

	/** Folder where region container files will be saved, or loaded from */
	FString RegionFolderLocation;
};