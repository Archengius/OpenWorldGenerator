// Copyright Nikita Zolotukhin. All Rights Reserved.

#include "Generation/OWGChunkBiomeGenerator.h"
#include "Generation/OWGBiome.h"
#include "Misc/ScopeExit.h"
#include "Partition/OWGChunk.h"

UOWGChunkBiomeGenerator::UOWGChunkBiomeGenerator()
{
}

bool UOWGChunkBiomeGenerator::AdvanceChunkGeneration_Implementation()
{
	AOWGChunk* Chunk = GetChunk();

	// Bail out early if we do not have a valid biome source
	if ( BiomeSource == nullptr )
	{
		UE_LOG( LogChunkGenerator, Warning, TEXT("BiomeGenerator '%s' does not have a valid Biome Source!"), *GetPathName() );
		return true;
	}

	TArray<UOWGNoiseIdentifier*> NoiseLayout;
	TArray<UOWGBiome*> BiomeIndexMappings;
	const FBiomeLookupFunc BiomeLookup = BiomeSource->CreateBiomeLookup( NoiseLayout, BiomeIndexMappings );

	// Exit early if biome lookup failed to reference a single biome
	if ( BiomeIndexMappings.IsEmpty() )
	{
		UE_LOG( LogChunkGenerator, Warning, TEXT("BiomeGenerator '%s' failed to generate biome placement because Biome Source '%s' did not provide a single biome!"),
			*GetPathName(), *BiomeSource.GetObject()->GetPathName() );
		return true;
	}

	const int32 NoiseResolutionXY = Chunk->GetWorldGeneratorDefinition()->NoiseResolutionXY;
	const int32 ElementStrafe = NoiseLayout.Num();

	const int32 CombinedBufferSizeBytes = NoiseResolutionXY * NoiseResolutionXY * ElementStrafe * sizeof(float);

	// Allocate the buffer, zero it out, and free it once we go out of scope
	float* CombinedNoiseDataBuffer = (float*) FMemory::Malloc( CombinedBufferSizeBytes );
	FMemory::Memzero( CombinedNoiseDataBuffer, CombinedBufferSizeBytes );
	ON_SCOPE_EXIT {
		FMemory::Free( CombinedNoiseDataBuffer );
	};

	// Populate combined noise data buffer for each cell
	for ( int32 NoiseIndex = 0; NoiseIndex < NoiseLayout.Num(); NoiseIndex++ )
	{
		const FChunkData2D* NoiseData = Chunk->FindRawNoiseData( NoiseLayout[ NoiseIndex ] );
		if ( NoiseData == nullptr ) continue;
		const float* RawNoiseDataPtr = NoiseData->GetDataPtr<float>();
		
		for ( int32 ElementIndex = 0; ElementIndex < NoiseResolutionXY * NoiseResolutionXY; ElementIndex++ )
		{
			CombinedNoiseDataBuffer[ ElementIndex * ElementStrafe + NoiseIndex ] = RawNoiseDataPtr[ ElementIndex ];
		}
	}

	// Call biome lookup function for each cell, utilizing the combined data buffer for fast noise data lookup
	TSet<int32> BiomeIndicesUsedInChunk;
	int32* GlobalCellBiomePaletteBuffer = (int32*) FMemory::Malloc( NoiseResolutionXY * NoiseResolutionXY * sizeof(int32) );
	ON_SCOPE_EXIT {
		FMemory::Free( GlobalCellBiomePaletteBuffer );
	};
	
	for ( int32 ElementIndex = 0; ElementIndex < NoiseResolutionXY * NoiseResolutionXY; ElementIndex++ )
	{
		const float* CellNoiseData = &CombinedNoiseDataBuffer[ ElementIndex * ElementStrafe ];
		int32 BiomeIndex = BiomeLookup( CellNoiseData );

		// Remap invalid biome index to the first biome in the map to avoid crashes down the line
		// First global index is always a valid one because we check at the entry of the biome generation that the biome index map is not empty
		if ( BiomeIndex == INDEX_NONE )
		{
			BiomeIndex = 0;
		}

		// Add biome index to the buffer and also to the set of biomes used in this chunk
		GlobalCellBiomePaletteBuffer[ ElementIndex ] = BiomeIndex;
		BiomeIndicesUsedInChunk.Add( BiomeIndex );
	}

	// Build a chunk's biome palette from the list of the biomes present in the chunk
	TArray<UOWGBiome*> ChunkProtoBiomePalette;
	FBiomePaletteIndex* GlobalBiomeIndexToPaletteIndexMap = (FBiomePaletteIndex*) FMemory::Malloc( BiomeIndexMappings.Num() * sizeof(FBiomePaletteIndex) );
	ON_SCOPE_EXIT {
		FMemory::Free( GlobalBiomeIndexToPaletteIndexMap );
	};

	// Make sure biome palette index does not overflow
	checkf( BiomeIndicesUsedInChunk.Num() < MAX_BIOMES_PER_CHUNK, TEXT("Biome palette index overflow: %d biomes in chunk while only %d are supported. Please change FBiomePaletteIndex to a larger type!"), BiomeIndicesUsedInChunk.Num(), MAX_BIOMES_PER_CHUNK );

	for ( const int32 GlobalBiomeIndex : BiomeIndicesUsedInChunk )
	{
		const int32 LocalBiomePaletteIndex = ChunkProtoBiomePalette.Add( BiomeIndexMappings[ GlobalBiomeIndex ] );
		GlobalBiomeIndexToPaletteIndexMap[ GlobalBiomeIndex ] = (FBiomePaletteIndex) LocalBiomePaletteIndex;
	}

	// Initialize chunk's biome palette, and copy the global biome data into the palette indices
	FChunkBiomePalette ChunkBiomePalette( ChunkProtoBiomePalette );
	// Biome map does not support interpolation, even though Lerp is defined for FBiomePaletteIndex
	FChunkData2D ChunkBiomeMap = FChunkData2D::Create<FBiomePaletteIndex>( NoiseResolutionXY, false );

	FBiomePaletteIndex* RawBiomeDataPtr = ChunkBiomeMap.GetMutableDataPtr<FBiomePaletteIndex>();
	for ( int32 ElementIndex = 0; ElementIndex < NoiseResolutionXY * NoiseResolutionXY; ElementIndex++ )
	{
		RawBiomeDataPtr[ ElementIndex ] = GlobalBiomeIndexToPaletteIndexMap[ GlobalCellBiomePaletteBuffer[ ElementIndex ] ];
	}

	Chunk->InitializeChunkBiomePalette( MoveTemp( ChunkBiomePalette ), MoveTemp( ChunkBiomeMap ) );
	return true;
}
