// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OWGWorldGeneratorConfiguration.h"
#include "Engine/DataAsset.h"
#include "UObject/Interface.h"
#include "OWGBiome.generated.h"

class UOWGChunkLandscapeLayer;
class UOWGChunkGenerator;
class UOWGBiome;
class AOWGChunk;
class UOWGNoiseIdentifier;

/** Input: Array of noise values for this grid cell. Output: local ID of the biome */

/**
 * A prototype for a function that is called for each cell in the chunk to determine the biome it should have
 * 
 * @param PackedNoiseData input, contains noise data for the cell in order defined by NoiseToIDMappings
 * @return the ID of the biome that the cell should have. ID is a lookup into BiomeToIDMappings
 */
typedef TFunction<int32(const float* PackedNoiseData)> FBiomeLookupFunc;

UINTERFACE()
class OPENWORLDGENERATOR_API UOWGBiomeSourceInterface : public UInterface
{
	GENERATED_BODY()
};

class OPENWORLDGENERATOR_API IOWGBiomeSourceInterface
{
	GENERATED_BODY()
public:
	/**
	 * Creates a biome lookup function for this biome interface
	 */
	virtual FBiomeLookupFunc CreateBiomeLookup(TArray<UOWGNoiseIdentifier*>& NoiseToIDMappings, TArray<UOWGBiome*>& BiomeToIDMappings ) = 0;
};

/**
 * A biome is a grouped collection of the chunk generators covering a particular area of the world
 */
UCLASS()
class OPENWORLDGENERATOR_API UOWGBiome : public UDataAsset, public IOWGBiomeSourceInterface
{
	GENERATED_BODY()
public:
	/** Name of the biome visible to the player. Also used for debug purposes */
	UPROPERTY( EditAnywhere, Category = "Biome" )
	FText DisplayName;

	/** Landscape layer that the ground should be painted to. If not set, the fallback is used */
	UPROPERTY( EditAnywhere, Category = "Biome" )
	UOWGChunkLandscapeLayer* GroundLayer;

	/** Color tint to apply to the grass layer in this biome */
	UPROPERTY( EditAnywhere, Category = "Biome" )
	FLinearColor GrassColor{FLinearColor::White};

	/** Chunk generators that should be run when this biome is present in the chunk */
	UPROPERTY( EditAnywhere, Category = "Biome" )
	TMap<EChunkGeneratorStage, FChunkGeneratorArray> ChunkGenerators;

	/** The name under which this layer should be exposed to the Procedural Content Generation framework as a metadata for each point using the Filter Target Biomes node */
	UPROPERTY( EditAnywhere, Category = "Biome" )
	FName PCGMetadataAttributeName;

	/** Landscape material used for this biome. If not set, the default landscape material for the world will be used */
	UPROPERTY( EditAnywhere, Category = "Biome" )
	FLandscapeMaterialDesc LandscapeMaterial;

	// Begin IOWGBiomeSourceInterface
	virtual FBiomeLookupFunc CreateBiomeLookup(TArray<UOWGNoiseIdentifier*>& NoiseToIDMappings, TArray<UOWGBiome*>& BiomeToIDMappings) override;
	// End IOWGBiomeSourceInterface
};

/** A single entry in the biome table */
USTRUCT()
struct OPENWORLDGENERATOR_API FBiomeTableRow
{
	GENERATED_BODY()

	/** The noise threshold for this biome to be selected from the table. If the noise value is below the threshold, this entry is selected */
	UPROPERTY( EditAnywhere, Category = "Biome Table Row" )
	float NoiseThreshold{1.0f};

	/** The biome that this entry represents, or a pointer to the next biome table in the chain */
	UPROPERTY( EditAnywhere, Category = "Biome Table Row" )
	TScriptInterface<IOWGBiomeSourceInterface> Biome;
};

/** Biome table allows conditional lookup of the biome based on the noise data in the chunk */
UCLASS()
class OPENWORLDGENERATOR_API UOWGBiomeTable : public UDataAsset, public IOWGBiomeSourceInterface
{
	GENERATED_BODY()
public:
	/** Noise identifier for the noise this table is mapping */
	UPROPERTY( EditAnywhere, Category = "Biome Table" )
	UOWGNoiseIdentifier* Noise;

	/** Rows that are checked in their order of definition to pick a biome for the table. Rows are evaluated sequentially from the start to the end of the array */
	UPROPERTY( EditAnywhere, Category = "Biome Table" )
	TArray<FBiomeTableRow> Rows;
	
	// Begin IOWGBiomeSourceInterface
	virtual FBiomeLookupFunc CreateBiomeLookup(TArray<UOWGNoiseIdentifier*>& NoiseToIDMappings, TArray<UOWGBiome*>& BiomeToIDMappings) override;
	// End IOWGBiomeSourceInterface
};

typedef uint8 FBiomePaletteIndex;
static constexpr int32 MAX_BIOMES_PER_CHUNK = UINT8_MAX;
static constexpr FBiomePaletteIndex BIOME_PALETTE_INDEX_NONE = static_cast<FBiomePaletteIndex>(-1);

/** Describes a list of all biomes present in the chunk and their mappings to the local palette indices */
class OPENWORLDGENERATOR_API FChunkBiomePalette
{
	/** Mapping of the biome to it's local ID in the chunk biome map */
	TArray<TObjectPtr<UOWGBiome>> BiomeIndexMappings;
public:
	FChunkBiomePalette() = default;
	explicit FChunkBiomePalette( const TArray<UOWGBiome*>& InBiomeMappings );

	/** Returns all biomes present in the chunk's palette */
	FORCEINLINE TArray<UOWGBiome*> GetAllBiomes() const { return BiomeIndexMappings; }
	FORCEINLINE int32 NumBiomeMappings() const { return BiomeIndexMappings.Num(); }

	/** Returns the biome at the provided index. Returns nullptr if the index is invalid */
	UOWGBiome* GetBiomeByIndex( FBiomePaletteIndex BiomeIndex ) const;
	/** Finds the index that corresponds to the given biome. Returns BIOME_PALETTE_INDEX_NONE if the biome is not in the chunk */
	FBiomePaletteIndex FindBiomeIndex( UOWGBiome* Biome ) const;

	void AddReferencedObjects( FReferenceCollector& ReferenceCollector );
	void Serialize( FArchive& Ar );

	friend FArchive& operator<<( FArchive& Ar, FChunkBiomePalette& BiomeMapDescriptor )
	{
		BiomeMapDescriptor.Serialize( Ar );
		return Ar;
	}
};
