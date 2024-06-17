// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OWGChunkGenerator.h"
#include "OWGChunkBiomeGenerator.generated.h"

class IOWGBiomeSourceInterface;

/**
 * First pass of the chunk generation, this generator lays out the biome placement across the chunk using the biome grid
 */
UCLASS()
class OPENWORLDGENERATOR_API UOWGChunkBiomeGenerator : public UOWGChunkGenerator
{
	GENERATED_BODY()
public:
	UOWGChunkBiomeGenerator();

	// Begin OWGChunkGenerator interface
	virtual bool AdvanceChunkGeneration_Implementation() override;
	// End OWGChunkGenerator interface
protected:
	/** Biome source that will determine the biome placement within the chunk */
	UPROPERTY( EditAnywhere, Category = "Biome Generator" )
	TScriptInterface<IOWGBiomeSourceInterface> BiomeSource;
};
