// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Partition/ChunkCoord.h"

THIRD_PARTY_INCLUDES_START
#include "FastNoise/Generators/Generator.h"
THIRD_PARTY_INCLUDES_END

#include "OWGNoiseGenerator.generated.h"

class UCurveFloat;
class AOWGChunk;

/** Allows identifying a noise generator in other parts of the chunk generator while allowing the flexibility of swapping it out for a different one */
UCLASS()
class OPENWORLDGENERATOR_API UOWGNoiseIdentifier : public UDataAsset
{
	GENERATED_BODY()
public:
	/** Display name of this noise type, for debugging purposes */
	UPROPERTY( EditAnywhere, Category = "Noise Identifier" )
	FText DebugName;

	/** True if this noise should replicate to the clients. Only the noise that visually affects the chunk needs to be replicated. */
	UPROPERTY( EditAnywhere, Category = "Noise Identifier" )
	bool bReplicates{false};

	/** The name under which this noise should be exposed to the Procedural Content Generation framework as a metadata for each point */
	UPROPERTY( EditAnywhere, Category = "Noise Identifier" )
	FName PCGMetadataAttributeName{NAME_None};

	/** When non-zero, the noise will be exposed to the landscape material as vertex colors on the landscape mesh. Since amount of channels is HEAVILY limited, only 1 or 2 noises should have this set */
	UPROPERTY( EditAnywhere, Category = "Noise Identifier" )
	int32 MaterialVertexColorIndex{INDEX_NONE};
};

/**
 * Generates the noise for a specific chunk
 */
UCLASS( Abstract )
class OPENWORLDGENERATOR_API UOWGNoiseGenerator : public UDataAsset
{
	GENERATED_BODY()
public:
	UOWGNoiseGenerator();

	/** Generates the noise of the given resolution for the particular chunk (using it's coordinates and world seed) */
	void GenerateNoise( int32 WorldSeed, const FChunkCoord& ChunkCoord, int32 HeightmapResolutionXY, float* OutNoiseData ) const;

protected:
	FastNoise::SmartNode<FastNoise::Generator> TransformGenerator( FastNoise::SmartNode<FastNoise::Generator> InGenerator ) const;

	/** Creates and configures the generator to use for generating the floor of this chunk */
	virtual FastNoise::SmartNode<FastNoise::Generator> CreateAndConfigureGenerator() const PURE_VIRTUAL( UOWGNoiseGenerator::CreateAndConfigureGenerator, return nullptr; )
protected:
	/** Offset of the noise over the X axis */
	UPROPERTY( EditAnywhere, Category = "Noise Generator|Offset" )
	float NoiseOffsetX;

	/** Offset of the noise over the Y axis */
	UPROPERTY( EditAnywhere, Category = "Noise Generator|Offset" )
	float NoiseOffsetY;
	
	/** Scale of the noise over the X axis */
	UPROPERTY( EditAnywhere, Category = "Noise Generator|Scale" )
	float NoiseScaleX;

	/** Scale of the noise over the Y axis */
	UPROPERTY( EditAnywhere, Category = "Noise Generator|Scale" )
	float NoiseScaleY;

	/** Frequency to use for the noise generator */
	UPROPERTY( EditAnywhere, Category = "Noise Generator" )
	float GeneratorFrequency;

	/** Number of octaves (iterations) of the base noise to apply. Must be >1 to enable fractal noise */
	UPROPERTY( EditAnywhere, Category = "Noise Generator|Fractal" )
	int32 NumOctaves;

	/** Lacunarity of the fractal noise. In simple terms, it's a scale to apply to the frequency on each iteration */
	UPROPERTY( EditAnywhere, Category = "Noise Generator|Fractal" )
	float Lacunarity;

	/** Gain of the fractal noise. In simple terms, it's a  scale to apply to the noise value on each iteration */
	UPROPERTY( EditAnywhere, Category = "Noise Generator|Fractal" )
	float Gain;
};

UCLASS()
class OPENWORLDGENERATOR_API UOWGPerlinNoiseGenerator : public UOWGNoiseGenerator
{
	GENERATED_BODY()

	// Begin UOWGNoiseGenerator interface
	virtual FastNoise::SmartNode<FastNoise::Generator> CreateAndConfigureGenerator() const override;
	// End UOWGNoiseGenerator interface
};

UCLASS()
class OPENWORLDGENERATOR_API UOWGConstantNoiseGenerator : public UOWGNoiseGenerator
{
	GENERATED_BODY()

	// Begin UOWGNoiseGenerator interface
	virtual FastNoise::SmartNode<FastNoise::Generator> CreateAndConfigureGenerator() const override;
	// End UOWGNoiseGenerator interface
protected:
	/** Constant value that this generator will provide */
	UPROPERTY( EditAnywhere, Category = "Noise Generator" )
	float ConstantValue;
};

/** A reference to an existing noise */
USTRUCT()
struct OPENWORLDGENERATOR_API FOWGNoiseReference
{
	GENERATED_BODY()

	/** Identifier of the noise to sample */
	UPROPERTY( EditAnywhere, Category = "Noise Reference" )
	UOWGNoiseIdentifier* NoiseIdentifier{nullptr};

	/** Curve used to remap the noise range to a different value range */
	UPROPERTY( EditAnywhere, Category = "Noise Reference" )
	UCurveFloat* RemapCurve{nullptr};

	/** Generates the noise for a particular chunk */
	void GenerateNoise( const AOWGChunk* Chunk, int32 HeightmapResolutionXY, float* OutNoiseData ) const;
};
