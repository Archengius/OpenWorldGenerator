// Copyright Nikita Zolotukhin. All Rights Reserved.

#include "Rendering/ChunkTextureManager.h"
#include "Partition/ChunkData2D.h"
#include "Partition/ChunkLandscapeWeight.h"
#include "TextureResource.h"

DECLARE_CYCLE_STAT( TEXT("Chunk Weight Map Texture Update"), STAT_ChunkWeightMapTextureUpdate, STATGROUP_Game );

UChunkTextureManager::UChunkTextureManager()
{
}

void UChunkTextureManager::ReleasePooledTextures()
{
	// Release pooled weight map textures
	for ( UTexture2D* Texture : PooledWeightMapTextures )
	{
		Texture->ReleaseResource();
		Texture->MarkAsGarbage();
	}
	PooledWeightMapTextures.Empty();
}

UTexture2D* UChunkTextureManager::CreateWeightMapTexture( const FChunkData2D* WeightMap, int32 TextureIndex )
{
	const int32 WeightMapResolutionXY = WeightMap->GetSurfaceResolutionXY();

	// Generate the resulting texture array
	UTexture2D* Texture = RetainSurfaceLayersTexture( WeightMapResolutionXY );
	PartialUpdateWeightMap( Texture, TextureIndex, WeightMap, 0, 0, WeightMapResolutionXY, WeightMapResolutionXY, true );
	return Texture;
}

void UChunkTextureManager::PartialUpdateWeightMap( UTexture2D* WeightMapTexture, int32 TextureIndex, const FChunkData2D* WeightMap, int32 StartX, int32 StartY, int32 EndX, int32 EndY, bool bFullUpdate )
{
	SCOPE_CYCLE_COUNTER( STAT_ChunkWeightMapTextureUpdate );
	const int32 HeightmapResolutionXY = WeightMap->GetSurfaceResolutionXY();
	constexpr int32 NumChannelsPerTexture = 4;

	// TODO @open-world-generator: Support texture streaming for generated weight maps. Need to implement UTextureMipDataProviderFactory and add it to AssetUserData
	FTexturePlatformData* PlatformData = WeightMapTexture->GetPlatformData();
	FTexture2DMipMap* FirstMipMap = &PlatformData->Mips[0];

	// Clamp the start/end of the texture to fit into the mip map data
	const int32 ClampedStartX = FMath::Clamp( StartX, 0, FirstMipMap->SizeX - 1 );
	const int32 ClampedStartY = FMath::Clamp( StartY, 0, FirstMipMap->SizeY - 1 );
	const int32 ClampedEndX = FMath::Clamp( EndX, 0, FirstMipMap->SizeX - 1 );
	const int32 ClampedEndY = FMath::Clamp( EndY, 0, FirstMipMap->SizeY - 1 );
	
	// Generate first mip map data by sampling weights data in each cell
	const FChunkLandscapeWeight* LandscapeWeightsData = WeightMap->GetDataPtr<FChunkLandscapeWeight>();
	FColor* TextureDataArray = static_cast< FColor* >(FirstMipMap->BulkData.Lock( LOCK_READ_WRITE ));

	for ( int32 PosX = ClampedStartX; PosX <= ClampedEndX; PosX++ )
	{
		for ( int32 PosY = ClampedStartY; PosY <= ClampedEndY; PosY++ )
		{
			const FChunkLandscapeWeight& LandscapeWeight = LandscapeWeightsData[ HeightmapResolutionXY * PosY + PosX ];
			const int32 TotalLayersWeight = LandscapeWeight.GetTotalWeight();

			// Safety check against uninitialized landscape weights. We should never get these but try not to crash with 0 total weight
			if ( TotalLayersWeight == 0 ) continue;

			// Calculate the index in the texture bulk data: The organization is Slice (Z) -> Row (Y) -> Column (X)
			const int32 TextureDataIndex = PosY * FirstMipMap->SizeX + PosX;
			FColor& TextureData = TextureDataArray[ TextureDataIndex ];

			// Copy the data from the layers into the texture. Non-allocated layers are allowed to contain garbage, as they are not read by the material
			// That implies that the capacity of layer weights is a multiple of channel size (which we check above)
			TextureData.R = (uint8) FMath::DivideAndRoundNearest( LandscapeWeight.LayerWeights[ TextureIndex * NumChannelsPerTexture + 0 ] * 255, TotalLayersWeight );
			TextureData.G = (uint8) FMath::DivideAndRoundNearest( LandscapeWeight.LayerWeights[ TextureIndex * NumChannelsPerTexture + 1 ] * 255, TotalLayersWeight );
			TextureData.B = (uint8) FMath::DivideAndRoundNearest( LandscapeWeight.LayerWeights[ TextureIndex * NumChannelsPerTexture + 2 ] * 255, TotalLayersWeight );
			TextureData.A = (uint8) FMath::DivideAndRoundNearest( LandscapeWeight.LayerWeights[ TextureIndex * NumChannelsPerTexture + 3 ] * 255, TotalLayersWeight );
		}
	}

	// Allocate the buffer for UpdateTextureRegions if we are willing to make an update. We want an update if the render resource has already been created
	FColor* TextureUpdateRegionBuffer = nullptr;
	const FUpdateTextureRegion2D* UpdateTextureRegion2D = nullptr;

	if ( WeightMapTexture->GetResource() && !bFullUpdate )
	{
		const int32 UpdateRegionSizeX = ClampedEndX - ClampedStartX + 1;
		const int32 UpdateRegionSizeY = ClampedEndY - ClampedStartY + 1;

		TextureUpdateRegionBuffer = (FColor*) FMemory::Malloc( UpdateRegionSizeX * UpdateRegionSizeY * sizeof(FColor) );
		UpdateTextureRegion2D = new FUpdateTextureRegion2D( ClampedStartX, ClampedStartY, 0, 0, UpdateRegionSizeX, UpdateRegionSizeY );

		// Copy the data from the global mip map buffer to the local update buffer with limited size
		for ( int32 LocalX = 0; LocalX < UpdateRegionSizeX; LocalX++ )
		{
			for ( int32 LocalY = 0; LocalY < UpdateRegionSizeY; LocalY++ )
			{
				const int32 TextureDataIndex = ( ClampedStartY + LocalY ) * FirstMipMap->SizeX + ( ClampedStartX + LocalX );
				TextureUpdateRegionBuffer[ LocalY * UpdateRegionSizeX + LocalX ] = TextureDataArray[ TextureDataIndex ];
			}
		}
	}

	// Unlock the mip data now that we have finished potentially making a partial copy for UpdateTextureRegions
	FirstMipMap->BulkData.Unlock();

	// Create resource for the texture if we are doing a full update
	if ( bFullUpdate )
	{
		WeightMapTexture->UpdateResource();
	}

	// Call UpdateTextureRegions if we have valid data for it. It is an asynchronous operation so we will need to free the buffers once it's done
	if ( TextureUpdateRegionBuffer && UpdateTextureRegion2D )
	{
		WeightMapTexture->UpdateTextureRegions( 0, 1, UpdateTextureRegion2D, UpdateTextureRegion2D->Width * sizeof(FColor), sizeof(FColor), (uint8*) TextureUpdateRegionBuffer, [](uint8* SrcData, const FUpdateTextureRegion2D* Regions)
		{
			// SrcData was allocated via FMemory::Malloc, region descriptor was allocated via new (single new, not vector new)
			FMemory::Free( SrcData );
			delete Regions;
		});
	}
}

void UChunkTextureManager::ReleaseSurfaceLayersTexture( UTexture2D* WeightMapTexture )
{
	check( IsInGameThread() );

	// Add texture back to the pool
	PooledWeightMapTextures.Add( WeightMapTexture );
}

UTexture2D* UChunkTextureManager::RetainSurfaceLayersTexture( int32 WeightMapResolutionXY )
{
	check( IsInGameThread() );

	// Attempt to re-use texture from the pool first
	if ( !PooledWeightMapTextures.IsEmpty() )
	{
		UTexture2D* RetainedTexture = PooledWeightMapTextures.Pop();
		check( RetainedTexture->GetSizeX() == WeightMapResolutionXY && RetainedTexture->GetSizeY() == WeightMapResolutionXY );

		return RetainedTexture;
	}

	// Create a new texture if we found to retain one from the pool
	const FName TextureName( TEXT("OWGWeightMapTexture"), SurfaceLayersTextureCounter++ );
	return UTexture2D::CreateTransient( WeightMapResolutionXY, WeightMapResolutionXY, PF_B8G8R8A8, TextureName );
}
