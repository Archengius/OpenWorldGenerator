// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OWGChunkGenerator.h"
#include "OWGNoiseGenerator.h"
#include "OWGChunkSurfaceGenerator.generated.h"

class UOWGChunkLandscapeLayer;

/**
 * First pass of the chunk generation, this generator populates the heightmap of the chunk surface using a collection of noise generators
 * It also lays out the biome mapping across the chunk and does the initial layer population for the biomes
 */
UCLASS()
class OPENWORLDGENERATOR_API UOWGChunkSurfaceGenerator : public UOWGChunkGenerator
{
	GENERATED_BODY()
public:
	UOWGChunkSurfaceGenerator();

	// Begin OWGChunkGenerator interface
	virtual bool AdvanceChunkGeneration_Implementation() override;
	// End OWGChunkGenerator interface
protected:
	/** Base noise to use to generate the surface */
	UPROPERTY( EditAnywhere, Category = "Surface Generator" )
	FOWGNoiseReference BaseNoise;

	/** Additional noise to stack up on top of the base one to get the final terrain height */
	UPROPERTY( EditAnywhere, Category = "Surface Generator" )
	TArray<FOWGNoiseReference> OverlayNoise;

	/** Default layer to pre-fill the landscape with in case the biome does not specify a valid layer, or no valid biome was selected */
	UPROPERTY( EditAnywhere, Category = "Surface Generator" )
	UOWGChunkLandscapeLayer* DefaultLandscapeLayer;
};
