// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChunkCoord.h"
#include "UObject/Interface.h"
#include "OWGChunkManagerInterface.generated.h"

class AOWGChunk;

/** Possible results of a chunk existence check */
UENUM( BlueprintType )
enum class EChunkExists : uint8
{
	/** It's not known whenever the chunk exists or not. Returned on the client when the chunk is not loaded */
	Unknown,
	/** Chunk does not exist */
	DoesNotExist,
	/** Chunk does exist */
	ChunkExists
};

/**
 * This interface allows operating on the chunks regardless of what side we are running on (e.g. Client or Server)
 * For Client, certain functionality will not be available. For example, the clients cannot load chunks from the underlying storage themselves,
 * and their ChunkExists checks are limited to the chunks currently available to the client.
 */
UINTERFACE( BlueprintType, NotBlueprintable )
class OPENWORLDGENERATOR_API UOWGChunkManagerInterface : public UInterface
{
	GENERATED_BODY()
};

class OPENWORLDGENERATOR_API IOWGChunkManagerInterface
{
	GENERATED_BODY()
public:
	/** Finds a loaded chunk at the given coordinates */
	UFUNCTION( BlueprintCallable, Category = "Chunk Manager" )
	virtual AOWGChunk* FindChunk( const FChunkCoord& ChunkCoord ) const = 0;

	/** Checks if the given chunk exists. Might check the region file existence on the disk. */
	UFUNCTION( BlueprintCallable, Category = "Chunk Manager" )
	virtual EChunkExists DoesChunkExistSync( const FChunkCoord& ChunkCoord ) const = 0;

	/** Attempts to load the given chunk and returns the loaded chunk if succeeded, or nullptr. Will not attempt to generate a chunk */
	UFUNCTION( BlueprintCallable, Category = "Chunk Manager" )
	virtual AOWGChunk* LoadChunk( const FChunkCoord& ChunkCoord ) = 0;

	/** Attempts to load, or, if the chunk does not exist, create the chunk */
	UFUNCTION( BlueprintCallable, Category = "Chunk Manager" )
	virtual AOWGChunk* LoadOrCreateChunk( const FChunkCoord& ChunkCoord ) = 0;

	/** Gives the chunk manager an opportunity to draw Debug HUD */
	virtual void DrawDebugHUD( class AHUD* HUD, class UCanvas* Canvas, const class FDebugDisplayInfo& DisplayInfo ) {}
private:
	friend class AOWGChunk;
	friend class UOpenWorldGeneratorSubsystem;

	virtual void Initialize() = 0;
	virtual void BeginPlay() = 0;
	virtual void Deinitialize() = 0;
	virtual void Tick( float DeltaTime ) = 0;

	/** Called by chunk actors when they BeginPlay, both on Client and Server. How chunk came to be (loaded or created) does not matter in this context */
	virtual void NotifyChunkBegunPlay( AOWGChunk* Chunk ) {};
	/** Called by the chunk actors when they are destroyed, both on Client and Server */
	virtual void NotifyChunkDestroyed( AOWGChunk* Chunk ) {};
	/** Requests the chunk in question to be generated until it's ProcessChunkGeneration returns false. Returns true if the chunk is already at the given generation stage */
	virtual void RequestChunkGeneration( AOWGChunk* Chunk ) = 0;
};
