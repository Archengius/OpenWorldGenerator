// Copyright Nikita Zolotukhin. All Rights Reserved.

#include "Generation/OWGBiome.h"
#include "Generation/OWGNoiseGenerator.h"
#include "Generation/OWGChunkGenerator.h"

FBiomeLookupFunc UOWGBiome::CreateBiomeLookup( TArray<UOWGNoiseIdentifier*>& NoiseToIDMappings, TArray<UOWGBiome*>& BiomeToIDMappings )
{
	const int32 BiomeIndex = BiomeToIDMappings.AddUnique( this );
	return [BiomeIndex](const float*) -> int32 { return BiomeIndex; };
}

FBiomeLookupFunc UOWGBiomeTable::CreateBiomeLookup( TArray<UOWGNoiseIdentifier*>& NoiseToIDMappings, TArray<UOWGBiome*>& BiomeToIDMappings )
{
	// Return early in case we do not have a valid noise, or no rows to pick from
	if ( Noise == nullptr || Rows.IsEmpty() )
	{
		UE_LOG( LogChunkGenerator, Warning, TEXT("Biome Table is not correctly configured! It does not have a valid Noise reference, or it's rows are empty!"), *GetPathName() );
		return [](const float*) -> int32 { return INDEX_NONE; };
	}
	
	const int32 NoiseIndex = NoiseToIDMappings.AddUnique( Noise );
	TArray<TPair<float, FBiomeLookupFunc>> BiomeIndices;

	float LargestNoiseThreshold = 0.0f;
	for ( const FBiomeTableRow& BiomeTableRow : Rows )
	{
		FBiomeLookupFunc LookupFunc;
		if ( BiomeTableRow.Biome )
		{
			// Let the biome interface create the lookup function for itself
			LookupFunc = BiomeTableRow.Biome->CreateBiomeLookup( NoiseToIDMappings, BiomeToIDMappings );
		}
		else
		{
			// If we do not have a valid biome, fallback to the invalid biome index
			LookupFunc = [](const float*) -> int32 { return INDEX_NONE; };
		}
		const float Threshold = BiomeTableRow.NoiseThreshold;

		LargestNoiseThreshold = FMath::Max( LargestNoiseThreshold, Threshold );
		BiomeIndices.Add( { Threshold, MoveTemp( LookupFunc ) } );
	}

	// Print a warning in case the largest noise threshold does not cover the entire noise range
	if ( LargestNoiseThreshold < 1.0f )
	{
		UE_LOG( LogChunkGenerator, Warning, TEXT("Biome Table '%s' does not cover the full noise range for Noise '%s'. Only the [0;%.2f] range is covered, while the [0;1] range is expected!"),
			*GetPathName(), *GetPathNameSafe( Noise ), LargestNoiseThreshold );
	}
	
	return [NoiseIndex, BiomeIndices]( const float* PackedNoiseData ) -> int32
	{
		const float NoiseValue = PackedNoiseData[ NoiseIndex ];
		
		for ( int32 i = 0; i < BiomeIndices.Num() - 1; i++)
		{
			if ( BiomeIndices[ i ].Key >= NoiseValue )
			{
				return BiomeIndices[ i ].Value( PackedNoiseData );
			}
		}
		// None of the rows matched this noise value. That means the table definition did not cover the entire range. We have printed the warning and returning the last value in the list is fine
		return BiomeIndices[ BiomeIndices.Num() - 1 ].Value( PackedNoiseData );
	};
}

FChunkBiomePalette::FChunkBiomePalette( const TArray<UOWGBiome*>& InBiomeMappings ) : BiomeIndexMappings( InBiomeMappings )
{
	checkf( InBiomeMappings.Num() < MAX_BIOMES_PER_CHUNK, TEXT("Biome palette overflow: %d biomes out of %d supported"), InBiomeMappings.Num(), MAX_BIOMES_PER_CHUNK );
}

UOWGBiome* FChunkBiomePalette::GetBiomeByIndex( FBiomePaletteIndex BiomeIndex ) const
{
	return BiomeIndexMappings.IsValidIndex( BiomeIndex ) ? BiomeIndexMappings[ BiomeIndex ] : nullptr;
}

FBiomePaletteIndex FChunkBiomePalette::FindBiomeIndex( UOWGBiome* Biome ) const
{
	const int32 BiomeIndex = BiomeIndexMappings.Find( Biome );
	return BiomeIndex == INDEX_NONE ? BIOME_PALETTE_INDEX_NONE : static_cast<FBiomePaletteIndex>( BiomeIndex );
}

void FChunkBiomePalette::AddReferencedObjects( FReferenceCollector& ReferenceCollector )
{
	ReferenceCollector.AddStableReferenceArray( &BiomeIndexMappings );
}

void FChunkBiomePalette::Serialize( FArchive& Ar )
{
	Ar << BiomeIndexMappings;
}
