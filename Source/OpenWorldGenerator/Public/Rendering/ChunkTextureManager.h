// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2DArray.h"
#include "UObject/Object.h"
#include "ChunkTextureManager.generated.h"

class UTexture2D;
class UTexture2DArray;
class FChunkLandscapeWeightMap;
class FChunkData2D;
class FChunkLandscapeWeightMapDescriptor;

/** Manages texture pooling and allocation/population for chunks */
UCLASS()
class OPENWORLDGENERATOR_API UChunkTextureManager : public UObject
{
	GENERATED_BODY()
public:
	UChunkTextureManager();

	/** Releases all pooled textures immediately */
	void ReleasePooledTextures();

	/** Creates a weight map texture for the given weight map and surface layers. Might re-use one of the textures in the pool */
	UTexture2D* CreateWeightMapTexture( const FChunkData2D* WeightMap, int32 WeightMapIndex );

	/** Performs a partial update of the data on the given weight map texture */
	static void PartialUpdateWeightMap( UTexture2D* WeightMapTexture, int32 TextureIndex, const FChunkData2D* WeightMap, int32 StartX, int32 StartY, int32 EndX, int32 EndY, bool bFullUpdate = false );

	/** Releases the previously created surface layers texture back into the pool */
	void ReleaseSurfaceLayersTexture( UTexture2D* WeightMapTexture );
protected:
	/** Attempts to retain the weight map texture from the pool, or creates a new one */
	UTexture2D* RetainSurfaceLayersTexture( int32 WeightMapResolutionXY );

	/** Pooled weight map textures available to be re-claimed */
	UPROPERTY( Transient )
	TArray<TObjectPtr<UTexture2D>> PooledWeightMapTextures;
	
	/** Counter for how many weight map textures we have created */
	int32 SurfaceLayersTextureCounter{0};
};
