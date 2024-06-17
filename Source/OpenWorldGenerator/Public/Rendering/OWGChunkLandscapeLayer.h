// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "OWGChunkLandscapeLayer.generated.h"

class UPhysicalMaterial;
class ULandscapeGrassType;
class UOWGLandscapeGrassType;
class UStaticMesh;

UCLASS()
class OPENWORLDGENERATOR_API UOWGChunkLandscapeLayer : public UDataAsset
{
	GENERATED_BODY()
public:
	/** Physical material to be used for this layer */
	UPROPERTY( EditAnywhere, Category = "Landscape Layer" )
	TObjectPtr<UPhysicalMaterial> PhysicalMaterial;

	/** Landscape grass type for this layer. This will be used to automatically generate the basic grass for this landscape layer */
	UPROPERTY( EditAnywhere, Category = "Landscape Layer" )
	TObjectPtr<UOWGLandscapeGrassType> LandscapeGrass;

	/** The name under which this layer should be exposed to the Procedural Content Generation framework as a metadata for each point */
	UPROPERTY( EditAnywhere, Category = "Landscape Layer" )
	FName PCGMetadataAttributeName;
};

UENUM()
enum class EOWGLandscapeGrassScaling : uint8
{
	/** Grass instances will have uniform X, Y and Z scales. */
	Uniform,
	/** Grass instances will have random X, Y and Z scales. */
	Free,
	/** X and Y will be the same random scale, Z will be another */
	LockXY,
};

/** A mirror of the struct in Landscape called FGrassVariety. This one does not include some extra attributes not relevant to the chunked landscapes, and is also actually exported so you can use it! */
USTRUCT()
struct OPENWORLDGENERATOR_API FOWGLandscapeGrassVariety
{
	GENERATED_BODY()

	FOWGLandscapeGrassVariety();

	UPROPERTY(EditAnywhere, Category = "Grass|Mesh")
	TObjectPtr<UStaticMesh> GrassMesh;

	UPROPERTY(EditAnywhere, Category = "Grass|Mesh", meta = (ToolTip = "Material Overrides."))
	TArray<TObjectPtr<class UMaterialInterface>> OverrideMaterials;

	/** Specifies grass instance scaling type */
	UPROPERTY(EditAnywhere, Category = "Grass|Mesh")
	EOWGLandscapeGrassScaling Scaling;
	/** Specifies the range of scale, from minimum to maximum, to apply to a grass instance's X Scale property */
	UPROPERTY(EditAnywhere, Category = "Grass|Mesh")
	FFloatInterval ScaleX;
	/** Specifies the range of scale, from minimum to maximum, to apply to a grass instance's Y Scale property */
	UPROPERTY(EditAnywhere, Category = "Grass|Mesh", meta = (EditCondition = "Scaling == EOWGLandscapeGrassScaling::Free"))
	FFloatInterval ScaleY;
	/** Specifies the range of scale, from minimum to maximum, to apply to a grass instance's Z Scale property */
	UPROPERTY(EditAnywhere, Category = "Grass|Mesh", meta = (EditCondition = "Scaling == EOWGLandscapeGrassScaling::Free || Scaling == EOWGLandscapeGrassScaling::LockXY"))
	FFloatInterval ScaleZ;

	/* Instances per 10 square meters. */
	UPROPERTY(EditAnywhere, Category = "Grass|Placement", meta = (UIMin = 0, ClampMin = 0, UIMax = 1000, ClampMax = 1000))
	float GrassDensity;
	/* If true, use a jittered grid sequence for placement, otherwise use a halton sequence. */
	UPROPERTY(EditAnywhere, Category = "Grass|Placement")
	bool bUseGrid;
	UPROPERTY(EditAnywhere, Category = "Grass|Placement", meta = (EditCondition = "bUseGrid", UIMin = 0, ClampMin = 0, UIMax = 1, ClampMax = 1))
	float PlacementJitter;
	/** Whether the grass instances should be placed at random rotation (true) or all at the same rotation (false) */
	UPROPERTY(EditAnywhere, Category = "Grass|Placement")
	bool RandomRotation;
	/** Whether the grass instances should be tilted to the normal of the landscape (true), or always vertical (false) */
	UPROPERTY(EditAnywhere, Category = "Grass|Placement")
	bool AlignToSurface;

	/* The distance where instances will begin to fade out if using a PerInstanceFadeAmount material node. 0 disables. */
	UPROPERTY(EditAnywhere, Category = "Grass|Scalability", meta = (UIMin = 0, ClampMin = 0, UIMax = 1000000, ClampMax = 1000000))
	int32 StartCullDistance;
	/**
	 * The distance where instances will have completely faded out when using a PerInstanceFadeAmount material node. 0 disables. 
	 * When the entire cluster is beyond this distance, the cluster is completely culled and not rendered at all.
	 */
	UPROPERTY(EditAnywhere, Category = "Grass|Scalability", meta = (UIMin = 0, ClampMin = 0, UIMax = 1000000, ClampMax = 1000000))
	int32 EndCullDistance;
	/** 
	 * Specifies the smallest LOD that will be used for this component.
	 * If -1 (default), the MinLOD of the static mesh asset will be used instead.
	 */
	UPROPERTY(EditAnywhere, Category = "Grass|Scalability", meta = (UIMin = -1, ClampMin = -1, UIMax = 8, ClampMax = 8))
	int32 MinLOD;
	/** Distance at which to grass instances should disable WPO for performance reasons */
	UPROPERTY(EditAnywhere, Category = "Grass|Scalability")
	uint32 InstanceWorldPositionOffsetDisableDistance;
};

/** Describes data type needed to automatically procedurally generate the landscape grass across the given landscape layer */
UCLASS()
class OPENWORLDGENERATOR_API UOWGLandscapeGrassType : public UDataAsset
{
	GENERATED_BODY()
public:
	UOWGLandscapeGrassType();

	UPROPERTY(EditAnywhere, Category = "Grass")
	TArray<FOWGLandscapeGrassVariety> GrassVarieties;

	/**
	* Whether this grass type should be affected by the Engine Scalability system's Foliage grass.DensityScale setting. 
	* This is enabled by default but can be disabled should this grass type be important for gameplay reasons.
	*/
	UPROPERTY(EditAnywhere, Category = "Scalability")
	uint32 bEnableDensityScaling : 1;
};
