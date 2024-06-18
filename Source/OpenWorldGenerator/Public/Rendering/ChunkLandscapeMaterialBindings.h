// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "Engine/DeveloperSettings.h"
#include "ChunkLandscapeMaterialBindings.generated.h"

class UMaterialInterface;
class UMaterialFunctionInterface;

USTRUCT()
struct OPENWORLDGENERATOR_API FChunkLandscapeMaterialLayerBlendInfo
{
	GENERATED_BODY()

	/** Name of the texture parameter which will be populated with the weight map texture for this layer */
	UPROPERTY( EditAnywhere, Category = "Landscape Material" )
	FName WeightMapTextureParameterName;

	/** Name of the vector parameter which will be populated with the weight map channel mask for this layer */
	UPROPERTY( EditAnywhere, Category = "Landscape Material" )
	FName WeightMapChannelMaskParameterName;
};

USTRUCT()
struct OPENWORLDGENERATOR_API FChunkLandscapeMaterialLayerInfo
{
	GENERATED_BODY()

	/** Name of the layer this material function maps to */
	UPROPERTY( EditAnywhere, Category = "Landscape Material" )
	TObjectPtr<class UOWGChunkLandscapeLayer> LandscapeLayer;
};

/** Data needed to bind the landscape material to the dynamic chunk inputs */
UCLASS( EditInlineNew )
class OPENWORLDGENERATOR_API UChunkLandscapeMaterialUserData : public UAssetUserData
{
	GENERATED_BODY()
public:
	/** Blend overrides for material functions used in this material. These take priority over default mappings from OWG material config */
	UPROPERTY( EditAnywhere, Category = "Landscape Material", AdvancedDisplay )
	TMap<TSoftObjectPtr<UMaterialFunctionInterface>, FChunkLandscapeMaterialLayerBlendInfo> BlendOverrides;

	/** Layer overrides for material functions used in this material. These take priority over default mappings from OWG material config */
	UPROPERTY( EditAnywhere, Category = "Landscape Material", AdvancedDisplay )
	TMap<TSoftObjectPtr<UMaterialFunctionInterface>, FChunkLandscapeMaterialLayerInfo> LayerOverrides;
};

UCLASS( Config = "OpenWorldGenerator", DefaultConfig )
class OPENWORLDGENERATOR_API UOpenWorldGeneratorMaterialSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	FORCEINLINE static UOpenWorldGeneratorMaterialSettings* Get()
	{
		return GetMutableDefault<UOpenWorldGeneratorMaterialSettings>();
	}

	/** A list of material functions used as landscape material layer blends and their bindings (since material functions for some reason cannot have asset user data) */
	UPROPERTY( EditAnywhere, Config, Category = "Landscape Material" )
	TMap<TSoftObjectPtr<UMaterialFunctionInterface>, FChunkLandscapeMaterialLayerBlendInfo> BlendMappings;

	/** A list of material functions used as landscape material layers and their bindings (since material functions for some reason cannot have asset user data) */
	UPROPERTY( EditAnywhere, Config, Category = "Landscape Material" )
	TMap<TSoftObjectPtr<UMaterialFunctionInterface>, FChunkLandscapeMaterialLayerInfo> LayerMappings;

	/** Materials used for visualizing LOD levels of landscapes when enabled, for debugging */
	UPROPERTY( EditAnywhere, Config, Category = "Landscape Material|Debug" )
	TArray<TSoftObjectPtr<UMaterialInterface>> LODVisualizationMaterials;

	/** Materials used for visualizing various debug modes */
	UPROPERTY( EditAnywhere, Config, Category = "Landscape Material|Debug" )
	TMap<FString, TSoftObjectPtr<UMaterialInterface>> VisualizationMaterials;
};
