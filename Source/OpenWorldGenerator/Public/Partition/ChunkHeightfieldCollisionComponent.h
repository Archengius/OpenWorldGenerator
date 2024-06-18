// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Chaos/HeightField.h"
#include "Components/PrimitiveComponent.h"
#include "ChunkHeightfieldCollisionComponent.generated.h"

class UPhysicalMaterial;

/** Data about the height field */
struct FChunkHeightFieldGeometryRef : FRefCountedObject
{
	TArray<Chaos::FMaterialHandle> UsedChaosMaterials;
	TRefCountPtr<Chaos::FHeightField> HeightField;
	FVector LocalOffset{}; // offset of the height field, in component local space
};

/** Component that exposes the height map collision to the chaos physics engine */
UCLASS( Within = "OWGChunk" )
class OPENWORLDGENERATOR_API UChunkHeightFieldCollisionComponent : public UPrimitiveComponent
{
	GENERATED_BODY()
public:
	UChunkHeightFieldCollisionComponent();

	// Begin UActorComponent Interface.
protected:
	virtual bool ShouldCreatePhysicsState() const override;
	virtual void OnCreatePhysicsState() override;
	virtual void OnDestroyPhysicsState() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform &BoundTransform) const override;
public:
	virtual void ApplyWorldOffset(const FVector& InOffset, bool bWorldShift) override;
	// End UActorComponent Interface.

	//~ Begin UPrimitiveComponent Interface
	virtual bool DoCustomNavigableGeometryExport(FNavigableGeometryExport& GeomExport) const override;
	virtual bool IsShown(const FEngineShowFlags& ShowFlags) const override;
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST) && WITH_EDITORONLY_DATA
	// The scene proxy is only for debug purposes
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual bool ShouldRecreateProxyOnUpdateTransform() const override;
#endif
	//End UPrimitiveComponent interface

	//~ Begin INavRelevantInterface Interface
	virtual bool SupportsGatheringGeometrySlices() const override { return true; }
	virtual void GatherGeometrySlice(FNavigableGeometryExport& GeomExport, const FBox& SliceBox) const override;
	virtual ENavDataGatheringMode GetGeometryGatheringMode() const override;
	virtual void PrepareGeometryExportSync() override;
	//~ End INavRelevantInterface Interface

	/** Performs a partial update of the height field, or creates a physics state if it has not been created yet */
	void PartialUpdateOrCreateHeightField( int32 StartX, int32 StartY, int32 EndX, int32 EndY );
protected:
	/** Updates collision data once the collision state has already been initialized and created. Should be called from the game thread without holding the Chaos lock. If collision is not initialized yet, does nothing */
	void PartialUpdateCollisionData( int32 StartX, int32 StartY, int32 EndX, int32 EndY );
	/** Creates and initialize height field reference for the first time, while it is not actively used by the physics engine. When the data is already in the physics scene, use PartialUpdateCollisionData */
	void CreateCollisionData();
	/** Partially updates the data in the height field from the chunk. Assumes that the physics scene is locked. Returns true if we added new materials to the shape */
	bool PartialUpdateCollisionData_AssumesLocked( int32 StartX, int32 StartY, int32 EndX, int32 EndY ) const;
	
	/** Height field data generated for this chunk */
	TRefCountPtr<FChunkHeightFieldGeometryRef> HeightFieldRef;

	/** Default physics material to use for the chunk when the chunk majority layer does not specify a correct physics material */
	UPROPERTY( EditDefaultsOnly, Category = "Chunk" )
	TObjectPtr<UPhysicalMaterial> DefaultPhysicsMaterial;

	/** Cached PxHeightFieldSamples values for navmesh generation. Note that it's being used only if navigation octree is set up for lazy geometry exporting */
	int32 HeightfieldRowsCount;
	int32 HeightfieldColumnsCount;
	FNavHeightfieldSamples CachedHeightFieldSamples;
};