// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

class UOWGChunkLandscapeLayer;

/** A singular weight map point for the chunk landscape */
struct OPENWORLDGENERATOR_API FChunkLandscapeWeight
{
	static constexpr int32 MaxWeightMapLayers = 16;

	/** Weights of all of the individual layers */
	uint8 LayerWeights[MaxWeightMapLayers]{};

	/** Returns the index of the layer with the largest contribution */
	FORCEINLINE_DEBUGGABLE int32 GetLayerWithLargestContribution() const
	{
		int32 LargestContributionLayer = 0;
		uint8 LargestContributionValue = LayerWeights[ LargestContributionLayer ];

		for ( int32 LayerIndex = 1; LayerIndex < MaxWeightMapLayers; LayerIndex++ )
		{
			if ( LayerWeights[ LayerIndex ] > LargestContributionValue )
			{
				LargestContributionLayer = LayerIndex;
				LargestContributionValue = LayerWeights[ LayerIndex ];
			}
		}
		return LargestContributionLayer;
	}

	/** Returns the sum of all of the entries in the weight map */
	FORCEINLINE_DEBUGGABLE int32 GetTotalWeight() const
	{
		// Undefined weights should always be zero, and therefore safe to add to the total weight
		int32 ResultWeight = 0;
		for ( int32 LayerIndex = 0; LayerIndex < MaxWeightMapLayers; LayerIndex++ )
		{
			ResultWeight += LayerWeights[ LayerIndex ];
		}
		return ResultWeight;
	}

	/** Returns normalized weights for the defined layers in the weight map entry. OutNormalizedWeights should be an array with at least NumLayers entries */
	FORCEINLINE_DEBUGGABLE void GetNormalizedWeights( float* OutNormalizedWeights ) const
	{
		const int32 TotalWeight = GetTotalWeight();
		for ( int32 LayerIndex = 0; LayerIndex < MaxWeightMapLayers; LayerIndex++ )
		{
			OutNormalizedWeights[ LayerIndex ] = TotalWeight == 0 ? 0.0f : ( LayerWeights[ LayerIndex ] * 1.0f / TotalWeight );
		}
	}

	/** Returns the normalized weight for a specific layer. Not very efficient, use GetNormalizedWeights instead where applicable */
	FORCEINLINE_DEBUGGABLE float GetNormalizedWeight( int32 LayerIndex ) const
	{
		const int32 TotalWeight = GetTotalWeight();
		return TotalWeight == 0 ? 0 : ( LayerWeights[ LayerIndex ] * 1.0f / TotalWeight );
	}

	/** Makes the given layer have the normalized weight value of NewWeight. NewWeight is a normalized absolute weight of this layer where 1 = full blend (no other layers) and 0 = no layer */
	FORCEINLINE_DEBUGGABLE void SetNormalizedWeight( int32 LayerIndex, float NewWeight, int32 NumLayers )
	{
		// Calculate total weight excluding current layer, and the new weight value for the current layer
		const int32 CurrentTotalWeight = GetTotalWeight() - LayerWeights[ LayerIndex ];
		const uint8 QuantizedNewWeightForCurrentLayer = (uint8) FMath::Clamp( FMath::RoundToInt32( NewWeight * 255.0f ), 0, 255 );

		// New total weight should be able to be fully satisfied by one channel. That means, it should be 255 or less
		constexpr int32 NewTotalWeight = 255;
		const int32 QuantizedTotalWeightForOtherLayers = NewTotalWeight - QuantizedNewWeightForCurrentLayer;

		// Go over every channel and adjust their absolute value
		for ( int32 OtherLayerIndex = 0; OtherLayerIndex < NumLayers; OtherLayerIndex++ )
		{
			// Do not re-scale the layer that we have fixed
			if ( OtherLayerIndex == LayerIndex )
			{
				LayerWeights[OtherLayerIndex] = QuantizedNewWeightForCurrentLayer;
				continue;
			}
			// Scale old relative weight to the new total weight
			const float QuantizedNewWeight = LayerWeights[OtherLayerIndex] * 1.0f / CurrentTotalWeight * QuantizedTotalWeightForOtherLayers;
			LayerWeights[OtherLayerIndex] = (uint8) FMath::Clamp( FMath::RoundToInt32( QuantizedNewWeight ), 0, 255 );
		}
	}

	/** Applies the absolute weight value to the given layer index */
	FORCEINLINE_DEBUGGABLE void SetAbsoluteWeight( int32 LayerIndex, uint8 NewAbsoluteWeight )
	{
		LayerWeights[LayerIndex] = NewAbsoluteWeight;
	}
};

/** Landscape weight is a POD type */
template<>
struct TIsPODType<FChunkLandscapeWeight> { enum { Value = true }; };

/** Lerp implementation for landscape weights. Chunk Data interpolation functions require Lerp to be supported by the underlying data type to be used */
template<>
struct TCustomLerp<FChunkLandscapeWeight>
{
	// Required to make FMath::Lerp<FChunkLandscapeWeight>() call our custom Lerp() implementation below.
	constexpr static bool Value = true;

	template<class U>
	static FORCEINLINE_DEBUGGABLE FChunkLandscapeWeight Lerp(const FChunkLandscapeWeight& A, const FChunkLandscapeWeight& B, const U& Alpha)
	{
		// We need to normalize the weights first to be able to Lerp them
		float NormalizedWeightsA[FChunkLandscapeWeight::MaxWeightMapLayers];
		float NormalizedWeightsB[FChunkLandscapeWeight::MaxWeightMapLayers];

		A.GetNormalizedWeights( NormalizedWeightsA );
		B.GetNormalizedWeights( NormalizedWeightsB );

		// Lerp each layer weight separately and quantize them to our valid value range after that
		FChunkLandscapeWeight ResultWeight;
		for ( int32 LayerIndex = 0; LayerIndex < FChunkLandscapeWeight::MaxWeightMapLayers; LayerIndex++ )
		{
			const float InterpolatedWeight = FMath::Lerp( NormalizedWeightsA[ LayerIndex ], NormalizedWeightsB[ LayerIndex ], Alpha );
			ResultWeight.LayerWeights[ LayerIndex ] = (uint8) FMath::Clamp( FMath::RoundToInt32( InterpolatedWeight * 255 ), 0, 255 );
		}
		return ResultWeight;
	}
};

/** A map of weights for the chunk and their layout in memory and on the textures */
class OPENWORLDGENERATOR_API FChunkLandscapeWeightMapDescriptor
{
	/** Names of the layers in this chunk's weight map, index of the layer mapping to it's index in the layer weights */
	TArray<UOWGChunkLandscapeLayer*> LandscapeLayers;
public:
	FORCEINLINE TArray<UOWGChunkLandscapeLayer*> GetAllLayers() const { return LandscapeLayers; }
	FORCEINLINE int32 GetNumLayers() const { return LandscapeLayers.Num(); }

	/** Returns the descriptor for the layer by it's index. Returns nullptr if the index is not valid */
	UOWGChunkLandscapeLayer* GetLayerDescriptor( int32 InLayerIndex ) const;

	/** Returns index for the given layer, or INDEX_NONE if it is not currently in the weight map */
	int32 FindLayerIndex( UOWGChunkLandscapeLayer* InLayer ) const;

	/** Returns the index in the weight map for the given layer, or allocates it and returns the newly created index. Can return INDEX_NONE if the layer is not possible to be created because the maximum number of layers in the chunk has been reached */
	int32 FindOrCreateLayer( UOWGChunkLandscapeLayer* InLayer );

	/** Same as FindOrCreateLayer, but asserts if it failed to create it */
	int32 CreateLayerChecked( UOWGChunkLandscapeLayer* InLayer );

	void AddReferencedObjects( FReferenceCollector& ReferenceCollector );
	void Serialize( FArchive& Ar );

	friend FArchive& operator<<( FArchive& Ar, FChunkLandscapeWeightMapDescriptor& WeightMapDescriptor )
	{
		WeightMapDescriptor.Serialize( Ar );
		return Ar;
	}
};