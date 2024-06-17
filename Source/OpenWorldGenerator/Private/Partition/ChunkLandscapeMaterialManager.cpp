// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "Partition/ChunkLandscapeMaterialManager.h"
#include "Components/DynamicMeshComponent.h"
#include "Generation/OWGWorldGeneratorConfiguration.h"
#include "Materials/Material.h"
#include "Partition/OWGChunk.h"
#include "Rendering/ChunkTextureManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/ChunkLandscapeMaterialBindings.h"

DECLARE_CYCLE_STAT( TEXT("Chunk Material Instance Generation"), STAT_ChunkMaterialGeneration, STATGROUP_Game );

DEFINE_LOG_CATEGORY_STATIC( LogChunkLandscapeMaterialManager, All, All );

FChunkLandscapeMaterialManager::FChunkLandscapeMaterialManager( AOWGChunk* InChunk, UChunkTextureManager* InChunkTextureManager ) : OwnerChunk( InChunk ), ChunkTextureManager( InChunkTextureManager )
{
}

void FLandscapeLayerParameterData::PopulateMetadataFromLayer( const UMaterialInterface* BaseMaterial, int32 BlendLayerIndex )
{
	// If we can retrieve the parameter value, the parameter is defined on the material
	const TCHAR* GrassColorParameterName = TEXT("GrassColor");
	FLinearColor OutDefaultGrassColor;
	if ( BaseMaterial->GetVectorParameterValue( GrassColorParameterName, OutDefaultGrassColor, false ) )
	{
		GrassColor = FMaterialParameterInfo( GrassColorParameterName, LayerParameter, BlendLayerIndex );
	}
}

void FChunkLandscapeMaterialManager::OnChunkLODLevelChanged()
{
	// Set material instance on the mesh
	if ( OwnerChunk->LandscapeMeshComponent )
	{
		// Make sure textures are up to date
		RegenerateTextures();
		
		for ( int32 MaterialIndex = 0; MaterialIndex < PerBiomeMaterials.Num(); MaterialIndex++ )
		{
			UMaterialInstance* NewMaterialInstance = PerBiomeMaterials[ MaterialIndex ].GetMaterialInstance( true );
			OwnerChunk->LandscapeMeshComponent->SetMaterial( MaterialIndex, NewMaterialInstance );
		}
	}
}

void FChunkLandscapeMaterialManager::PartialUpdateWeightMap( int32 StartX, int32 StartY, int32 EndX, int32 EndY )
{
	const FChunkData2D& WeightMapLayers = OwnerChunk->ChunkData2D.FindChecked( ChunkDataID::SurfaceWeights );

	// Update existing weight map textures
	for ( int32 TextureIndex = 0; TextureIndex < WeightMapTextures.Num(); TextureIndex++ )
	{
		UTexture2D* WeightMapTexture = WeightMapTextures[ TextureIndex ];
		ChunkTextureManager->PartialUpdateWeightMap( WeightMapTexture, TextureIndex, &WeightMapLayers, StartX, StartY, EndX, EndY );
	}

	// Create new weight map textures if needed, if we already have a valid material instance
	if ( !WeightMapTextures.IsEmpty() )
	{
		RegenerateTextures();
	}
}

void FChunkLandscapeMaterialManager::RegenerateTextures()
{
	// Make sure we have enough weight map textures
	constexpr int32 ChannelsPerTexture = 4;
	const FChunkLandscapeWeightMapDescriptor* ChunkLandscapeWeightMap = OwnerChunk->GetWeightMapDescriptor();

	const int32 ExpectedNumberOfTextures = FMath::DivideAndRoundUp( ChunkLandscapeWeightMap->GetNumLayers(), ChannelsPerTexture );
	if ( ExpectedNumberOfTextures > WeightMapTextures.Num() )
	{
		const FChunkData2D* WeightMapLayers = OwnerChunk->FindRawChunkData( ChunkDataID::SurfaceWeights );

		for ( int32 NewTextureIndex = WeightMapTextures.Num(); NewTextureIndex < ExpectedNumberOfTextures; NewTextureIndex++ )
		{
			WeightMapTextures.Add( ChunkTextureManager->CreateWeightMapTexture( WeightMapLayers, NewTextureIndex ) );
		}
	}

	// Populate per biome materials once our list of biomes becomes available. Biome placement should never change once the chunk is generated.
	const TArray<UOWGBiome*> ChunkBiomes = OwnerChunk->GetBiomePalette()->GetAllBiomes();
	if ( PerBiomeMaterials.IsEmpty() && !ChunkBiomes.IsEmpty() )
	{
		for ( UOWGBiome* Biome : ChunkBiomes )
		{
			PerBiomeMaterials.Emplace( this, Biome );
		}
	}

	// Re-bind new textures to the materials
	for ( FChunkBiomeLandscapeMaterial& BiomeLandscapeMaterial : PerBiomeMaterials )
	{
		BiomeLandscapeMaterial.RebindTexturesToMaterialParameters();
	}
}

void FChunkLandscapeMaterialManager::ReleaseTextures()
{
	for ( UTexture2D* WeightMapTexture : WeightMapTextures )
	{
		ChunkTextureManager->ReleaseSurfaceLayersTexture( WeightMapTexture );
	}
	WeightMapTextures.Empty();

	for ( FChunkBiomeLandscapeMaterial& BiomeMaterial : PerBiomeMaterials )
	{
		BiomeMaterial.ReleaseMaterialInstance();
	}
}

FChunkBiomeLandscapeMaterial::FChunkBiomeLandscapeMaterial( FChunkLandscapeMaterialManager* InParentManager, UOWGBiome* InBiome ) : ParentManager( InParentManager ), Biome( InBiome )
{
}

void FChunkBiomeLandscapeMaterial::ReleaseMaterialInstance()
{
	if ( MaterialInstance != nullptr )
	{
		MaterialInstance->MarkAsGarbage();
		MaterialInstance = nullptr;
	}
}

void FChunkBiomeLandscapeMaterial::AddReferencedObjects( FReferenceCollector& ReferenceCollector )
{
	ReferenceCollector.AddStableReference( &Biome );
	ReferenceCollector.AddStableReferenceMap( LayerToBlendTextureNameAndChannelMaskParameters );
}

UMaterialInstance* FChunkBiomeLandscapeMaterial::GetMaterialInstance( bool bCreate )
{
	if ( MaterialInstance == nullptr && bCreate )
	{
		CreateNewMaterialInstance();
	}
	return MaterialInstance;
}

void FChunkBiomeLandscapeMaterial::CreateNewMaterialInstance()
{
	SCOPE_CYCLE_COUNTER( STAT_ChunkMaterialGeneration );

	// Delete old material instance
	if ( MaterialInstance != nullptr )
	{
		MaterialInstance->MarkAsGarbage();
		MaterialInstance = nullptr;
	}

	// Load the base material, exit if we failed
	UMaterialInterface* BaseMaterial = Biome->LandscapeMaterial.SolidMaterial.LoadSynchronous();
	if ( BaseMaterial == nullptr )
	{
		BaseMaterial = ParentManager->OwnerChunk->GetWorldGeneratorDefinition()->DefaultLandscapeMaterial.SolidMaterial.LoadSynchronous();
	}
	MaterialInstance = UMaterialInstanceDynamic::Create( BaseMaterial, ParentManager->OwnerChunk, *FString::Printf( TEXT("LandscapeMaterial_%s"), *Biome->GetName() ) );

	// Retrieve material layers for our selected base material
	FMaterialLayersFunctions MaterialLayersFunctions;
	if ( !BaseMaterial->GetMaterialLayers( MaterialLayersFunctions ) )
	{
		UE_LOG( LogChunkLandscapeMaterialManager, Warning, TEXT("Landscape Material %s does not have valid Material Layers!"), *BaseMaterial->GetFullName() );
	}

	FMaterialInheritanceChain MaterialInheritanceChain;
	BaseMaterial->GetMaterialInheritanceChain( MaterialInheritanceChain );

	// Retrieve the most recent material user data instance for this material
	const UChunkLandscapeMaterialUserData* MaterialUserData = const_cast<UMaterial*>(MaterialInheritanceChain.BaseMaterial)->GetAssetUserData<UChunkLandscapeMaterialUserData>();

	for ( const UMaterialInstance* ParentMaterialInstance : MaterialInheritanceChain.MaterialInstances )
	{
		if ( const UChunkLandscapeMaterialUserData* MaterialInstanceUserData = const_cast<UMaterialInstance*>(ParentMaterialInstance)->GetAssetUserData<UChunkLandscapeMaterialUserData>() )
		{
			MaterialUserData = MaterialInstanceUserData;
			break;
		}
	}

	// Make sure we have valid data in the chain, otherwise we should just stop here
	if ( !MaterialUserData )
	{
		UE_LOG( LogChunkLandscapeMaterialManager, Warning, TEXT("Landscape Material %s does not have valid LandscapeMaterialUserData in it's inheritance chain!"), *BaseMaterial->GetFullName() );
		return;
	}

	const UOpenWorldGeneratorMaterialSettings* MaterialSettings = UOpenWorldGeneratorMaterialSettings::Get();

	// Build combines mappings of material functions to their configurations
	TMap<TSoftObjectPtr<UMaterialFunctionInterface>, FChunkLandscapeMaterialLayerInfo> CombinedMaterialLayers = MaterialSettings->LayerMappings;
	TMap<TSoftObjectPtr<UMaterialFunctionInterface>, FChunkLandscapeMaterialLayerBlendInfo> CombinedMaterialBlends = MaterialSettings->BlendMappings;

	CombinedMaterialLayers.Append( MaterialUserData->LayerOverrides );
	CombinedMaterialBlends.Append( MaterialUserData->BlendOverrides );

	LayerToBlendTextureNameAndChannelMaskParameters.Empty();

	// First material layer needs to be bound explicitly as it will not have a dedicated blend layer, but can still represent a material layer
	if ( !MaterialLayersFunctions.Layers.IsEmpty() )
	{
		if ( const FChunkLandscapeMaterialLayerInfo* LayerInfo = CombinedMaterialLayers.Find( MaterialLayersFunctions.Layers[ 0 ] ) )
		{
			// Background layer does not have a dedicated blend, but is still a part of the supported layers of this material
			FLandscapeLayerParameterData ParameterData{};
			ParameterData.bIsBackgroundLayer = true;

			ParameterData.PopulateMetadataFromLayer( BaseMaterial, 0 );
			LayerToBlendTextureNameAndChannelMaskParameters.Add( LayerInfo->LandscapeLayer, ParameterData );
		}
	}

	// Material layers after the first one will have blend layers associated with them
	for ( int32 MaterialLayerIndex = 1; MaterialLayerIndex < MaterialLayersFunctions.Layers.Num(); MaterialLayerIndex++ )
	{
		if ( const FChunkLandscapeMaterialLayerInfo* LayerInfo = CombinedMaterialLayers.Find( MaterialLayersFunctions.Layers[ MaterialLayerIndex ] ) )
		{
			// Regardless of whenever this layer has a compatible blend or not, it is supported by this material
			const int32 BlendIndex = MaterialLayerIndex - 1;
			FLandscapeLayerParameterData ParameterData{};
			if ( const FChunkLandscapeMaterialLayerBlendInfo* BlendInfo = CombinedMaterialBlends.Find( MaterialLayersFunctions.Blends[ BlendIndex ] ) )
			{
				ParameterData.WeightMapTexture = FMaterialParameterInfo( BlendInfo->WeightMapTextureParameterName, BlendParameter, BlendIndex );
				ParameterData.WeightMapChannelMask = FMaterialParameterInfo( BlendInfo->WeightMapChannelMaskParameterName, BlendParameter, BlendIndex );
			}
			ParameterData.PopulateMetadataFromLayer( BaseMaterial, MaterialLayerIndex );
			LayerToBlendTextureNameAndChannelMaskParameters.Add( LayerInfo->LandscapeLayer, ParameterData );
		}
	}

	// Bind textures to the parameters
	RebindTexturesToMaterialParameters();
}

void FChunkBiomeLandscapeMaterial::RebindTexturesToMaterialParameters()
{
	constexpr int32 ChannelsPerTexture = 4;

	// Apply new values to the material instance
	if ( MaterialInstance == nullptr )
	{
		return;
	}
	const FChunkLandscapeWeightMapDescriptor* ChunkLandscapeWeightMap = ParentManager->OwnerChunk->GetWeightMapDescriptor();

	// Bind weight map textures to blend layers
	for ( const TPair<UOWGChunkLandscapeLayer*, FLandscapeLayerParameterData>& Pair : LayerToBlendTextureNameAndChannelMaskParameters )
	{
		const int32 LayerIndex = ChunkLandscapeWeightMap->FindLayerIndex( Pair.Key );

		// If the layer index is not valid, we do not need to bind a valid texture, but should instead mask out that layer completely
		if ( LayerIndex == INDEX_NONE )
		{
			if ( Pair.Value.WeightMapChannelMask.Name != NAME_None )
			{
				constexpr FLinearColor MaskedOutChannelMask( 0.0f, 0.0f, 0.0f, 0.0f );
				MaterialInstance->SetVectorParameterValueByInfo( Pair.Value.WeightMapChannelMask, MaskedOutChannelMask );
			}
			continue;
		}

		// Otherwise, resolve the texture index and the channel mask for that layer
		const int32 WeightMapTextureIndex = LayerIndex / ChannelsPerTexture;
		const int32 WeightMapChannelIndex = LayerIndex % ChannelsPerTexture;

		// Apply the texture
		if ( Pair.Value.WeightMapTexture.Name != NAME_None )
		{
			UTexture2D* WeightMapTexture = ParentManager->WeightMapTextures[ WeightMapTextureIndex ];
			MaterialInstance->SetTextureParameterValueByInfo( Pair.Value.WeightMapTexture, WeightMapTexture );
		}
		// Apply the channel mask
		if ( Pair.Value.WeightMapChannelMask.Name != NAME_None )
		{
			const FLinearColor ChannelMask( WeightMapChannelIndex == 0 ? 1.0f : 0.0f, WeightMapChannelIndex == 1 ? 1.0f : 0.0f, WeightMapChannelIndex == 2 ? 1.0f : 0.0f, WeightMapChannelIndex == 3 ? 1.0f : 0.0f );
			MaterialInstance->SetVectorParameterValueByInfo( Pair.Value.WeightMapChannelMask, ChannelMask );
		}
		// Apply grass color
		if ( Pair.Value.GrassColor.Name != NAME_None )
		{
			MaterialInstance->SetVectorParameterValueByInfo( Pair.Value.GrassColor, Biome->GrassColor );
		}
	}
}

void FChunkLandscapeMaterialManager::AddReferencedObjects( FReferenceCollector& ReferenceCollector )
{
	ReferenceCollector.AddStableReference( &OwnerChunk );
	ReferenceCollector.AddStableReferenceArray( &WeightMapTextures );

	for ( FChunkBiomeLandscapeMaterial& BiomeLandscapeMaterial : PerBiomeMaterials )
	{
		BiomeLandscapeMaterial.AddReferencedObjects( ReferenceCollector ); 
	}
}


