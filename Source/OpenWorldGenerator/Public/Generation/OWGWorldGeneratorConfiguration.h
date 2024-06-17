// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "OWGWorldGeneratorConfiguration.generated.h"

enum class EChunkGeneratorStage : uint8;

class UOWGChunkGenerator;
class UOWGNoiseIdentifier;
class UOWGNoiseGenerator;
class UMaterialInterface;
class AVolume;

/** A simple struct to hold a list of generators for each stage */
USTRUCT()
struct OPENWORLDGENERATOR_API FChunkGeneratorArray
{
	GENERATED_BODY()

	/** A list of all generators that should run for the provided stage. Order matters! */
	UPROPERTY( EditAnywhere, Category = "Open World Generator" )
	TArray<TSubclassOf<UOWGChunkGenerator>> Generators;
};

USTRUCT()
struct OPENWORLDGENERATOR_API FLandscapeMaterialDesc
{
	GENERATED_BODY()

	/** Material with Solid blend mode, used for base biome and when the chunk has a single biome */
	UPROPERTY( EditAnywhere, Category = "Open World Generator" )
	TSoftObjectPtr<UMaterialInterface> SolidMaterial;

	/** Material with Cutout blend mode, used for blended in biomes in chunks on higher LODs */
	UPROPERTY( EditAnywhere, Category = "Open World Generator" )
	TSoftObjectPtr<UMaterialInterface> CutoutMaterial;

	/** Material used for smooth blending between biomes */
	UPROPERTY( EditAnywhere, Category = "Open World Generator" )
	TSoftObjectPtr<UMaterialInterface> TranslucentMaterial;

	friend bool operator==( const FLandscapeMaterialDesc& A, const FLandscapeMaterialDesc& B )
	{
		return A.SolidMaterial == B.SolidMaterial && A.CutoutMaterial == B.CutoutMaterial && A.TranslucentMaterial == B.TranslucentMaterial;
	}
};

/**
 * World Generator Definition defines the global world chunk generator settings
 */
UCLASS( BlueprintType )
class OPENWORLDGENERATOR_API UOWGWorldGeneratorConfiguration : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	UOWGWorldGeneratorConfiguration();

	/** Resolution of the noise map generated for each chunk. Should be a Power Of Two to allow generating Landscape LODs. */
	UPROPERTY( EditAnywhere, BlueprintReadOnly, Category = "Open World Generator|Base" )
	int32 NoiseResolutionXY;

	/** Resolution of the weight map used for painting materials onto the chunk's surface */
	UPROPERTY( EditAnywhere, BlueprintReadOnly, Category = "Open World Generator|Base" )
	int32 WeightMapResolutionXY;

	/** Noise generators for the world generators to be used */
	UPROPERTY( EditAnywhere, Category = "Open World Generator|Base" )
	TMap<UOWGNoiseIdentifier*, UOWGNoiseGenerator*> NoiseGenerators;

	/** Chunk generator definitions for each stage */
	UPROPERTY( EditAnywhere, Category = "Open World Generator|Base" )
	TMap<EChunkGeneratorStage, FChunkGeneratorArray> ChunkGenerators;

	/** Default material for the chunk landscape, when the biome does not specify an override */
	UPROPERTY( EditAnywhere, Category = "Open World Generator|Materials" )
	FLandscapeMaterialDesc DefaultLandscapeMaterial;

	/** Maximum steepness of the landscape that the material/PCG systems can differentiate */
	UPROPERTY( EditAnywhere, BlueprintReadOnly, Category = "Open World Generator|Base" )
	float MaxLandscapeSteepness;
};
