// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MaterialTypes.h"
#include "Generation/OWGWorldGeneratorConfiguration.h"

class UTexture2D;
class AOWGChunk;
class UMaterialInstance;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UChunkTextureManager;
class UAssetUserData;
class UChunkLandscapeMaterialUserData;
class UOWGChunkLandscapeLayer;
class FChunkLandscapeMaterialManager;
class UOWGBiome;

struct FLandscapeLayerParameterData
{
	FMaterialParameterInfo WeightMapTexture;
	FMaterialParameterInfo WeightMapChannelMask;
	FMaterialParameterInfo GrassColor;
	bool bIsBackgroundLayer{false};

	void PopulateMetadataFromLayer( const UMaterialInterface* BaseMaterial, int32 BlendLayerIndex );
};

class OPENWORLDGENERATOR_API FChunkBiomeLandscapeMaterial : public FNoncopyable
{
public:
	explicit FChunkBiomeLandscapeMaterial( FChunkLandscapeMaterialManager* InParentManager, UOWGBiome* InBiome );

	/** Retrieves the currently active material instance */
	UMaterialInstance* GetMaterialInstance( bool bCreate = true );

	void ReleaseMaterialInstance();
	void RebindTexturesToMaterialParameters();

	void AddReferencedObjects( FReferenceCollector& ReferenceCollector );
private:
	void CreateNewMaterialInstance();
protected:
	FChunkLandscapeMaterialManager* ParentManager;

	/** Dynamic material instances generated for the landscape (per biome) */
	UMaterialInstanceDynamic* MaterialInstance{};
	/** Biome we are based on */
	UOWGBiome* Biome;

	TMap<UOWGChunkLandscapeLayer*, FLandscapeLayerParameterData> LayerToBlendTextureNameAndChannelMaskParameters;
};

/** Holds landscape material instances */
class OPENWORLDGENERATOR_API FChunkLandscapeMaterialManager : public FNoncopyable
{
public:
	explicit FChunkLandscapeMaterialManager( AOWGChunk* InChunk, UChunkTextureManager* InChunkTextureManager );

	void OnChunkLODLevelChanged();
	/** Performs a partial weight map update */
	void PartialUpdateWeightMap( int32 StartX, int32 StartY, int32 EndX, int32 EndY );

	void ReleaseTextures();
	void AddReferencedObjects( FReferenceCollector& ReferenceCollector );
protected:
	void RegenerateTextures();

	friend class FChunkBiomeLandscapeMaterial; 
	/** The chunk owning this material manager */
	AOWGChunk* OwnerChunk{};

	/** Texture holding the weight map data for the chunk. Textures are automatically added as needed to support new layers and dynamically updated */ 
	TArray<UTexture2D*> WeightMapTextures;

	TArray<FChunkBiomeLandscapeMaterial> PerBiomeMaterials;

	/** Cached chunk texture manager */
	UChunkTextureManager* ChunkTextureManager{};
};
