// Copyright Nikita Zolotukhin. All Rights Reserved.

#include "Generation/OWGChunkSurfaceGenerator.h"
#include "OpenWorldGeneratorSubsystem.h"
#include "Partition/ChunkLandscapeWeight.h"
#include "Partition/OWGChunk.h"
#include "Rendering/OWGChunkLandscapeLayer.h"

DECLARE_CYCLE_STAT( TEXT("ChunkSurfaceGenerator::AdvanceChunkGeneration"), STAT_ChunkSurfaceGenerator_AdvanceChunkGeneration, STATGROUP_Game );

UOWGChunkSurfaceGenerator::UOWGChunkSurfaceGenerator()
{
}

bool UOWGChunkSurfaceGenerator::AdvanceChunkGeneration_Implementation()
{
	SCOPE_CYCLE_COUNTER( STAT_ChunkSurfaceGenerator_AdvanceChunkGeneration );
	AOWGChunk* Chunk = GetChunk();

	const FChunkBiomePalette* BiomePalette = Chunk->GetBiomePalette();
	const FChunkData2D* ChunkBiomeMap = Chunk->FindRawChunkData( ChunkDataID::BiomeMap );

	const int32 HeightmapResolutionXY = Chunk->GetWorldGeneratorDefinition()->NoiseResolutionXY;
	FChunkData2D SurfaceHeightmap = FChunkData2D::Create<float>( HeightmapResolutionXY, true );

	float* SurfaceHeightmapData = SurfaceHeightmap.GetMutableDataPtr<float>();
	BaseNoise.GenerateNoise( Chunk, HeightmapResolutionXY, SurfaceHeightmapData );

	const int32 HeightmapTotalElementCount = HeightmapResolutionXY * HeightmapResolutionXY;
	TArray<float> TemporaryOverlayData;
	TemporaryOverlayData.AddUninitialized( HeightmapTotalElementCount );

	for ( const FOWGNoiseReference& OverlayNoiseRef : OverlayNoise )
	{
		FMemory::Memzero( TemporaryOverlayData.GetData(), HeightmapTotalElementCount * sizeof(float) );
		OverlayNoiseRef.GenerateNoise( Chunk, HeightmapResolutionXY, TemporaryOverlayData.GetData() );

		for ( int32 i = 0; i < HeightmapTotalElementCount; i++ )
		{
			SurfaceHeightmapData[ i ] += TemporaryOverlayData[ i ];
		}
	}

	// Weight map resolution matches the resolution of the chunk and does not go beyond this chunk's boundaries
	const int32 WeightMapResolutionXY = Chunk->GetWorldGeneratorDefinition()->WeightMapResolutionXY;

	FChunkLandscapeWeightMapDescriptor SurfaceWeightMapDescriptor{};
	FChunkData2D SurfaceWeights = FChunkData2D::Create<FChunkLandscapeWeight>( WeightMapResolutionXY, true );

	// Fill the weight map with the desired fill layer. The ocean generator will replace the layer below sea level with sand or gravel
	FChunkLandscapeWeight* SurfaceWeightsData = SurfaceWeights.GetMutableDataPtr<FChunkLandscapeWeight>();
	TArray<int32> BiomeToSurfaceLayerMap;
	for ( UOWGBiome* Biome : BiomePalette->GetAllBiomes() )
	{
		UOWGChunkLandscapeLayer* BiomeLayer = Biome->GroundLayer ? Biome->GroundLayer : DefaultLandscapeLayer;
		BiomeToSurfaceLayerMap.Add( SurfaceWeightMapDescriptor.CreateLayerChecked( BiomeLayer ) );
	}
	const FBiomePaletteIndex* ChunkBiomeData = ChunkBiomeMap->GetDataPtr<FBiomePaletteIndex>();
	check( ChunkBiomeMap->GetSurfaceResolutionXY() == WeightMapResolutionXY );

	// Set the absolute weight. Since there are no other weights in the grid it is okay (and it is faster)
	for ( int32 ElementIndex = 0; ElementIndex < SurfaceWeights.GetSurfaceElementCount(); ElementIndex++ )
	{
		const int32 LayerIndex = BiomeToSurfaceLayerMap[ ChunkBiomeData[ ElementIndex ] ];
		SurfaceWeightsData[ElementIndex].SetAbsoluteWeight( LayerIndex, 255 );
	}

	// Emplace the newly generated surface heightmap and recalculate all of the surface data immediately.
	Chunk->InitializeChunkLandscape( MoveTemp( SurfaceWeightMapDescriptor ), MoveTemp( SurfaceHeightmap ), MoveTemp( SurfaceWeights ) );
	return true;
}


