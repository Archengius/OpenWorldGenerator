// Copyright Nikita Zolotukhin. All Rights Reserved.

#include "Partition/ChunkLandscapeWeight.h"
#include "Rendering/OWGChunkLandscapeLayer.h"

UOWGChunkLandscapeLayer* FChunkLandscapeWeightMapDescriptor::GetLayerDescriptor( int32 InLayerIndex ) const
{
	return LandscapeLayers.IsValidIndex( InLayerIndex ) ? LandscapeLayers[ InLayerIndex ] : nullptr;
}

int32 FChunkLandscapeWeightMapDescriptor::FindLayerIndex( UOWGChunkLandscapeLayer* InLayer ) const
{
	return LandscapeLayers.IndexOfByKey( InLayer );
}

int32 FChunkLandscapeWeightMapDescriptor::FindOrCreateLayer( UOWGChunkLandscapeLayer* InLayer )
{
	const int32 ExistingLayerIndex = FindLayerIndex( InLayer );
	if ( ExistingLayerIndex != INDEX_NONE )
	{
		return ExistingLayerIndex;
	}

	if ( LandscapeLayers.Num() + 1 < FChunkLandscapeWeight::MaxWeightMapLayers )
	{
		check( InLayer );
		return LandscapeLayers.Add( InLayer );
	}
	return INDEX_NONE;
}

int32 FChunkLandscapeWeightMapDescriptor::CreateLayerChecked( UOWGChunkLandscapeLayer* InLayer )
{
	const int32 LayerIndex = FindOrCreateLayer( InLayer );
	check( LayerIndex != INDEX_NONE );
	return LayerIndex;
}

void FChunkLandscapeWeightMapDescriptor::AddReferencedObjects( FReferenceCollector& ReferenceCollector )
{
	ReferenceCollector.AddStableReferenceArray( &LandscapeLayers );
}

void FChunkLandscapeWeightMapDescriptor::Serialize( FArchive& Ar )
{
	Ar << LandscapeLayers;
}
