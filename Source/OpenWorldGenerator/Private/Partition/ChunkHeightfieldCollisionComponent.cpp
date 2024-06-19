// Copyright Nikita Zolotukhin. All Rights Reserved.

#include "Partition/ChunkHeightfieldCollisionComponent.h"
#include "DynamicMeshBuilder.h"
#include "PrimitiveSceneProxy.h"
#include "ShowFlags.h"
#include "AI/NavigationSystemHelpers.h"
#include "Engine/Engine.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"
#include "Partition/ChunkData2D.h"
#include "Partition/OWGChunk.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Physics/PhysicsFiltering.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "Rendering/OWGChunkLandscapeLayer.h"

DECLARE_CYCLE_STAT( TEXT("Chunk Landscape Collision Build/Update"), STAT_ChunkLandscapeCollisionBuild, STATGROUP_Game );

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST) && WITH_EDITORONLY_DATA

static TAutoConsoleVariable CVarDrawChunkLandscapeCollision(
	TEXT("owg.DrawChunkLandscapeCollision"),
	false,
	TEXT("Whenever to draw landscape collision visualization; 1 = enabled; 0 = disabled (default)"),
	ECVF_RenderThreadSafe | ECVF_Default
);

/** Height field visualization component to show the data inside of the chunk collision object */
class FChunkHeightFieldCollisionComponentSceneProxy final : public FPrimitiveSceneProxy
{
public:
	FChunkHeightFieldCollisionComponentSceneProxy(const UChunkHeightFieldCollisionComponent* InComponent, const Chaos::FHeightField& InHeightField, const FVector& LocalOffset, const FLinearColor& InWireframeColor);

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
	virtual bool CanBeOccluded() const override;
	virtual uint32 GetMemoryFootprint() const override;
	virtual SIZE_T GetTypeHash() const override;
private:
	TArray<FDynamicMeshVertex> Vertices;
	TArray<uint32> Indices;
	TUniquePtr<FColoredMaterialRenderProxy> WireframeMaterialInstance = nullptr;
};

#endif

UChunkHeightFieldCollisionComponent::UChunkHeightFieldCollisionComponent()
{
	SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	SetGenerateOverlapEvents(false);

	CastShadow = false;
	bUseAsOccluder = true;
	bAllowCullDistanceVolume = false;
	Mobility = EComponentMobility::Static;
	bCanEverAffectNavigation = true;
	bHasCustomNavigableGeometry = EHasCustomNavigableGeometry::Yes;

	HeightfieldRowsCount = -1;
	HeightfieldColumnsCount = -1;
}

void UChunkHeightFieldCollisionComponent::CreateCollisionData()
{
	SCOPE_CYCLE_COUNTER( STAT_ChunkLandscapeCollisionBuild );

	// Setup default physics material if we do not have one
	if ( !DefaultPhysicsMaterial )
	{
		DefaultPhysicsMaterial = GEngine->DefaultPhysMaterial;
	}
	
	const AOWGChunk* Chunk = CastChecked<AOWGChunk>( GetOwner() );
	const FChunkData2D& ChunkHeightmapData = Chunk->ChunkData2D.FindChecked( ChunkDataID::SurfaceHeightmap );
	
	TArray<Chaos::FReal> HeightFieldHeights;

	const int32 SurfaceResolutionXY = ChunkHeightmapData.GetSurfaceResolutionXY();
	const float* RawHeightmapDataPtr = ChunkHeightmapData.GetDataPtr<float>();

	// Scale in that context should map input range into the [0; SurfaceResolutionXY) range
	const Chaos::FVec3 HeightFieldScale( FChunkCoord::ChunkSizeWorldUnits / (SurfaceResolutionXY - 1), FChunkCoord::ChunkSizeWorldUnits / (SurfaceResolutionXY - 1), 1.0f );

	// Height field heights are per cell and column.
	for ( int32 ElementIndex = 0; ElementIndex < ChunkHeightmapData.GetSurfaceElementCount(); ElementIndex++ )
	{
		HeightFieldHeights.Add( RawHeightmapDataPtr[ElementIndex] );
	}

	TArray<Chaos::FMaterialHandle> UsedPhysicalMaterials;
	for ( const UOWGChunkLandscapeLayer* LandscapeLayer : Chunk->WeightMapDescriptor.GetAllLayers() )
	{
		const TObjectPtr<UPhysicalMaterial> PhysicalMaterial = LandscapeLayer->PhysicalMaterial.Get() ? LandscapeLayer->PhysicalMaterial : DefaultPhysicsMaterial;
		UsedPhysicalMaterials.Add( PhysicalMaterial->GetPhysicsMaterial() );
	}
	
	TArray<uint8> HeightFieldMaterials;
	const FChunkData2D& ChunkWeightMapData = Chunk->ChunkData2D.FindChecked( ChunkDataID::SurfaceWeights );

	// Height field materials are per cell! Which means 1 column and 1 row less
	for ( int32 CellX = 0; CellX < SurfaceResolutionXY - 1; CellX++ )
	{
		for ( int32 CellY = 0; CellY < SurfaceResolutionXY - 1; CellY++ )
		{
			const FVector2f NormalizedPosition( CellX * 1.0f / ( SurfaceResolutionXY - 1 ), CellY * 1.0f / ( SurfaceResolutionXY - 1 ) );
			const FChunkLandscapeWeight LandscapeWeight = ChunkWeightMapData.GetInterpolatedElementAt<FChunkLandscapeWeight>( NormalizedPosition );
			
			const int32 LargestContributionLayer = LandscapeWeight.GetLayerWithLargestContribution();
			HeightFieldMaterials.Add( (uint8) FMath::Min( LargestContributionLayer, HeightFieldMaterials.Num() - 1 ) );
		}
	}

	HeightFieldRef = MakeRefCount<FChunkHeightFieldGeometryRef>();
	HeightFieldRef->HeightField = MakeRefCount<Chaos::FHeightField>( MoveTemp( HeightFieldHeights ), MoveTemp( HeightFieldMaterials ), SurfaceResolutionXY, SurfaceResolutionXY, HeightFieldScale );
	HeightFieldRef->UsedChaosMaterials = MoveTemp( UsedPhysicalMaterials );

	// Height field needs to be adjusted to the middle of the chunk since it is originally in the corner of it
	HeightFieldRef->LocalOffset = FVector( -FChunkCoord::ChunkSizeWorldUnits / 2.0f, -FChunkCoord::ChunkSizeWorldUnits / 2.0f, 0.0f );

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST) && WITH_EDITORONLY_DATA
	// Re-create render state now that we have a valid height field. Only relevant for debug view modes in editor
	SendRenderTransform_Concurrent();
#endif
}


void UChunkHeightFieldCollisionComponent::PartialUpdateOrCreateHeightField( int32 StartX, int32 StartY, int32 EndX, int32 EndY )
{
	// Update collision data if it has been initialized before
	if ( HasValidPhysicsState() )
	{
		PartialUpdateCollisionData( StartX, StartY, EndX, EndY );
	}
	// Otherwise, create the physics state now
	else
	{
		CreatePhysicsState();
	}
}

void UChunkHeightFieldCollisionComponent::PartialUpdateCollisionData( int32 StartX, int32 StartY, int32 EndX, int32 EndY )
{
	if ( BodyInstance.IsValidBodyInstance() && false )
	{
		// Take the write lock on the shape object associated with the height field
		FPhysicsCommand::ExecuteWrite( BodyInstance.ActorHandle, [this, StartX, StartY, EndX, EndY]( const FPhysicsActorHandle& ActorHandle )
		{
			// Update the underlying height field data
			const bool bNeedsShapeUpdate = PartialUpdateCollisionData_AssumesLocked( StartX, StartY, EndX, EndY );

			// Rebuild geometry to update local bounds, and update in acceleration structure.
			const Chaos::FImplicitObjectUnion& Union = ActorHandle->GetGameThreadAPI().GetGeometry()->GetObjectChecked<Chaos::FImplicitObjectUnion>();
			Chaos::FImplicitObjectsArray NewGeometry;
			for (const Chaos::FImplicitObjectPtr& Object : Union.GetObjects())
			{
				const Chaos::TImplicitObjectTransformed<Chaos::FReal, 3>& TransformedHeightField = Object->GetObjectChecked<Chaos::TImplicitObjectTransformed<Chaos::FReal, 3>>();
				NewGeometry.Emplace(MakeImplicitObjectPtr<Chaos::TImplicitObjectTransformed<Chaos::FReal, 3>>(TransformedHeightField.GetGeometry(), TransformedHeightField.GetTransform()));
			}
			ActorHandle->GetGameThreadAPI().SetGeometry(MakeImplicitObjectPtr<Chaos::FImplicitObjectUnion>( MoveTemp(NewGeometry)) );

			// Rebuild shape if we need to update the materials used for it
			if ( bNeedsShapeUpdate )
			{
				const Chaos::FShapesArray& Shapes = ActorHandle->GetGameThreadAPI().ShapesArray();
				for (const TUniquePtr<Chaos::FPerShapeData>& ShapeData : Shapes )
				{
					ShapeData->AsShapeInstanceProxy()->SetMaterials( HeightFieldRef->UsedChaosMaterials );
				}
			}

			FPhysScene* PhysScene = GetWorld()->GetPhysicsScene();
			PhysScene->UpdateActorInAccelerationStructure( ActorHandle );
		} );

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST) && WITH_EDITORONLY_DATA
		// Re-create render state now that we have a valid height field. Only relevant for debug view modes in editor
		SendRenderTransform_Concurrent();
#endif
	}
}

bool UChunkHeightFieldCollisionComponent::PartialUpdateCollisionData_AssumesLocked( int32 StartX, int32 StartY, int32 EndX, int32 EndY ) const
{
	SCOPE_CYCLE_COUNTER( STAT_ChunkLandscapeCollisionBuild );

	const AOWGChunk* Chunk = CastChecked<AOWGChunk>( GetOwner() );
	const FChunkData2D& ChunkHeightmapData = Chunk->ChunkData2D.FindChecked( ChunkDataID::SurfaceHeightmap );

	const int32 SurfaceResolutionXY = ChunkHeightmapData.GetSurfaceResolutionXY();
	const float* RawHeightmapDataPtr = ChunkHeightmapData.GetDataPtr<float>();

	const int32 CurrentMaterialsSize = HeightFieldRef->UsedChaosMaterials.Num();
	TArray<Chaos::FMaterialHandle>& UsedChaosMaterials = HeightFieldRef->UsedChaosMaterials;

	const int32 ClampedStartX = FMath::Max( StartX, 0 );
	const int32 ClampedStartY = FMath::Max( StartY, 0 );
	const int32 ClampedEndX = FMath::Min( EndX, SurfaceResolutionXY - 1 );
	const int32 ClampedEndY = FMath::Min( EndY, SurfaceResolutionXY - 1 );
	
	// Update the height data on the height field now that we built a new array
	const int32 NumRows = ClampedEndY - ClampedStartY + 1;
	const int32 NumColumns = ClampedEndX - ClampedStartX + 1;
	const int32 BeginRow = ClampedStartY;
	const int32 BeginColumn = SurfaceResolutionXY - NumColumns - ClampedStartX;

	TArray<Chaos::FReal> HeightFieldHeights;
	HeightFieldHeights.AddZeroed( NumRows * NumColumns );
	
	// Height field heights are per row and column
	for ( int32 PosX = ClampedStartX; PosX <= ClampedEndX; PosX++ )
	{
		for ( int32 PosY = ClampedStartY; PosY <= ClampedEndY; PosY++ )
		{
			const int32 HeightFieldIndex = ( PosY - ClampedStartY ) * NumColumns + ( NumColumns - ( PosX - ClampedStartX ) - 1 );
			const int32 HeightmapIndex = PosY * SurfaceResolutionXY + PosX;
			HeightFieldHeights[ HeightFieldIndex ] = RawHeightmapDataPtr[ HeightmapIndex ];
		}
	}

	// Height field materials are per cell
	for ( int32 CellX = ClampedStartX; CellX < ClampedEndX; CellX++ )
	{
		for ( int32 CellY = ClampedStartY; CellY < ClampedEndY; CellY++ )
		{
			// Update material data at this point
			const uint8 NewMaterialIndex = (uint8) UsedChaosMaterials.AddUnique( DefaultPhysicsMaterial->GetPhysicsMaterial() );
			HeightFieldRef->HeightField->GeomData.MaterialIndices[ CellY * ( SurfaceResolutionXY - 1 ) + CellX ] = NewMaterialIndex;
		}
	}

	HeightFieldRef->HeightField->EditHeights( HeightFieldHeights, BeginRow, BeginColumn, NumRows, NumColumns );

	// We need to update the shape materials if we have added any new materials to the array
	return CurrentMaterialsSize != UsedChaosMaterials.Num();
}

bool UChunkHeightFieldCollisionComponent::ShouldCreatePhysicsState() const
{
	// Only allow physics state creation when we have a valid heightmap data on the owning chunk. Before that, it would be a waste of the CPU time
	const AOWGChunk* Chunk = CastChecked<AOWGChunk>( GetOwner() );
	return Super::ShouldCreatePhysicsState() && Chunk->ChunkData2D.Contains( ChunkDataID::SurfaceHeightmap );
}

void UChunkHeightFieldCollisionComponent::OnCreatePhysicsState()
{
	USceneComponent::OnCreatePhysicsState(); // route OnCreatePhysicsState, skip PrimitiveComponent implementation

	if ( !BodyInstance.IsValidBodyInstance() )
	{
		// Create the collision data, since we do not have a physics representation right now
		CreateCollisionData();
		
		FActorCreationParams Params;
		Params.InitialTM = GetComponentTransform();
		Params.bQueryOnly = false;
		Params.bStatic = true;
		Params.Scene = GetWorld()->GetPhysicsScene();

		FPhysicsActorHandle PhysHandle;
		FPhysicsInterface::CreateActor( Params, PhysHandle );
		Chaos::FRigidBodyHandle_External& Body_External = PhysHandle->GetGameThreadAPI();

		Chaos::FShapesArray ShapeArray;
		Chaos::FImplicitObjectsArray Geometry;

		// First add complex geometry
		// We need to offset the height field by the half of the chunk because right now the chunk landscape geometry is not centered, and instead starts at the bottom of the chunk, going up
		Chaos::FImplicitObjectPtr ChaosHeightFieldShapeData = MakeImplicitObjectPtr<Chaos::TImplicitObjectTransformed<Chaos::FReal, 3>>(
			Chaos::FImplicitObjectPtr(HeightFieldRef->HeightField), Chaos::FRigidTransform3( FTransform( HeightFieldRef->LocalOffset ) ) );
		TUniquePtr<Chaos::FShapeInstanceProxy> NewShape = Chaos::FShapeInstanceProxy::Make( ShapeArray.Num(), ChaosHeightFieldShapeData );

		// Setup filtering
		FCollisionFilterData QueryFilterData, SimFilterData;
		CreateShapeFilterData( GetCollisionObjectType(), static_cast<FMaskFilter>(0), GetOwner()->GetUniqueID(), GetCollisionResponseToChannels(), 
		GetUniqueID(), 0, QueryFilterData, SimFilterData, true, false, true );

		// Height field is used for simple and complex collision
		QueryFilterData.Word3 |= (EPDF_SimpleCollision | EPDF_ComplexCollision);
		SimFilterData.Word3 |= (EPDF_SimpleCollision | EPDF_ComplexCollision);

		NewShape->SetQueryData( QueryFilterData );
		NewShape->SetSimData( SimFilterData );
		NewShape->SetMaterials( HeightFieldRef->UsedChaosMaterials );

		Geometry.Emplace( MoveTemp(ChaosHeightFieldShapeData) );
		ShapeArray.Emplace( MoveTemp(NewShape) );

		// Push the shapes to the actor
		// We always wrap it into the object union because the code in PartialUpdateCollisionData_AssumesLocked assumes it will always be the object union
		Body_External.SetGeometry( MakeImplicitObjectPtr<Chaos::FImplicitObjectUnion>( MoveTemp(Geometry) ) );

		// Construct Shape Bounds
		for ( const TUniquePtr<Chaos::FPerShapeData>& Shape : ShapeArray)
		{
			Chaos::FRigidTransform3 WorldTransform = Chaos::FRigidTransform3( Body_External.X(), Body_External.R() );
			Shape->AsShapeInstanceProxy()->UpdateShapeBounds( WorldTransform );
		}
		Body_External.MergeShapesArray( MoveTemp(ShapeArray) );

		// Push the actor to the scene
		FPhysScene* PhysScene = GetWorld()->GetPhysicsScene();

		// Set body instance data
		BodyInstance.PhysicsUserData = FPhysicsUserData( &BodyInstance );
		BodyInstance.OwnerComponent = this;
		BodyInstance.ActorHandle = PhysHandle;

		Body_External.SetUserData( &BodyInstance.PhysicsUserData );

		TArray<FPhysicsActorHandle> Actors;
		Actors.Add( PhysHandle );

		FPhysicsCommand::ExecuteWrite( PhysScene, [&]()
		{
			constexpr bool bImmediateAccelStructureInsertion = true;
			PhysScene->AddActorsToScene_AssumesLocked( Actors, bImmediateAccelStructureInsertion );
		 });

		PhysScene->AddToComponentMaps(this, PhysHandle);
		if ( BodyInstance.bNotifyRigidBodyCollision )
		{
			PhysScene->RegisterForCollisionEvents(this);
		}
	}
}

void UChunkHeightFieldCollisionComponent::OnDestroyPhysicsState()
{
	Super::OnDestroyPhysicsState();

	if ( FPhysScene_Chaos* PhysScene = GetWorld()->GetPhysicsScene() )
	{
		const FPhysicsActorHandle& ActorHandle = BodyInstance.GetPhysicsActorHandle();
		if ( FPhysicsInterface::IsValid(ActorHandle) )
		{
			PhysScene->RemoveFromComponentMaps( ActorHandle );
		}
		if ( BodyInstance.bNotifyRigidBodyCollision )
		{
			PhysScene->UnRegisterForCollisionEvents( this );
		}
	}
}

FBoxSphereBounds UChunkHeightFieldCollisionComponent::CalcBounds( const FTransform& LocalToWorld ) const
{
	// We encapsulate the entire chunk, but the chunk boundaries are starting from the origin vertically and going up - because chunks are not currently centered, since height range is undefined.
	// Since we are the only colliding component in the chunk at the start, our boundaries determine the chunk actor boundaries, which should span from 0 to chunk size
	FBoxSphereBounds NewBounds;
	NewBounds.Origin = LocalToWorld.GetLocation() + FVector( 0.0f, 0.0f, FChunkCoord::ChunkSizeWorldUnits / 2.0f );
	NewBounds.BoxExtent = FVector( FChunkCoord::ChunkSizeWorldUnits );
	NewBounds.SphereRadius = NewBounds.BoxExtent.Size();
	return NewBounds;
}

void UChunkHeightFieldCollisionComponent::ApplyWorldOffset( const FVector& InOffset, bool bWorldShift )
{
	Super::ApplyWorldOffset( InOffset, bWorldShift );
	
	if ( !bWorldShift || !FPhysScene::SupportsOriginShifting() )
	{
		RecreatePhysicsState();
	}
}

bool UChunkHeightFieldCollisionComponent::DoCustomNavigableGeometryExport(FNavigableGeometryExport& GeomExport) const
{
	check( IsInGameThread() );
	if( HeightFieldRef.IsValid() && HeightFieldRef->HeightField )
	{
		const FTransform HFToW = GetComponentTransform();
		GeomExport.ExportChaosHeightField( HeightFieldRef->HeightField.GetReference(), HFToW );
	}
	return false;
}

bool UChunkHeightFieldCollisionComponent::IsShown( const FEngineShowFlags& ShowFlags ) const
{
	return ShowFlags.Landscape;
}

void UChunkHeightFieldCollisionComponent::GatherGeometrySlice(FNavigableGeometryExport& GeomExport, const FBox& SliceBox) const
{
	// note that this function can get called off game thread
	if ( !CachedHeightFieldSamples.IsEmpty() )
	{
		const FTransform HFToW = GetComponentTransform();
		GeomExport.ExportChaosHeightFieldSlice(CachedHeightFieldSamples, HeightfieldRowsCount, HeightfieldColumnsCount, HFToW, SliceBox);
	}
}

ENavDataGatheringMode UChunkHeightFieldCollisionComponent::GetGeometryGatheringMode() const
{ 
	return ENavDataGatheringMode::Default;
}

void UChunkHeightFieldCollisionComponent::PrepareGeometryExportSync()
{
	if( HeightFieldRef.IsValid() && HeightFieldRef->HeightField && CachedHeightFieldSamples.IsEmpty() )
	{
		HeightfieldRowsCount = HeightFieldRef->HeightField->GetNumRows();
		HeightfieldColumnsCount = HeightFieldRef->HeightField->GetNumCols();
		const int32 HeightsCount = HeightfieldRowsCount * HeightfieldColumnsCount;

		if( CachedHeightFieldSamples.Heights.Num() != HeightsCount )
		{
			CachedHeightFieldSamples.Heights.SetNumUninitialized(HeightsCount);
			for(int32 Index = 0; Index < HeightsCount; Index++)
			{
				CachedHeightFieldSamples.Heights[Index] = HeightFieldRef->HeightField->GetHeight(Index);
			}

			const int32 HolesCount = (HeightfieldRowsCount-1) * (HeightfieldColumnsCount-1);
			CachedHeightFieldSamples.Holes.SetNumUninitialized(HolesCount);
			for(int32 Index = 0; Index < HolesCount; ++Index)
			{
				CachedHeightFieldSamples.Holes[Index] = HeightFieldRef->HeightField->IsHole(Index);
			}
		}
	}
}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST) && WITH_EDITORONLY_DATA

FPrimitiveSceneProxy* UChunkHeightFieldCollisionComponent::CreateSceneProxy()
{
	if ( HeightFieldRef.IsValid() && HeightFieldRef->HeightField.IsValid() )
	{
		const Chaos::FHeightField* LocalHeightField = HeightFieldRef->HeightField.GetReference();
		const FLinearColor WireframeColor = FLinearColor::Green;
		
		if (LocalHeightField != nullptr)
		{
			return new FChunkHeightFieldCollisionComponentSceneProxy(this, *LocalHeightField, HeightFieldRef->LocalOffset, WireframeColor);
		}
	}
	return nullptr;
}

bool UChunkHeightFieldCollisionComponent::ShouldRecreateProxyOnUpdateTransform() const
{
	// We never update transform really, but updating the transform is a simple way to trigger re-creation of the scene proxy, so we want to re-create it if we forcefully update the transform
	return true;
}

FChunkHeightFieldCollisionComponentSceneProxy::FChunkHeightFieldCollisionComponentSceneProxy(const UChunkHeightFieldCollisionComponent* InComponent, const Chaos::FHeightField& InHeightField, const FVector& LocalOffset, const FLinearColor& InWireframeColor) : FPrimitiveSceneProxy(InComponent)
{
	const Chaos::FHeightField::FData<uint16>& GeomData = InHeightField.GeomData; 
	const int32 NumRows = InHeightField.GetNumRows();
	const int32 NumCols = InHeightField.GetNumCols();
	const int32 NumVerts = NumRows * NumCols;
	const int32 NumTris = (NumRows - 1) * (NumCols - 1) * 2;

	Vertices.SetNumUninitialized(NumVerts);
	for (int32 I = 0; I < NumVerts; I++)
	{
		Chaos::FVec3 Point = GeomData.GetPointScaled(I);
		Vertices[I].Position = FVector3f( LocalOffset ) + FVector3f(Point.X, Point.Y, Point.Z);
	}
	Indices.SetNumUninitialized(NumTris * 3);

	// Editor heightfields don't have material indices (hence, no holes), in which case InHeightfield.GeomData.MaterialIndices.Num() == 1 : 
	const int32 NumMaterialIndices = InHeightField.GeomData.MaterialIndices.Num();
	const bool bHasMaterialIndices = (NumMaterialIndices > 1);
	check(!bHasMaterialIndices || (NumMaterialIndices == ((NumRows - 1) * (NumCols - 1))));

	int32 TriangleIdx = 0;
	for (int32 Y = 0; Y < (NumRows - 1); Y++)
	{
		for (int32 X = 0; X < (NumCols - 1); X++)
		{
			bool bHole = false;

			if (bHasMaterialIndices)
			{
				// Material indices don't have the final row/column : 
				int32 MaterialIndicesDataIdx = X + Y * (NumCols - 1);
				uint8 LayerIdx = InHeightField.GeomData.MaterialIndices[MaterialIndicesDataIdx];
				bHole = (LayerIdx == TNumericLimits<uint8>::Max());
			}

			if (bHole)
			{
				Indices[TriangleIdx + 0] = (X + 0) + (Y + 0) * NumCols;
				Indices[TriangleIdx + 1] = Indices[TriangleIdx + 0];
				Indices[TriangleIdx + 2] = Indices[TriangleIdx + 0];
			}
			else
			{
				Indices[TriangleIdx + 0] = (X + 0) + (Y + 0) * NumCols;
				Indices[TriangleIdx + 1] = (X + 1) + (Y + 1) * NumCols;
				Indices[TriangleIdx + 2] = (X + 1) + (Y + 0) * NumCols;
			}

			TriangleIdx += 3;

			if (bHole)
			{
				Indices[TriangleIdx + 0] = (X + 0) + (Y + 0) * NumCols;
				Indices[TriangleIdx + 1] = Indices[TriangleIdx + 0];
				Indices[TriangleIdx + 2] = Indices[TriangleIdx + 0];
			}
			else
			{
				Indices[TriangleIdx + 0] = (X + 0) + (Y + 0) * NumCols;
				Indices[TriangleIdx + 1] = (X + 0) + (Y + 1) * NumCols;
				Indices[TriangleIdx + 2] = (X + 1) + (Y + 1) * NumCols;
			}

			TriangleIdx += 3;
		}
	}
	WireframeMaterialInstance.Reset(new FColoredMaterialRenderProxy( GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : NULL, InWireframeColor));
}

void FChunkHeightFieldCollisionComponentSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	FMatrix LocalToWorldNoScale = GetLocalToWorld();
	LocalToWorldNoScale.RemoveScaling();

	const bool bDrawCollision = ViewFamily.EngineShowFlags.Collision && IsCollisionEnabled();
	
	if ( ( bDrawCollision || CVarDrawChunkLandscapeCollision.GetValueOnRenderThread() ) && AllowDebugViewmodes() && WireframeMaterialInstance.IsValid())
	{
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				const FSceneView* View = Views[ViewIndex];
				// Set up mesh builder
				FDynamicMeshBuilder MeshBuilder(View->GetFeatureLevel());
				MeshBuilder.AddVertices(Vertices);
				MeshBuilder.AddTriangles(Indices);

				MeshBuilder.GetMesh(LocalToWorldNoScale, WireframeMaterialInstance.Get(), SDPG_World, false, false, ViewIndex, Collector);
			}
		}
	}
}

FPrimitiveViewRelevance FChunkHeightFieldCollisionComponentSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	// Should we draw this because collision drawing is enabled, and we have collision
	const bool bShowForCollision = View->Family->EngineShowFlags.Collision && IsCollisionEnabled();

	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View) || bShowForCollision || CVarDrawChunkLandscapeCollision.GetValueOnRenderThread();
	Result.bDynamicRelevance = true;
	Result.bShadowRelevance = false;
	Result.bEditorPrimitiveRelevance = UseEditorCompositing(View);
	return Result;
}

bool FChunkHeightFieldCollisionComponentSceneProxy::CanBeOccluded() const
{
	return false;
}

uint32 FChunkHeightFieldCollisionComponentSceneProxy::GetMemoryFootprint() const 
{
	return sizeof(*this) + GetAllocatedSize();
}

SIZE_T FChunkHeightFieldCollisionComponentSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer{};
	return reinterpret_cast<size_t>(&UniquePointer);
}

#endif
