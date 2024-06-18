// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Partition/ChunkCoord.h"
#include "OWGRegionContainer.generated.h"

class AOWGChunk;

enum class ERegionContainerVersion : uint32
{
	InitialVersion = 0,

	// Add new versions above this line
	LatestPlusOne,
	Latest = LatestPlusOne - 1,
};

DECLARE_DELEGATE_OneParam( FOnChunkLoadedDelegate, AOWGChunk* /** ChunkOrNullptr */ );
DECLARE_DELEGATE_TwoParams( FOnChunkGeneratedDelegate, AOWGChunk* /* GeneratedChunk */, bool /** bChunkLoaded */ );

/**
 * Region container is a container for a segment of the world consisting of a grid of chunks
 * Each segment is saved into a separate file and is only streamed in on demand. That means,
 * only a limited set of regions will be loaded at a particular point in the world.
 */
UCLASS()
class OPENWORLDGENERATOR_API UOWGRegionContainer : public UObject
{
	GENERATED_BODY()
public:
	/** Returns the region/section coordinate of this container */
	FORCEINLINE FChunkCoord GetRegionCoord() const { return RegionCoord; }

	/**
	 * Attempts to find a chunk using the given chunk coordinate as a key
	 * This will NOT generate the chunk as the process of chunk generator is async,
	 * if you would like to generate or load the chunk you should call 
	 */
	AOWGChunk* FindChunk( FChunkCoord ChunkCoord ) const;

	/**
	 * Attempts to load the given chunk from the underlying serialized data
	 * This will NOT generate the chunk if it does not exist!
	 */
	AOWGChunk* LoadChunk( FChunkCoord ChunkCoord );

	/**
	 * First attempts to load, and then to generate a chunk if it is not found
	 * The generated chunk will not be populated or fully generated, instead, you are implied to manually
	 * call RequestChunkGeneration on the resulting chunk object and provide the stage which should be reached
	 */
	AOWGChunk* LoadOrCreateChunk( FChunkCoord ChunkCoord );

	/** Unloads a specific chunk at the given coordinates */
	void UnloadChunk( FChunkCoord ChunkCoord );

	/**
	 * Returns true if the chunk at the given chunk coordinates exists, either in a loaded form or in a serialized form
	 */
	bool ChunkExists( FChunkCoord ChunkCoord ) const;

	/** Serializes the data contained inside of this container into the file. */
	void SerializeRegionContainerToFile(FArchive& Ar);

	/** Loads this region container's data from the file */
	void LoadRegionContainerFromFile(FArchive& Ar);

	/** Parses the header of the region file to gather the list of chunks contained in it */
	static bool ReadRegionContainerChunkListFromFile(FArchive& Ar, TArray<FChunkCoord>& OutChunkList);

	/** Updates the coordinate of this region. Must be called prior to BeginPlay */
	void SetRegionCoord( const FChunkCoord& NewRegionCoord );

	/** Returns the coordinates of the already loaded chunks */
	TArray<FChunkCoord> GetLoadedChunkCoords() const;
protected:
	friend class AOWGChunk;

	/** Called by the chunk to notify it has been destroyed */
	void NotifyChunkDestroyed( const AOWGChunk* Chunk );
private:
	/** Coordinate of the section this container holds */
	FChunkCoord RegionCoord;

	/** Binary blobs for each chunk serialized as a part of this region */
	TMap<FChunkCoord, TArray<uint8>> SerializedChunkData;

	/** A Map of loaded chunks that have been deserialized from the container */
	UPROPERTY()
	TMap<FChunkCoord, TObjectPtr<AOWGChunk>> LoadedChunks;
};
