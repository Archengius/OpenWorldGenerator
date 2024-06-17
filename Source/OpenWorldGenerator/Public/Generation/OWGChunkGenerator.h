// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "OWGChunkGenerator.generated.h"

class AOWGChunk;
class UOWGChunkGenerator;
class UOWGBiome;

DECLARE_LOG_CATEGORY_EXTERN( LogChunkGenerator, All, All );

/** Various stages for chunk generators */
UENUM( BlueprintType )
enum class EChunkGeneratorStage : uint8
{
	/** The stage at which the generation starts */
	Initial,
	/** Surface generation. Generates surfaces in chunk, such as the water plane and the floor surface */
	Surface,
	/** Generates additional terrain on top of the surface or the floor in the chunk, such as the cliffs or boulders */
	Terrain,
	/** Decoration phase */
	Decoration,
	/** Feature generation phase. Individual features can be generated here */
	Features,
	
	// Add new generator stages above this line
	LatestPlusOne,
	Latest = LatestPlusOne - 1
};

/**
 * Base class for all chunk generators. Chunk generators modify the state of the chunks by placing objects, altering elevation level or layers,
 * or generating structures.
 * 
 * Chunk generation happens in stages so that the chunks can end up in not fully generated stages when persisted.
 * Chunk generator objects are saved and persisted until their generation is finished, as it may spin up for multiple frames.
 * Such generators can benefit from using blueprint functions of execution control. But be wary that the state of the execution will not persist across chunk reloads.
 * If one cannot possibly achieve the persistence of the chunk generator state (for example, because the state is managed by an external subsystem),
 * the chunk generator should override CanPersistChunkGenerator and return false. Keep in mind that this will not prevent the chunk generator from being forcibly saved
 * if the serialization cannot be delayed (for example, when doing a global save on exit). In such cases, the generator can override NotifyAboutToUnloadChunk and do some last resort cleanup there
 */
UCLASS( Blueprintable, Within = "OWGChunk", Abstract )
class OPENWORLDGENERATOR_API UOWGChunkGenerator : public UObject
{
	GENERATED_BODY()
public:
	UOWGChunkGenerator();

	// Begin UObject interface
	virtual UWorld* GetWorld() const override;
	// End UObject interface

	/** Returns the chunk that this generator is generating for */
	UFUNCTION( BlueprintPure, Category = "Chunk Generator" )
	AOWGChunk* GetChunk() const;

	/** Called each tick to advance chunk generation. Return true if the generation is finished and can pass to the next generator, false if it still happening */
	UFUNCTION( BlueprintNativeEvent, Category = "Chunk Generator" )
	bool AdvanceChunkGeneration();

	/** Called after the chunk generator returns true in AdvanceChunkGeneration to notify that it is done and will be destroyed shortly after */
	UFUNCTION( BlueprintNativeEvent, Category = "Chunk Generator" )
	void EndChunkGeneration();

	/** Return true if this chunk generator can be safely persisted. Returning false will delay the chunk persistence if possible (in some cases it is not possible to delay it) */
	UFUNCTION( BlueprintNativeEvent, Category = "Chunk Generator" )
	bool CanPersistChunkGenerator() const;

	/** Called when we are about to unload the chunk. Gives the chunk generator the last chance to cleanup resources that cannot be persisted */
	UFUNCTION( BlueprintNativeEvent, Category = "Chunk Generator" )
	void NotifyAboutToUnloadChunk();

	/** Schedules the generation of the chunks adjacent to this one in the given radius up to the provided stage. Returns true if chunks are generated, false if the generation is still pending */
	UFUNCTION( BlueprintCallable, Category = "Chunk Generator" )
	bool WaitForAdjacentChunkGeneration( EChunkGeneratorStage TargetStage, int32 Range = 1 );

	/** The biomes that resulted in this chunk generator being selected for generation. A list is a union of all biomes that have this chunk generator listed in their generators */
	UPROPERTY( VisibleInstanceOnly, BlueprintReadOnly, Category = "Chunk Generator" )
	TArray<UOWGBiome*> TargetBiomes;
};
