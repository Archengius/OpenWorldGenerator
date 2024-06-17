// Copyright Nikita Zolotukhin. All Rights Reserved.

#include "Partition/OWGChunk.h"
#include "DisplayDebugHelpers.h"
#include "DrawDebugHelpers.h"
#include "OpenWorldGeneratorSubsystem.h"
#include "Algo/Unique.h"
#include "Async/Async.h"
#include "Components/BrushComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "Generation/OWGChunkGenerator.h"
#include "Generation/OWGNoiseGenerator.h"
#include "Generation/OWGWorldGeneratorConfiguration.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Partition/ChunkHeightfieldCollisionComponent.h"
#include "Partition/ChunkLandscapeMaterialManager.h"
#include "Partition/ChunkLandscapeMeshManager.h"
#include "Partition/ChunkLandscapeWeight.h"
#include "Partition/OWGChunkManagerInterface.h"
#include "Partition/OWGChunkSerialization.h"
#include "Partition/TerraformingBrush.h"
#include "Rendering/ChunkTextureManager.h"
#include "Rendering/OWGChunkLandscapeLayer.h"

DECLARE_CYCLE_STAT( TEXT("Chunk Landscape Point Sample"), STAT_ChunkLandscapePointSample, STATGROUP_Game );
DECLARE_CYCLE_STAT( TEXT("Chunk Get Landscape Metrics"), STAT_ChunkGetLandscapeMetrics, STATGROUP_Game );
DECLARE_CYCLE_STAT( TEXT("Chunk Modify Landscape"), STAT_ChunkModifyLandscape, STATGROUP_Game );
DECLARE_CYCLE_STAT( TEXT("Chunk Process Chunk Generation"), STAT_ProcessChunkGeneration, STATGROUP_Game );
DECLARE_CYCLE_STAT( TEXT("Generate Noise For Chunk"), STAT_GenerateNoiseForChunk, STATGROUP_Game );

static TAutoConsoleVariable CVarChunkLODOverride(
	TEXT("owg.ChunkLODOverride"),
	-1,
	TEXT("Allows overriding the LOD level that the chunks use. -1 = use default (player distance based)")
);

static TAutoConsoleVariable CVarChunkVisualizeLandscapeEditBounds(
	TEXT("owg.VisualizeLandscapeEditBounds"),
	false,
	TEXT("True to visualize bounds of landscape modifications to the chunk. Useful for investigating issues where brushes do not return correct extents for landscape modification.")
);

FChunkLandscapePointSampler::FChunkLandscapePointSampler( const AOWGChunk* Chunk )
{
	check( Chunk->IsChunkInitialized() );
	check( IsInGameThread() );

	ChunkToWorld = Chunk->GetActorTransform();
	HeightMapData = Chunk->FindRawChunkData( ChunkDataID::SurfaceHeightmap );
	NormalMapData = Chunk->FindRawChunkData( ChunkDataID::SurfaceNormal );
	SteepnessData = Chunk->FindRawChunkData( ChunkDataID::SurfaceSteepness );
	WeightMapData = Chunk->FindRawChunkData( ChunkDataID::SurfaceWeights );
	WeightMapDescriptor = Chunk->GetWeightMapDescriptor();

	BiomeMapData = Chunk->FindRawChunkData( ChunkDataID::BiomeMap );
	BiomePalette = Chunk->GetBiomePalette();
}

FChunkLandscapePointSampler::FChunkLandscapePointSampler( const FCachedChunkLandscapeData* LandscapeData, const FCachedChunkBiomeData* BiomeData )
{
	check( LandscapeData );

	ChunkToWorld = LandscapeData->ChunkToWorld;
	HeightMapData = &LandscapeData->HeightMapData;
	NormalMapData = &LandscapeData->NormalMapData;
	SteepnessData = &LandscapeData->SteepnessData;
	WeightMapData = &LandscapeData->WeightMapData;
	WeightMapDescriptor = &LandscapeData->WeightMapDescriptor;

	if ( BiomeData != nullptr )
	{
		BiomeMapData = &BiomeData->BiomeMap;
		BiomePalette = &BiomeData->BiomePalette;
	}
}

FVector FChunkLandscapePointSampler::GetPointExtents() const
{
	const float PointAbsoluteSize = FChunkCoord::ChunkSizeWorldUnits / ( HeightMapData->GetSurfaceResolutionXY() - 1 );
	return FVector( PointAbsoluteSize / 2.0f );
}

bool FChunkLandscapePointSampler::CheckPointInBounds( const FVector& WorldLocation ) const
{
	const FVector ChunkLocalPosition = ChunkToWorld.InverseTransformPosition( WorldLocation );
	return FMath::Abs( ChunkLocalPosition.X ) <= FChunkCoord::ChunkSizeWorldUnits / 2.0f &&
		FMath::Abs( ChunkLocalPosition.Y ) <= FChunkCoord::ChunkSizeWorldUnits / 2.0f;
}

FTransform FChunkLandscapePointSampler::SamplePointTransformInterpolated( const FVector& WorldLocation ) const
{
	const FVector ChunkLocalPosition = ChunkToWorld.InverseTransformPosition( WorldLocation );
	const FTransform PointTransform = SamplePointTransformInterpolated_Local( ChunkLocalPosition );
	return PointTransform * ChunkToWorld;
}

FTransform FChunkLandscapePointSampler::SamplePointTransformGrid( const FVector& WorldLocation ) const
{
	const FVector ChunkLocalPosition = ChunkToWorld.InverseTransformPosition( WorldLocation );
	const FTransform PointTransform = SamplePointTransformGrid_Local( ChunkLocalPosition );
	return PointTransform * ChunkToWorld;
}

FChunkLandscapePoint FChunkLandscapePointSampler::SamplePointInterpolated( const FVector& WorldLocation ) const
{
	const FVector ChunkLocalPosition = ChunkToWorld.InverseTransformPosition( WorldLocation );
	FChunkLandscapePoint ResultPoint = SamplePointInterpolated_Local( ChunkLocalPosition );
	ResultPoint.Transform = ResultPoint.Transform * ChunkToWorld;
	return ResultPoint;
}

FChunkLandscapePoint FChunkLandscapePointSampler::SamplePointGrid( const FVector& WorldLocation ) const
{
	const FVector ChunkLocalPosition = ChunkToWorld.InverseTransformPosition( WorldLocation );
	FChunkLandscapePoint ResultPoint = SamplePointGrid_Local( ChunkLocalPosition );
	ResultPoint.Transform = ResultPoint.Transform * ChunkToWorld;
	return ResultPoint;
}

void FChunkLandscapePointSampler::ForEachPointGrid( const FBox& WorldBounds, const TFunctionRef<bool(FChunkLandscapePoint& Point)>& Operation ) const
{
	const FBox ChunkLocalBounds( ChunkToWorld.InverseTransformPosition( WorldBounds.Min ), ChunkToWorld.InverseTransformPosition( WorldBounds.Max ) );
	
	ForEachPointGrid_Local( ChunkLocalBounds, [this, &Operation](FChunkLandscapePoint& LandscapePoint)
	{
		LandscapePoint.Transform = LandscapePoint.Transform * ChunkToWorld;
		return Operation( LandscapePoint );
	} );
}

FTransform FChunkLandscapePointSampler::SamplePointTransformInterpolated_Local( const FVector& ChunkLocalPosition ) const
{
	SCOPE_CYCLE_COUNTER( STAT_ChunkLandscapePointSample );
	const FVector2f NormalizedPosition = FChunkData2D::ChunkLocalPositionToNormalized( ChunkLocalPosition );

	const float PointHeight = HeightMapData->GetInterpolatedElementAt<float>( NormalizedPosition );
	const FVector3f PointNormal = NormalMapData->GetInterpolatedElementAt<FVector3f>( NormalizedPosition );

	const FVector PointLocation( ChunkLocalPosition.X, ChunkLocalPosition.Y, PointHeight );
	const FQuat PointRotation = FRotationMatrix::MakeFromZ( FVector( PointNormal ) ).ToQuat();

	return FTransform( PointRotation, PointLocation );
}

FTransform FChunkLandscapePointSampler::SamplePointTransformGrid_Local( const FVector& ChunkLocalPosition ) const
{
	SCOPE_CYCLE_COUNTER( STAT_ChunkLandscapePointSample );
	const FVector2f NormalizedPosition = FChunkData2D::ChunkLocalPositionToNormalized( ChunkLocalPosition );

	const float PointHeight = HeightMapData->GetClosestElementAt<float>( NormalizedPosition );
	const FVector3f PointNormal = NormalMapData->GetClosestElementAt<FVector3f>( NormalizedPosition );

	const FVector PointLocation = HeightMapData->SnapToGrid( FVector( ChunkLocalPosition.X, ChunkLocalPosition.Y, PointHeight ) );
	const FQuat PointRotation = FRotationMatrix::MakeFromZ( FVector( PointNormal ) ).ToQuat();

	return FTransform( PointRotation, PointLocation );
}

FChunkLandscapePoint FChunkLandscapePointSampler::SamplePointInterpolated_Local( const FVector& ChunkLocalPosition ) const
{
	SCOPE_CYCLE_COUNTER( STAT_ChunkLandscapePointSample );
	const FVector2f NormalizedPosition = FChunkData2D::ChunkLocalPositionToNormalized( ChunkLocalPosition );

	const float PointHeight = HeightMapData->GetInterpolatedElementAt<float>( NormalizedPosition );
	const FVector3f PointNormal = NormalMapData->GetInterpolatedElementAt<FVector3f>( NormalizedPosition );
	const float PointSteepness = SteepnessData->GetInterpolatedElementAt<float>( NormalizedPosition );
	const FChunkLandscapeWeight PointWeight = WeightMapData->GetInterpolatedElementAt<FChunkLandscapeWeight>( NormalizedPosition );

	const FVector PointLocation( ChunkLocalPosition.X, ChunkLocalPosition.Y, PointHeight );
	const FQuat PointRotation = FRotationMatrix::MakeFromZ( FVector( PointNormal ) ).ToQuat();
	
	FChunkLandscapePoint ResultPoint{};
	ResultPoint.Transform = FTransform( PointRotation, PointLocation );
	ResultPoint.Steepness = PointSteepness;
	PopulatePointLayerWeights( ResultPoint, PointWeight );

	if ( BiomePalette != nullptr && BiomeMapData != nullptr )
	{
		const FBiomePaletteIndex BiomePaletteIndex = BiomeMapData->GetClosestElementAt<FBiomePaletteIndex>( NormalizedPosition );
		ResultPoint.Biome = BiomePalette->GetBiomeByIndex( BiomePaletteIndex );
	}
	return ResultPoint;
}

FChunkLandscapePoint FChunkLandscapePointSampler::SamplePointGrid_Local( const FVector& ChunkLocalPosition ) const
{
	SCOPE_CYCLE_COUNTER( STAT_ChunkLandscapePointSample );
	const FVector2f NormalizedPosition = FChunkData2D::ChunkLocalPositionToNormalized( ChunkLocalPosition );

	const float PointHeight = HeightMapData->GetClosestElementAt<float>( NormalizedPosition );
	const FVector3f PointNormal = NormalMapData->GetClosestElementAt<FVector3f>( NormalizedPosition );
	const float PointSteepness = SteepnessData->GetClosestElementAt<float>( NormalizedPosition );
	const FChunkLandscapeWeight PointWeight = WeightMapData->GetClosestElementAt<FChunkLandscapeWeight>( NormalizedPosition );

	const FVector PointLocation = HeightMapData->SnapToGrid( FVector( FVector( ChunkLocalPosition.X, ChunkLocalPosition.Y, PointHeight ) ) );
	const FQuat PointRotation = FRotationMatrix::MakeFromZ( FVector( PointNormal ) ).ToQuat();
	
	FChunkLandscapePoint ResultPoint{};
	ResultPoint.Transform = FTransform( PointRotation, PointLocation );
	ResultPoint.Steepness = PointSteepness;
	PopulatePointLayerWeights( ResultPoint, PointWeight );

	if ( BiomePalette != nullptr && BiomeMapData != nullptr )
	{
		const FBiomePaletteIndex BiomePaletteIndex = BiomeMapData->GetClosestElementAt<FBiomePaletteIndex>( NormalizedPosition );
		ResultPoint.Biome = BiomePalette->GetBiomeByIndex( BiomePaletteIndex );
	}
	return ResultPoint;
}

void FChunkLandscapePointSampler::ForEachPointGrid_Local( const FBox& ChunkLocalBounds, const TFunctionRef<bool(FChunkLandscapePoint& Point)>& Operation ) const
{
	// Determine the range in grid points, and then iterate over them
	const FIntVector2 StartPos = HeightMapData->ChunkLocalPositionToPoint( ChunkLocalBounds.Min );
	const FIntVector2 EndPos = HeightMapData->ChunkLocalPositionToPoint( ChunkLocalBounds.Max );

	for ( int32 PosX = StartPos.X; PosX <= EndPos.X; PosX++ )
	{
		for ( int32 PosY = StartPos.Y; PosY <= EndPos.Y; PosY++ )
		{
			const FVector ChunkLocalPosition = HeightMapData->PointToChunkLocalPosition( PosX, PosY, 0.0f );
			FChunkLandscapePoint LandscapePoint = SamplePointGrid_Local( ChunkLocalPosition );

			if ( !Operation( LandscapePoint ) )
			{
				return;
			}
		}
	}
}

void FChunkLandscapePointSampler::PopulatePointLayerWeights( FChunkLandscapePoint& OutPoint, const FChunkLandscapeWeight& Weight ) const
{
	float NormalizedWeights[FChunkLandscapeWeight::MaxWeightMapLayers] {};
	Weight.GetNormalizedWeights( NormalizedWeights );

	for ( int32 LayerIndex = 0; LayerIndex < WeightMapDescriptor->GetNumLayers(); LayerIndex++ )
	{
		if ( NormalizedWeights[ LayerIndex ] > 0.0f )
		{
			OutPoint.LayerWeights.Add( WeightMapDescriptor->GetLayerDescriptor( LayerIndex ), NormalizedWeights[ LayerIndex ] );
		}
	}
}

AOWGChunk::AOWGChunk() : NumChunkLandscapeLODs( 4 )
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRootComponent = CreateDefaultSubobject<USceneComponent>( TEXT("SceneRootComponent") );
	SceneRootComponent->SetMobility( EComponentMobility::Static );
	RootComponent = SceneRootComponent;
	
	HeightFieldCollisionComponent = CreateDefaultSubobject<UChunkHeightFieldCollisionComponent>( TEXT("CollisionComponent") );
	HeightFieldCollisionComponent->SetupAttachment( RootComponent );
	HeightFieldCollisionComponent->SetMobility( EComponentMobility::Static );
}

FChunkLandscapeMetrics FChunkLandscapeMetrics::Merge( const UObject* WorldContext, const TArray<FChunkLandscapeMetrics>& AllMetrics )
{
	const UOpenWorldGeneratorSubsystem* OpenWorldGeneratorSubsystem = UOpenWorldGeneratorSubsystem::Get( WorldContext );
	check( OpenWorldGeneratorSubsystem );
	
	// Calculate total amount of points across all metrics
	int32 TotalAmountOfPoints = 0;
	for ( const FChunkLandscapeMetrics& Metrics : AllMetrics )
	{
		TotalAmountOfPoints += Metrics.NumberOfPoints;
	}

	// Return empty metrics if we have not sampled a single point
	if ( TotalAmountOfPoints == 0 )
	{
		return FChunkLandscapeMetrics{};
	}

	FChunkLandscapeMetrics ResultMetrics;
	ResultMetrics.NumberOfPoints = TotalAmountOfPoints;

	ResultMetrics.MinimumHeightPoint.Z = UE_BIG_NUMBER;
	ResultMetrics.MaximumHeightPoint.Z = -UE_BIG_NUMBER;
	float TotalWeightMapWeight = 0.0f;

	// Sum up all metrics in the list to get the average
	for ( const FChunkLandscapeMetrics& SubMetrics : AllMetrics )
	{
		const float MetricsWeight = SubMetrics.NumberOfPoints / ( ResultMetrics.NumberOfPoints * 1.0f );
		ResultMetrics.MiddleHeightPoint += SubMetrics.MiddleHeightPoint * MetricsWeight;

		if ( ResultMetrics.MinimumHeightPoint.Z > SubMetrics.MinimumHeightPoint.Z )
		{
			ResultMetrics.MinimumHeightPoint = SubMetrics.MinimumHeightPoint;
		}
		if ( ResultMetrics.MaximumHeightPoint.Z < SubMetrics.MaximumHeightPoint.Z )
		{
			ResultMetrics.MaximumHeightPoint = SubMetrics.MaximumHeightPoint;
		}
		if ( ResultMetrics.MaximumSteepness < SubMetrics.MaximumSteepness )
		{
			ResultMetrics.MaximumSteepness = SubMetrics.MaximumSteepness;
		}

		for ( const TPair<UOWGChunkLandscapeLayer*, float>& WeightPair : SubMetrics.AverageWeights )
		{
			const float WeightedWeight = WeightPair.Value * MetricsWeight; // the name is silly
			ResultMetrics.AverageWeights.FindOrAdd( WeightPair.Key ) += WeightedWeight;
			TotalWeightMapWeight += WeightedWeight;
		}
	}

	// Normalize weights in the resulting metric. They are normalized in the individual metrics, but in the combined one they might not be.
	for ( TPair<UOWGChunkLandscapeLayer*, float>& WeightPair : ResultMetrics.AverageWeights )
	{
		WeightPair.Value /= TotalWeightMapWeight;
	}

	// Get the max landscape steepness by dividing absolute steepness by 
	ResultMetrics.MaximumSteepnessAbsolute = OpenWorldGeneratorSubsystem->GetWorldGeneratorDefinition()->MaxLandscapeSteepness * ResultMetrics.MaximumSteepness;
	return ResultMetrics;
}

void AOWGChunk::GetLifetimeReplicatedProps( TArray<FLifetimeProperty>& OutLifetimeProps ) const
{
	Super::GetLifetimeReplicatedProps( OutLifetimeProps );

	// TODO @open-world-generator: Replicate NoiseData and ChunkData2D
	DOREPLIFETIME( ThisClass, ChunkCoord );
}

void AOWGChunk::PostActorCreated()
{
	Super::PostActorCreated();

	// Only run this logic in game worlds - PostActorCreated happens in editor and preview worlds as well
	if ( GetWorld()->IsGameWorld() )
	{
		const UOpenWorldGeneratorSubsystem* OpenWorldGeneratorSubsystem = UOpenWorldGeneratorSubsystem::Get( GetWorld() );
		check( OpenWorldGeneratorSubsystem );
	
		WorldGeneratorDefinition = OpenWorldGeneratorSubsystem->GetWorldGeneratorDefinition();
		WorldSeed = OpenWorldGeneratorSubsystem->GetWorldSeed();
		check( WorldGeneratorDefinition );

		LandscapeMeshManager = MakeUnique<FChunkLandscapeMeshManager>( this );
		LandscapeMaterialManager = MakeUnique<FChunkLandscapeMaterialManager>( this, OpenWorldGeneratorSubsystem->GetChunkTextureManager() );
	}
}

void AOWGChunk::BeginPlay()
{
	Super::BeginPlay();

	const UOpenWorldGeneratorSubsystem* OpenWorldGeneratorSubsystem = UOpenWorldGeneratorSubsystem::Get( GetWorld() );
	check( OpenWorldGeneratorSubsystem );

	// Create the transient PCG component used for evaluating chunk generation graphs
	if ( !PCGComponent )
	{
		PCGComponent = NewObject<UPCGComponent>( this, TEXT("PCGComponent"), RF_Transient );
		PCGComponent->SetIsPartitioned( false );

		// Only trigger the component on demand, and use the world seed as the PCG component seed for deterministic generation results
		PCGComponent->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;
		PCGComponent->Seed = OpenWorldGeneratorSubsystem->GetWorldSeed();
		PCGComponent->RegisterComponent();
	}

	// Regenerate surface mesh from the heightmap
	if ( !LandscapeMeshComponent )
	{
		LandscapeMeshComponent = NewObject<UDynamicMeshComponent>( this, TEXT("LandscapeMeshComponent"), RF_Transient );
		LandscapeMeshComponent->SetupAttachment( SceneRootComponent, NAME_None );
		
		// do not generate collision data for the mesh. We use height field based collision instead.
		LandscapeMeshComponent->SetCollisionEnabled( ECollisionEnabled::NoCollision );

		LandscapeMeshComponent->RegisterComponent();
	}

	// Register the chunk in the subsystem's chunk manager
	if ( const TScriptInterface<IOWGChunkManagerInterface> ChunkManager = OpenWorldGeneratorSubsystem->GetChunkManager() )
	{
		ChunkManager->NotifyChunkBegunPlay( this );

		// Request chunk generation from the chunk manager in case it was requested before BeginPlay was dispatched
		RecalculateCurrentStageGenerators();
		if ( TargetGenerationStage > CurrentGenerationStage )
		{
			ChunkManager->RequestChunkGeneration( this );
		}
	}
}

void AOWGChunk::EndPlay( const EEndPlayReason::Type EndPlayReason )
{
	Super::EndPlay( EndPlayReason );

	// Notify the chunk manager that we are dying
	if ( const UOpenWorldGeneratorSubsystem* OpenWorldGeneratorSubsystem = UOpenWorldGeneratorSubsystem::Get( GetWorld() ) )
	{
		// Notify the owner region that we have been destroyed
		if ( OwnerContainer != nullptr )
		{
			OwnerContainer->NotifyChunkDestroyed( this );
		} 

		// Un-register from the chunk manager if there is one still
		if ( const TScriptInterface<IOWGChunkManagerInterface> ChunkManager = OpenWorldGeneratorSubsystem->GetChunkManager() )
		{
			ChunkManager->NotifyChunkDestroyed( this );
		}
	}

	// Release textures back to the pool
	LandscapeMaterialManager->ReleaseTextures();

	// Destroy actors that are associated with the chunk
	TArray<AActor*> ReferencedActors;
	CollectActorReferences( ReferencedActors );

	for ( AActor* ReferencedActor : ReferencedActors )
	{
		if ( ReferencedActor )
		{
			ReferencedActor->Destroy();
		}
	}
}

void AOWGChunk::CollectActorReferences( TArray<AActor*>& OutActorReferences ) const
{
	OutActorReferences.Append( ChunkChildActors );
}

void AOWGChunk::Serialize( FArchive& Ar )
{
	Super::Serialize( Ar );
	Ar.UsingCustomVersion( FOpenWorldGeneratorVersion::GUID );

	// Serialize noise data
	Ar << NoiseData;
	// Serialize generic 2D chunk data
	Ar << ChunkData2D;
	// Serialize weight map descriptor
	Ar << WeightMapDescriptor;
	// Serialize biome palette
	Ar << BiomePalette;
}

void AOWGChunk::AddReferencedObjects( UObject* InThis, FReferenceCollector& Collector )
{
	AOWGChunk* Chunk = CastChecked<AOWGChunk>( InThis );

	// Add references to the noise identifiers
	Collector.AddStableReferenceMap( Chunk->NoiseData );

	Chunk->BiomePalette.AddReferencedObjects( Collector );
	Chunk->WeightMapDescriptor.AddReferencedObjects( Collector );

	// Add references to managers
	if ( Chunk->LandscapeMaterialManager.IsValid() )
	{
		Chunk->LandscapeMaterialManager->AddReferencedObjects( Collector );
	}
	if ( Chunk->LandscapeMeshManager.IsValid() )
	{
		Chunk->LandscapeMeshManager->AddReferencedObjects( Collector );
	}
	if ( Chunk->CachedBiomeData )
	{
		Chunk->CachedBiomeData->BiomePalette.AddReferencedObjects( Collector );
	}
}

void AOWGChunk::SetupChunk( UOWGRegionContainer* InOwnerContainer, const FChunkCoord& InChunkCoord )
{
	check( InOwnerContainer );
	ChunkCoord = InChunkCoord;
	OwnerContainer = InOwnerContainer;
}

void AOWGChunk::OnChunkLoaded()
{
}

void AOWGChunk::OnChunkAboutToBeUnloaded()
{
	// Notify the currently running generator that we are about to let go
	if ( CurrentGeneratorInstance )
	{
		CurrentGeneratorInstance->NotifyAboutToUnloadChunk();
	}
}

bool AOWGChunk::ShouldDeferChunkUnloading() const
{
	// We should defer unloading if we have a current chunk generator that is willing to do that
	return CurrentGeneratorInstance && !CurrentGeneratorInstance->CanPersistChunkGenerator();
}

void AOWGChunk::OnChunkCreated()
{
	// Generate noise data for this chunk
	GenerateNoiseForChunk();

	// Aim for the surface generation stage immediately after the chunk creation
	RequestChunkGeneration( EChunkGeneratorStage::Surface );
}

bool AOWGChunk::IsChunkInitialized() const
{
	return ChunkData2D.Contains( ChunkDataID::SurfaceHeightmap ) && ChunkData2D.Contains( ChunkDataID::BiomeMap );
}

void AOWGChunk::RequestChunkGeneration( EChunkGeneratorStage InTargetGenerationStage )
{
	if ( TargetGenerationStage < InTargetGenerationStage )
	{
		TargetGenerationStage = InTargetGenerationStage;
	
		if ( HasActorBegunPlay() )
		{
			const UOpenWorldGeneratorSubsystem* OpenWorldGeneratorSubsystem = UOpenWorldGeneratorSubsystem::Get( GetWorld() );
		
			if ( const TScriptInterface<IOWGChunkManagerInterface> ChunkManager = OpenWorldGeneratorSubsystem->GetChunkManager() )
			{
				ChunkManager->RequestChunkGeneration( this );
			}
		}
	}
}

FTerraformingPrecision AOWGChunk::GetNativeLandscapePrecision() const
{
	const FChunkData2D& HeightMapData = ChunkData2D.FindChecked( ChunkDataID::SurfaceHeightmap );
	return FTerraformingPrecision::DynamicGrid( FChunkCoord::ChunkSizeWorldUnits / ( HeightMapData.GetSurfaceResolutionXY() - 1 ) );
}

FChunkLandscapePoint AOWGChunk::GetLandscapePoint( const FVector& WorldLocation ) const
{
	// Guard against uninitialized chunks
	if ( IsChunkInitialized() )
	{
		const FChunkLandscapePointSampler PointSampler( this );
		return PointSampler.SamplePointInterpolated( WorldLocation );
	}
	return FChunkLandscapePoint{};
}

void AOWGChunk::GetLandscapePoints( const FVector& WorldLocation, const FPolymorphicTerraformingBrush& Brush, TArray<FChunkLandscapePoint>& OutPoints, float MinWeight )
{
	check( false );
}

FChunkLandscapeMetrics AOWGChunk::GetLandscapeMetrics( const FVector& WorldLocation, const FPolymorphicTerraformingBrush& Brush, bool bIncludeWeights, float MinWeight )
{
	check( false );
	SCOPE_CYCLE_COUNTER( STAT_ChunkGetLandscapeMetrics );

	const FChunkData2D& HeightMapData = ChunkData2D.FindChecked( ChunkDataID::SurfaceHeightmap );
	const FChunkData2D& SteepnessData = ChunkData2D.FindChecked( ChunkDataID::SurfaceSteepness );
	const FChunkData2D* SurfaceWeightMap = ChunkData2D.Find( ChunkDataID::SurfaceWeights );

	const FTransform ChunkTransform = GetActorTransform();
	const FVector ChunkLocalOrigin = ChunkTransform.InverseTransformPosition( WorldLocation );

	const int32 ChunkDataSize = HeightMapData.GetSurfaceResolutionXY();
	constexpr float GridOriginOffset = FChunkCoord::ChunkSizeWorldUnits / 2.0f;
	const float GridCellSize = FChunkCoord::ChunkSizeWorldUnits / ( ChunkDataSize - 1 );

	FIntPoint GridStartXY;
	FIntVector2 GridSizeXY;
	TArray<float> BrushPointWeights;
	FBox2f BrushBounds;
	Brush->RenderBrushToSizedGrid( FVector2f( ChunkLocalOrigin.X, ChunkLocalOrigin.Y ), GridOriginOffset, GridCellSize, GridStartXY, GridSizeXY, BrushPointWeights, &BrushBounds );

	// Sample points along the grid to build the metric
	FChunkLandscapeMetrics ResultMetrics;
	int32 AverageWeightMapLayerWeights[FChunkLandscapeWeight::MaxWeightMapLayers] {};

	ResultMetrics.MinimumHeightPoint.Z = UE_BIG_NUMBER;
	ResultMetrics.MaximumHeightPoint.Z = -UE_BIG_NUMBER;
	
	const int32 NumLayers = WeightMapDescriptor.GetNumLayers();

	// Sample landscape at each point
	for ( int32 ChunkDataX = FMath::Max( GridStartXY.X, 0 ); ChunkDataX < FMath::Min( GridStartXY.X + GridSizeXY.X, ChunkDataSize ); ChunkDataX++ )
	{
		for ( int32 ChunkDataY = FMath::Max( GridStartXY.Y, 0 ); ChunkDataY < FMath::Min( GridStartXY.Y + GridSizeXY.Y, ChunkDataSize ); ChunkDataY++ )
		{
			const int32 BrushX = ChunkDataX - GridStartXY.X;
			const int32 BrushY = ChunkDataY - GridStartXY.Y;

			// Filter out positions that are below the minimum weight
			const float PointWeight = BrushPointWeights[ GridSizeXY.X * BrushY + BrushX ];
			if ( PointWeight == 0.0f || PointWeight < MinWeight )
			{
				continue;
			}

			// Calculate height and steepness at the given point
			const float PointHeight = HeightMapData.GetElementAt<float>( ChunkDataX, ChunkDataY );
			const float PointSteepness = SteepnessData.GetElementAt<float>( ChunkDataX, ChunkDataY );
			
			const FVector PointWorldLocation = ChunkTransform.TransformPosition( FVector( ChunkDataX, ChunkDataY, PointHeight ) );
			
			ResultMetrics.NumberOfPoints++;
			ResultMetrics.MiddleHeightPoint += PointWorldLocation;

			if ( ResultMetrics.MinimumHeightPoint.Z > PointWorldLocation.Z )
			{
				ResultMetrics.MinimumHeightPoint = PointWorldLocation;
			}
			if ( ResultMetrics.MaximumHeightPoint.Z < PointWorldLocation.Z )
			{
				ResultMetrics.MaximumHeightPoint = PointWorldLocation;
			}
			if ( ResultMetrics.MaximumSteepness < PointSteepness )
			{
				ResultMetrics.MaximumSteepness = PointSteepness;
			}

			// Calculate average weight map layers if we have been asked to
			if ( SurfaceWeightMap && bIncludeWeights )
			{
				const FChunkLandscapeWeight Weight = SurfaceWeightMap->GetElementAt<FChunkLandscapeWeight>( ChunkDataX, ChunkDataY );
				for ( int32 LayerIndex = 0; LayerIndex < NumLayers; LayerIndex++ )
				{
					AverageWeightMapLayerWeights[ LayerIndex ] += Weight.LayerWeights[ LayerIndex ];
				}
			}
		}
	}

	// Divide average values by the number of points sampled
	if ( ResultMetrics.NumberOfPoints > 0 )
	{
		ResultMetrics.MiddleHeightPoint /= ResultMetrics.NumberOfPoints;
		ResultMetrics.MaximumSteepnessAbsolute = WorldGeneratorDefinition->MaxLandscapeSteepness * ResultMetrics.MaximumSteepness;

		// Average out the weights
		if ( SurfaceWeightMap && bIncludeWeights )
		{
			int32 TotalLayerWeights = 0;
			for ( int32 LayerIndex = 0; LayerIndex < NumLayers; LayerIndex++ )
			{
				AverageWeightMapLayerWeights[ LayerIndex ] /= ResultMetrics.NumberOfPoints;
				TotalLayerWeights += AverageWeightMapLayerWeights[ LayerIndex ];
			}
			for ( int32 LayerIndex = 0; LayerIndex < NumLayers; LayerIndex++ )
			{
				const float RelativeWeight = AverageWeightMapLayerWeights[ LayerIndex ] * 1.0f / TotalLayerWeights;
				ResultMetrics.AverageWeights.Add( WeightMapDescriptor.GetLayerDescriptor( LayerIndex ), RelativeWeight );
			}
		}
	}
	else
	{
		ResultMetrics.MinimumHeightPoint = FVector::ZeroVector;
		ResultMetrics.MaximumHeightPoint = FVector::ZeroVector;
	}
	return ResultMetrics;
}

void AOWGChunk::ModifyLandscapeHeightsInternal( const FVector& WorldLocation, const FPolymorphicTerraformingBrush& Brush, float NewLandscapeHeight, float MinWeight )
{
	FChunkData2D& HeightMapData = ChunkData2D.FindChecked( ChunkDataID::SurfaceHeightmap );

	const FTransform ChunkTransform = GetActorTransform();
	const FVector ChunkLocalOrigin = ChunkTransform.InverseTransformPosition( WorldLocation );

	const int32 ChunkDataSize = HeightMapData.GetSurfaceResolutionXY();
	constexpr float GridOriginOffset = FChunkCoord::ChunkSizeWorldUnits / 2.0f;
	const float GridCellSize = FChunkCoord::ChunkSizeWorldUnits / ( ChunkDataSize - 1 );

	FIntPoint GridStartXY;
	FIntVector2 GridSizeXY;
	TArray<float> BrushPointWeights;
	FBox2f BrushBounds;
	Brush->RenderBrushToSizedGrid( FVector2f( ChunkLocalOrigin.X, ChunkLocalOrigin.Y ), GridOriginOffset, GridCellSize, GridStartXY, GridSizeXY, BrushPointWeights, &BrushBounds );

	int32 PointsModified = 0;

	// Modify the landscape at each given point.
	for ( int32 ChunkDataX = FMath::Max( GridStartXY.X, 0 ); ChunkDataX < FMath::Min( GridStartXY.X + GridSizeXY.X, ChunkDataSize ); ChunkDataX++ )
	{
		for ( int32 ChunkDataY = FMath::Max( GridStartXY.Y, 0 ); ChunkDataY < FMath::Min( GridStartXY.Y + GridSizeXY.Y, ChunkDataSize ); ChunkDataY++ )
		{
			const int32 BrushX = ChunkDataX - GridStartXY.X;
			const int32 BrushY = ChunkDataY - GridStartXY.Y;

 			// Filter out positions that are below the minimum weight
			const float PointWeight = BrushPointWeights[ GridSizeXY.X * BrushY + BrushX ];
			if ( PointWeight == 0.0f || PointWeight < MinWeight )
			{
				continue;
			}

			// Update the height map value at the position!
			const float CurrentHeight = HeightMapData.GetElementAt<float>( ChunkDataX, ChunkDataY );
			const float NewPointHeight = FMath::InterpSinInOut( CurrentHeight, NewLandscapeHeight, PointWeight );
			HeightMapData.SetElementAt<float>( ChunkDataX, ChunkDataY, NewPointHeight );
			PointsModified++;
		}
	}

	// Perform partial update of the layers and weights at the given location
	if ( PointsModified > 0 )
	{
		PartialRecalculateSurfaceData( BrushBounds );

		if ( CVarChunkVisualizeLandscapeEditBounds.GetValueOnGameThread() )
		{
			// Debug drawing of update area
			const FVector2f LocalCenter = BrushBounds.GetCenter();
			const FChunkLandscapePoint ChunkLandscapePoint = GetLandscapePoint( WorldLocation );
		
			const FVector BoxExtents = FVector( BrushBounds.GetExtent().X, BrushBounds.GetExtent().Y, 300.0f );
			const FVector BoxCenter = GetActorLocation() + FVector( LocalCenter.X, LocalCenter.Y, ChunkLandscapePoint.Transform.GetLocation().Z );

			DrawDebugSolidBox( GetWorld(), BoxCenter, BoxExtents, FColor::Blue, false, 30.0f );			
		}
	}
}

void AOWGChunk::ModifyLandscapeWeightsInternal( const FVector& WorldLocation, const FPolymorphicTerraformingBrush& Brush, const FChunkLandscapeWeight& NewLandscapeWeight, float MinWeight )
{
	FChunkData2D& WeightMapData = ChunkData2D.FindChecked( ChunkDataID::SurfaceWeights );

	const FTransform ChunkTransform = GetActorTransform();
	const FVector ChunkLocalOrigin = ChunkTransform.InverseTransformPosition( WorldLocation );

	const int32 ChunkDataSize = WeightMapData.GetSurfaceResolutionXY();
	constexpr float GridOriginOffset = FChunkCoord::ChunkSizeWorldUnits / 2.0f;
	const float GridCellSize = FChunkCoord::ChunkSizeWorldUnits / ( ChunkDataSize - 1 );

	FIntPoint GridStartXY;
	FIntVector2 GridSizeXY;
	TArray<float> BrushPointWeights;
	FBox2f BrushBounds;
	Brush->RenderBrushToSizedGrid( FVector2f( ChunkLocalOrigin.X, ChunkLocalOrigin.Y ), GridOriginOffset, GridCellSize, GridStartXY, GridSizeXY, BrushPointWeights, &BrushBounds );

	int32 PointsModified = 0;

	// Modify the landscape at each given point.
	for ( int32 ChunkDataX = FMath::Max( GridStartXY.X, 0 ); ChunkDataX < FMath::Min( GridStartXY.X + GridSizeXY.X, ChunkDataSize ); ChunkDataX++ )
	{
		for ( int32 ChunkDataY = FMath::Max( GridStartXY.Y, 0 ); ChunkDataY < FMath::Min( GridStartXY.Y + GridSizeXY.Y, ChunkDataSize ); ChunkDataY++ )
		{
			const int32 BrushX = ChunkDataX - GridStartXY.X;
			const int32 BrushY = ChunkDataY - GridStartXY.Y;

			// Filter out positions that are below the minimum weight
			const float PointWeight = BrushPointWeights[ GridSizeXY.X * BrushY + BrushX ];
			if ( PointWeight == 0.0f || PointWeight < MinWeight )
			{
				continue;
			}

			// Update the weight map value at the position!
			const FChunkLandscapeWeight CurrentWeight = WeightMapData.GetElementAt<FChunkLandscapeWeight>( ChunkDataX, ChunkDataY );
			const FChunkLandscapeWeight NewPointHeight = FMath::Lerp( CurrentWeight, NewLandscapeWeight, PointWeight );
			WeightMapData.SetElementAt<FChunkLandscapeWeight>( ChunkDataX, ChunkDataY, NewPointHeight );
			PointsModified++;
		}
	}

	// Perform partial update of the layers and weights at the given location
	if ( PointsModified > 0 )
	{
		PartialUpdateWeightMap( BrushBounds );
	}
}

void AOWGChunk::ModifyLandscape( const FVector& WorldLocation, const FPolymorphicTerraformingBrush& Brush, const FChunkLandscapeModification& LandscapeModification, float MinWeight )
{
	check( false );
	SCOPE_CYCLE_COUNTER( STAT_ChunkModifyLandscape );

	// Landscape modifications that do not overlap with the chunk bounding box are pointless
	const FVector2f ChunkWorldLocation = FVector2f( FVector3f( GetActorLocation() ) );
	const FVector2f ChunkExtents = FVector2f( FChunkCoord::ChunkSizeWorldUnits / 2.0f, FChunkCoord::ChunkSizeWorldUnits / 2.0f );
	const FBox2f ChunkBoundingBox( ChunkWorldLocation - ChunkExtents, ChunkWorldLocation + ChunkExtents );

	const FVector2f BrushWorldLocation = FVector2f( FVector3f( WorldLocation ) );
	const FVector2f BrushWorldExtents = Brush->GetBrushExtents();
	const FBox2f BrushBoundingBox( BrushWorldLocation - BrushWorldExtents, BrushWorldLocation + BrushWorldExtents );

	// Exit early if chunk bounding box does not intersect with the brush bounding box
	if ( !ChunkBoundingBox.Intersect( BrushBoundingBox ) )
	{
		return;
	}

	// Modify heights if we are asked to do so
	if ( LandscapeModification.bModifyHeight && ChunkData2D.Contains( ChunkDataID::SurfaceHeightmap ) )
	{
		ModifyLandscapeHeightsInternal( WorldLocation, Brush, LandscapeModification.NewHeight, MinWeight );
	}

	// Modify weight map if we have weights
	if ( !LandscapeModification.NewLayers.IsEmpty() && ChunkData2D.Contains( ChunkDataID::SurfaceWeights ) )
	{
		// Create weight map layers for the new entries we want to add, and populate their weights
		FChunkLandscapeWeight NewLandscapeWeight;
		for ( const TPair<UOWGChunkLandscapeLayer*, float>& LayerWeightPair : LandscapeModification.NewLayers )
		{
			const int32 LayerIndex = WeightMapDescriptor.FindOrCreateLayer( LayerWeightPair.Key );
			if ( LayerIndex != INDEX_NONE )
			{
				NewLandscapeWeight.LayerWeights[ LayerIndex ] = (uint8) FMath::Clamp( FMath::RoundToInt32( LayerWeightPair.Value * 255.0f ), 0, 255 );
			}
		}
		ModifyLandscapeWeightsInternal( WorldLocation, Brush, NewLandscapeWeight, MinWeight );
	}
}

TArray<UOWGChunkLandscapeLayer*> AOWGChunk::GetLandscapeLayers() const
{
	return WeightMapDescriptor.GetAllLayers();
}

void AOWGChunk::AddChunkChildActor( AActor* InChunkChildActor )
{
	if ( IsValid( InChunkChildActor ) && ensureMsgf( Cast<AOWGChunk>( InChunkChildActor->GetOwner() ) == nullptr, TEXT("Attempt to add Actor '%s' as a Child Actor to Chunk '%s', but it is already owned by another Chunk"), *InChunkChildActor->GetName(), *GetName() ) )
	{
		InChunkChildActor->SetOwner( this );
		ChunkChildActors.AddUnique( InChunkChildActor );
	}
}

void AOWGChunk::RequestChunkLOD( int32 NewChunkLODIndex )
{
	// Allow overriding chunk LOD levels
	const int32 ChunkLODOverride = CVarChunkLODOverride.GetValueOnGameThread();
	if ( ChunkLODOverride != INDEX_NONE )
	{
		NewChunkLODIndex = ChunkLODOverride;
	}

	// Only perform LOD updates when the value is actually different, and if the chunk is already initialized (uninitialized LOD swaps are bad)
	if ( NewChunkLODIndex != CurrentChunkLOD && ChunkData2D.Contains( ChunkDataID::SurfaceHeightmap ) )
	{
		CurrentChunkLOD = NewChunkLODIndex;

		LandscapeMeshManager->OnChunkLODLevelChanged();
		LandscapeMaterialManager->OnChunkLODLevelChanged();
	}
}

float AOWGChunk::GetNoiseValueAtLocation( const FVector& WorldLocation, const UOWGNoiseIdentifier* NoiseIdentifier ) const
{
	if ( const FChunkData2D* ChunkNoiseData = FindRawNoiseData( NoiseIdentifier ) )
	{
		const FVector2f NormalizedPosition = FChunkData2D::ChunkLocalPositionToNormalized( GetActorTransform().InverseTransformPosition( WorldLocation ) );
		return ChunkNoiseData->GetInterpolatedElementAt<float>( NormalizedPosition );
	}
	return 0.0f;
}

const FChunkData2D* AOWGChunk::FindRawNoiseData( const UOWGNoiseIdentifier* NoiseIdentifier ) const
{
	return NoiseData.Find( NoiseIdentifier );
}

const FChunkData2D* AOWGChunk::FindRawChunkData( FName ChunkDataID ) const
{
	return ChunkData2D.Find( ChunkDataID );
}

void AOWGChunk::GenerateNoiseForChunk()
{
	SCOPE_CYCLE_COUNTER( STAT_GenerateNoiseForChunk );

	// Generate noise for each identifier
	for ( const TPair<UOWGNoiseIdentifier*, UOWGNoiseGenerator*>& Pair : WorldGeneratorDefinition->NoiseGenerators )
	{
		if ( Pair.Key && Pair.Value && !NoiseData.Contains( Pair.Key ) )
		{
			// Allocate space for one additional row/column so we can seamlessly interpolate noise from adjacent chunks
			FChunkData2D NewNoiseData = FChunkData2D::Create<float>( WorldGeneratorDefinition->NoiseResolutionXY, true );
			Pair.Value->GenerateNoise( WorldSeed, ChunkCoord, NewNoiseData.GetSurfaceResolutionXY(), NewNoiseData.GetMutableDataPtr<float>() );

			NoiseData.Emplace( Pair.Key, MoveTemp( NewNoiseData ) );
		}
	}
}

bool AOWGChunk::ProcessChunkGeneration()
{
	SCOPE_CYCLE_COUNTER( STAT_ProcessChunkGeneration );

	// Generate each stage from the current one until we reach the start of the target stage
	while ( CurrentGenerationStage <= TargetGenerationStage )
	{
		// Execute each generator in sequence starting from the current one until we are done with all of them
		while ( CurrentGeneratorIndex < CurrentStageChunkGenerators.Generators.Num() )
		{
			const TSubclassOf<UOWGChunkGenerator> GeneratorType = CurrentStageChunkGenerators.Generators[ CurrentGeneratorIndex ];

			// Only allocate a new chunk generator if we don't have one already. If we have one, it has some internal state
			if ( !CurrentGeneratorInstance || CurrentGeneratorInstance->GetClass() != GeneratorType )
			{
				// Do not attempt to start any new chunk generators when we are pending to be unloaded. Wrapping up the existing ones is okay and should still happen.
				if ( bPendingToBeUnloaded )
				{
					// Return true instead of returning false because while we are in the "pending unload" state we can become relevant again if the streaming state changes,
					// and if we return false here we will completely stop ticking the generation until we request it again, which is not what we want here
					return true;
				}
				check( GeneratorType );

				CurrentGeneratorInstance = NewObject<UOWGChunkGenerator>( this, GeneratorType );
				check( CurrentGeneratorInstance );
				CurrentGeneratorInstance->TargetBiomes = CurrentStageChunkGenerators.GeneratorInstigatorBiomes.FindOrAdd( GeneratorType );
			}
			// Abort the execution if the current generator is waiting for some condition
			if ( !CurrentGeneratorInstance->AdvanceChunkGeneration() )
			{
				return true;
			}

			// The generator returned false, that means it's done and we can advance to the next one
			CurrentGeneratorInstance->EndChunkGeneration();

			// Destroy the generator so that the save system does not try to save it
			CurrentGeneratorInstance->SetFlags( RF_Transient );
			CurrentGeneratorInstance->MarkAsGarbage();
			CurrentGeneratorInstance = nullptr;

			// Advance the index
			CurrentGeneratorIndex++;
		}

		// Advance to the next generation stage
		CurrentGenerationStage = (EChunkGeneratorStage) ( (int32) CurrentGenerationStage + 1 );
		CurrentGeneratorIndex = 0;
		RecalculateCurrentStageGenerators();
	}

	// We're done, nothing else to generate for now
	return false;
}

void AOWGChunk::DrawDebugHUD( AHUD* HUD, UCanvas* Canvas, const FDebugDisplayInfo& DisplayInfo ) const
{
	const FVector PlayerLocation = HUD->GetOwningPawn()->GetActorLocation();
	const FVector ChunkRelativePlayerLocation = PlayerLocation - ChunkCoord.ToOriginWorldLocation() + FVector( FChunkCoord::FChunkCoord::ChunkSizeWorldUnits / 2.0f );
	FDisplayDebugManager& DisplayDebugManager = Canvas->DisplayDebugManager;

	const UEnum* ChunkStageEnum = StaticEnum<EChunkGeneratorStage>();
	DisplayDebugManager.DrawString( FString::Printf( TEXT("Current/Target Generation Stage: %s/%s"),
		*ChunkStageEnum->GetNameStringByValue( (int64) CurrentGenerationStage ),
		*ChunkStageEnum->GetNameStringByValue( (int64) TargetGenerationStage ) ) );

	DisplayDebugManager.DrawString( FString::Printf( TEXT("Chunk LOD: %d"), CurrentChunkLOD ) );
	const int32 HeightmapResolution = WorldGeneratorDefinition->NoiseResolutionXY;

	const int32 NoisePosX = FMath::Clamp( FMath::RoundToInt32( ChunkRelativePlayerLocation.X / FChunkCoord::ChunkSizeWorldUnits * ( HeightmapResolution - 1 ) ), 0, HeightmapResolution - 1 );
	const int32 NoisePosY = FMath::Clamp( FMath::RoundToInt32( ChunkRelativePlayerLocation.Y / FChunkCoord::ChunkSizeWorldUnits * ( HeightmapResolution - 1 ) ), 0, HeightmapResolution - 1 );

	// Draw noise information for the cell
	if ( !NoiseData.IsEmpty() )
	{
		TArray<FString> NoiseDataEntries;
		for ( const TPair<UOWGNoiseIdentifier*, FChunkData2D>& Pair : NoiseData )
		{
			NoiseDataEntries.Add( FString::Printf( TEXT("%s: %.2f"), *Pair.Key->DebugName.ToString(), Pair.Value.GetElementAt<float>( NoisePosX, NoisePosY ) ) );
		}
		DisplayDebugManager.DrawString( FString::Printf( TEXT("Noise: %s"), *FString::Join( NoiseDataEntries, TEXT("; ") ) ) );
	}

	// Add terrain height if we can sample it
	float TerrainHeight = 0.0f;
	if ( const FChunkData2D* TerrainHeightData = ChunkData2D.Find( ChunkDataID::SurfaceHeightmap ) )
	{
		TerrainHeight = TerrainHeightData->GetElementAt<float>( NoisePosX, NoisePosY );
		DisplayDebugManager.DrawString( FString::Printf( TEXT("Terrain Height: %.2f\n"), TerrainHeight ) );
	}
	if ( const FChunkData2D* TerrainSteepnessData = ChunkData2D.Find( ChunkDataID::SurfaceGradient ) )
	{
		const FVector2f TerrainGradient = TerrainSteepnessData->GetElementAt<FVector2f>( NoisePosX, NoisePosY );
		DisplayDebugManager.DrawString( FString::Printf( TEXT("Terrain Steepness: %.2f\n"), TerrainGradient.Size() ) );
	}

	const FChunkData2D* TerrainNormalData = ChunkData2D.Find( ChunkDataID::SurfaceNormal );
	if ( TerrainNormalData != nullptr )
	{
		const FVector3f TerrainNormal = TerrainNormalData->GetElementAt<FVector3f>( NoisePosX, NoisePosY );
		DisplayDebugManager.DrawString( FString::Printf( TEXT("Terrain Normal: %s\n"), *TerrainNormal.ToCompactString() ) );

		const FVector WorldLocation = GetActorTransform().TransformPosition( FVector(
			NoisePosX * FChunkCoord::ChunkSizeWorldUnits / ( HeightmapResolution - 1) - FChunkCoord::ChunkSizeWorldUnits / 2.0f,
			NoisePosY * FChunkCoord::ChunkSizeWorldUnits / ( HeightmapResolution - 1) - FChunkCoord::ChunkSizeWorldUnits / 2.0f, TerrainHeight ) );

		DrawDebugDirectionalArrow( GetWorld(), WorldLocation, WorldLocation + FVector( TerrainNormal ) * 300.0f, 12.0f, FColor::Red, false, 5.0f );
	}

	const FChunkData2D* WeightMapData = ChunkData2D.Find( ChunkDataID::SurfaceWeights );
	if ( WeightMapData != nullptr )
	{
		const FChunkLandscapeWeight LandscapeWeight = WeightMapData->GetElementAt<FChunkLandscapeWeight>( NoisePosX, NoisePosY );
		const UOWGChunkLandscapeLayer* LandscapeLayer = WeightMapDescriptor.GetLayerDescriptor( LandscapeWeight.GetLayerWithLargestContribution() );

		DisplayDebugManager.DrawString( FString::Printf( TEXT("Most Contributing Layer: %s"), *GetNameSafe( LandscapeLayer ) ) );
	}

	const FChunkData2D* BiomeMap = ChunkData2D.Find( ChunkDataID::BiomeMap );
	if ( BiomeMap != nullptr )
	{
		const FBiomePaletteIndex BiomePaletteIndex = BiomeMap->GetElementAt<FBiomePaletteIndex>( NoisePosX, NoisePosY );
		const UOWGBiome* Biome = BiomePalette.GetBiomeByIndex( BiomePaletteIndex );

		DisplayDebugManager.DrawString( FString::Printf( TEXT("Biome: %s"), *GetNameSafe( Biome ) ) );
	}
}

void AOWGChunk::PartialRecalculateSurfaceData( const FBox2f& UpdateVolume )
{
	if ( ChunkData2D.Contains( ChunkDataID::SurfaceHeightmap ) )
	{
		// Convert chunk space update volume to the height map grid coordinates
		const int32 GridSize = ChunkData2D.FindChecked( ChunkDataID::SurfaceHeightmap ).GetSurfaceResolutionXY();
		const float GridCellSize = FChunkCoord::ChunkSizeWorldUnits / ( GridSize - 1 );
		constexpr float GridOriginOffset = FChunkCoord::ChunkSizeWorldUnits / 2.0f;

		const int32 StartX = FMath::FloorToInt32( ( UpdateVolume.Min.X + GridOriginOffset ) / GridCellSize );
		const int32 StartY = FMath::FloorToInt32( ( UpdateVolume.Min.Y + GridOriginOffset ) / GridCellSize );

		const int32 EndX = FMath::CeilToInt32( ( UpdateVolume.Max.X + GridOriginOffset ) / GridCellSize );
		const int32 EndY = FMath::CeilToInt32( ( UpdateVolume.Max.Y + GridOriginOffset ) / GridCellSize );

		// Gradients use forward differencing, so when we want to update surface data for the particular cell what we actually want is to update it for the cells next to it
		// We also want one cell back because we use backwards differencing for cells at -X-Y
		PartialUpdateSurfaceGradient( StartX - 1, StartY - 1, EndX + 1, EndY + 1 );

		// Update normals only for the chanced cells
		PartialUpdateSurfaceNormal( StartX, StartY, EndX, EndY );

		// Update or create height field collision for affected cells
		HeightFieldCollisionComponent->PartialUpdateOrCreateHeightField( StartX, StartY, EndX, EndY );

		// Invalidate current landscape mesh changelist. We will regenerate the mesh as needed
		LandscapeMeshManager->InvalidateLandscapeMesh();

		/** Heightmap changed, grass cached data is no longer up to date */
		GrassSourceDataChangelistNumber++;
	}
}

void AOWGChunk::PartialUpdateWeightMap( const FBox2f& UpdateVolume )
{
	if ( const FChunkData2D* WeightMapData = ChunkData2D.Find( ChunkDataID::SurfaceWeights ) )
	{
		// Convert chunk space update volume to the height map grid coordinates
		const int32 GridSize = WeightMapData->GetSurfaceResolutionXY();
		constexpr float GridOriginOffset = FChunkCoord::ChunkSizeWorldUnits / 2.0f;

		const int32 StartX = FMath::FloorToInt32( ( ( UpdateVolume.Min.X + GridOriginOffset ) / FChunkCoord::ChunkSizeWorldUnits ) * ( GridSize - 1 ) );
		const int32 StartY = FMath::FloorToInt32( ( ( UpdateVolume.Min.Y + GridOriginOffset ) / FChunkCoord::ChunkSizeWorldUnits ) * ( GridSize - 1 ) );
		
		const int32 EndX = FMath::CeilToInt32( ( ( UpdateVolume.Max.X + GridOriginOffset ) / FChunkCoord::ChunkSizeWorldUnits ) * ( GridSize - 1 ) );
		const int32 EndY = FMath::CeilToInt32( ( ( UpdateVolume.Max.Y + GridOriginOffset ) / FChunkCoord::ChunkSizeWorldUnits ) * ( GridSize - 1 ) );

		LandscapeMaterialManager->PartialUpdateWeightMap( StartX, StartY, EndX, EndY );

		/** Weightmap changed, grass cached data is no longer up to date */
		GrassSourceDataChangelistNumber++;
	}
}

void AOWGChunk::PartialUpdateSurfaceGradient( int32 StartX, int32 StartY, int32 EndX, int32 EndY )
{
	const FChunkData2D& SurfaceHeightmapData = ChunkData2D.FindChecked( ChunkDataID::SurfaceHeightmap );
	const int32 ResolutionXY = SurfaceHeightmapData.GetSurfaceResolutionXY();
	const float MaxSurfaceSteepness = WorldGeneratorDefinition->MaxLandscapeSteepness;

	// Create data entries if we do not have them already
	if ( !ChunkData2D.Contains( ChunkDataID::SurfaceGradient ) )
	{
		ChunkData2D.Emplace( ChunkDataID::SurfaceGradient, FChunkData2D::Create<FVector2f>( ResolutionXY, true ) );
	}
	if ( !ChunkData2D.Contains( ChunkDataID::SurfaceSteepness ) )
	{
		ChunkData2D.Emplace( ChunkDataID::SurfaceSteepness, FChunkData2D::Create<float>( ResolutionXY, true ) );
	}

	// Retrieve the data from the layers now
	FChunkData2D& SurfaceGradientData = ChunkData2D.FindChecked( ChunkDataID::SurfaceGradient );
	FChunkData2D& SurfaceSteepnessData = ChunkData2D.FindChecked( ChunkDataID::SurfaceSteepness );

	// Do forward differencing for each cell except for the border at X+Y+:
	// dx = f(x+1) - f(x)
	// dy = f(y+1) - f(y)
	// steepness = sqrt(dx^2+dy^2)
	for ( int32 PosX = FMath::Max( StartX, 0 ); PosX < FMath::Min( EndX + 1, ResolutionXY - 1 ); PosX++ )
	{
		for ( int32 PosY = FMath::Max( StartY, 0 ); PosY < FMath::Min( EndY + 1, ResolutionXY - 1 ); PosY++ )
		{
			const float X0Y0 = SurfaceHeightmapData.GetElementAt<float>( PosX, PosY );
			const float XPY0 = SurfaceHeightmapData.GetElementAt<float>( PosX + 1, PosY );
			const float X0YP = SurfaceHeightmapData.GetElementAt<float>( PosX, PosY + 1 );

			// Calculate the points now
			const FVector2f ResultGradient( XPY0 - X0Y0, X0Y0 - X0YP );
			SurfaceGradientData.SetElementAt( PosX, PosY, ResultGradient.GetSafeNormal() );
			SurfaceSteepnessData.SetElementAt( PosX, PosY, FMath::Min( ResultGradient.Size() / MaxSurfaceSteepness, 1.0f ) );
		}
	}

	// Do backwards differencing for the cells at the X+Y+ border. We do not strife to provide accurate data for that particular corner though, as it technically belongs to another chunk.
	// dx = f(x) - f(x-1)
	// dy = f(y) - f(y-1)
	// steepness = sqrt(dx^2+dy^2)
	for ( int32 Pos = 0; Pos < ResolutionXY - 1; Pos++ )
	{
		// Border at +X. Check if it is in the bounds
		if ( StartX <= ( ResolutionXY - 1 ) && ( ResolutionXY - 1 ) <= EndX && StartY >= Pos && Pos <= EndY )
		{
			const float X0Y0 = SurfaceHeightmapData.GetElementAt<float>( ResolutionXY - 1, Pos );
			const float XNY0 = SurfaceHeightmapData.GetElementAt<float>( ResolutionXY - 2, Pos );
			const float X0YP = SurfaceHeightmapData.GetElementAt<float>( ResolutionXY - 1, Pos + 1 );

			// Calculate the points now
			const FVector2f ResultGradient( X0Y0 - XNY0, X0YP - X0Y0 );
			SurfaceGradientData.SetElementAt( ResolutionXY - 1, Pos, ResultGradient.GetSafeNormal() );
			SurfaceSteepnessData.SetElementAt( ResolutionXY - 1, Pos, FMath::Min( ResultGradient.Size() / MaxSurfaceSteepness, 1.0f ) );
		}

		// Border at +Y
		if ( StartX <= Pos && Pos <= EndY && StartY <= ( ResolutionXY - 1 ) && EndY <= ( ResolutionXY - 1 ) )
		{
			const float X0Y0 = SurfaceHeightmapData.GetElementAt<float>( Pos, ResolutionXY - 1 );
			const float XPY0 = SurfaceHeightmapData.GetElementAt<float>( Pos + 1, ResolutionXY - 1 );
			const float X0YN = SurfaceHeightmapData.GetElementAt<float>( ResolutionXY - 1, Pos + 1 );

			// Calculate the points now
			const FVector2f ResultGradient( XPY0 - X0Y0, X0Y0 - X0YN );
			SurfaceGradientData.SetElementAt( Pos, ResolutionXY - 1, ResultGradient.GetSafeNormal() );
			SurfaceSteepnessData.SetElementAt( Pos, ResolutionXY - 1, FMath::Min( ResultGradient.Size() / MaxSurfaceSteepness, 1.0f ) );
		}
	}

	// Corner at +X+Y
	if ( StartX <= ( ResolutionXY - 1 ) && EndY <= ( ResolutionXY - 1 ) && StartY <= ( ResolutionXY - 1 ) && EndY <= ( ResolutionXY - 1 ) )
	{
		const float X0Y0 = SurfaceHeightmapData.GetElementAt<float>( ResolutionXY - 1, ResolutionXY - 1 );
		const float XNY0 = SurfaceHeightmapData.GetElementAt<float>( ResolutionXY - 2, ResolutionXY - 1 );
		const float X0YN = SurfaceHeightmapData.GetElementAt<float>( ResolutionXY - 1, ResolutionXY - 2 );

		// Calculate the points now
		const FVector2f ResultGradient( X0Y0 - XNY0, X0Y0 - X0YN );
		SurfaceGradientData.SetElementAt( ResolutionXY - 1, ResolutionXY - 1, ResultGradient.GetSafeNormal() );
		SurfaceSteepnessData.SetElementAt( ResolutionXY - 1, ResolutionXY - 1, FMath::Min( ResultGradient.Size() / MaxSurfaceSteepness, 1.0f ) );
	}
}

void AOWGChunk::PartialUpdateSurfaceNormal( int32 StartX, int32 StartY, int32 EndX, int32 EndY )
{
	const FChunkData2D& SurfaceHeightmapData = ChunkData2D.FindChecked( ChunkDataID::SurfaceHeightmap );
	const int32 ResolutionXY = SurfaceHeightmapData.GetSurfaceResolutionXY();

	// Create data entries if we do not have them already
	if ( !ChunkData2D.Contains( ChunkDataID::SurfaceNormal ) )
	{
		ChunkData2D.Emplace( ChunkDataID::SurfaceNormal, FChunkData2D::Create<FVector3f>( ResolutionXY, true ) );
	}
	FChunkData2D& SurfaceNormalData = ChunkData2D.FindChecked( ChunkDataID::SurfaceNormal );

	// Calculate normals for all points. CalculatePointNormal automatically handles edges.
	for ( int32 PointX = FMath::Max( StartX, 0 ); PointX < FMath::Min( EndX + 1, ResolutionXY ); PointX++ )
	{
		for ( int32 PointY = FMath::Max( StartY, 0 ); PointY < FMath::Min( EndY + 1, ResolutionXY ); PointY++ )
		{
			SurfaceNormalData.SetElementAt( PointX, PointY, SurfaceHeightmapData.CalculatePointNormal<float>( PointX, PointY ) );
		}
	}
}

void AOWGChunk::RecalculateCurrentStageGenerators()
{
	CurrentStageChunkGenerators.Generators.Empty();
	CurrentStageChunkGenerators.GeneratorInstigatorBiomes.Empty();
	
	if ( const FChunkGeneratorArray* ChunkGeneratorArray = WorldGeneratorDefinition->ChunkGenerators.Find( CurrentGenerationStage ) )
	{
		// No need to go over each element here because these generators are not mapped to a biome
		CurrentStageChunkGenerators.Generators.Append( ChunkGeneratorArray->Generators );
	}

	for ( UOWGBiome* Biome : BiomePalette.GetAllBiomes() )
	{
		if ( const FChunkGeneratorArray* ChunkGeneratorArray = Biome->ChunkGenerators.Find( CurrentGenerationStage ) )
		{
			for ( const TSubclassOf<UOWGChunkGenerator>& ChunkGenerator : ChunkGeneratorArray->Generators )
			{
				CurrentStageChunkGenerators.Generators.Add( ChunkGenerator );
				CurrentStageChunkGenerators.GeneratorInstigatorBiomes.FindOrAdd( ChunkGenerator ).Add( Biome );
			}
		}
	}

	// Remove duplicates from the resulting collection
	CurrentStageChunkGenerators.Generators.SetNum( Algo::Unique( CurrentStageChunkGenerators.Generators ) );
}

void AOWGChunk::InitializeChunkBiomePalette( FChunkBiomePalette&& InBiomePalette, FChunkData2D&& InBiomeMap )
{
	checkf( !ChunkData2D.Contains( ChunkDataID::BiomeMap ), TEXT("InitializeChunkBiomePalette called on already initialized chunk") );

	BiomePalette = InBiomePalette;
	ChunkData2D.Emplace( ChunkDataID::BiomeMap, MoveTemp( InBiomeMap ) );
}

void AOWGChunk::InitializeChunkLandscape( FChunkLandscapeWeightMapDescriptor&& InWeightMapDescriptor, FChunkData2D&& InHeightMap, FChunkData2D&& InWeightMap )
{
	checkf( !ChunkData2D.Contains( ChunkDataID::SurfaceHeightmap ), TEXT("InitializeChunkLandscape called on already initialized chunk") );

	WeightMapDescriptor = InWeightMapDescriptor;
	ChunkData2D.Emplace( ChunkDataID::SurfaceHeightmap, MoveTemp( InHeightMap ) );
	ChunkData2D.Emplace( ChunkDataID::SurfaceWeights, MoveTemp( InWeightMap ) );

	const FVector2f ChunkExtents( FChunkCoord::ChunkSizeWorldUnits / 2.0f );
	PartialRecalculateSurfaceData( FBox2f( -ChunkExtents, ChunkExtents ) );
}

TSharedRef<FCachedChunkLandscapeData> AOWGChunk::GetChunkLandscapeSourceData()
{
	check( IsChunkInitialized() );
	if ( !CachedLandscapeData.IsValid() || CachedLandscapeData->ChangelistNumber != GrassSourceDataChangelistNumber )
	{
		CachedLandscapeData = MakeShared<FCachedChunkLandscapeData>();
		CachedLandscapeData->ChunkToWorld = GetActorTransform();
		CachedLandscapeData->HeightMapData = ChunkData2D.FindChecked( ChunkDataID::SurfaceHeightmap );
		CachedLandscapeData->NormalMapData = ChunkData2D.FindChecked( ChunkDataID::SurfaceNormal );
		CachedLandscapeData->SteepnessData = ChunkData2D.FindChecked( ChunkDataID::SurfaceSteepness );
		CachedLandscapeData->WeightMapData = ChunkData2D.FindChecked( ChunkDataID::SurfaceWeights );
		CachedLandscapeData->WeightMapDescriptor = *GetWeightMapDescriptor();
		CachedLandscapeData->ChangelistNumber = GrassSourceDataChangelistNumber;
	}
	return CachedLandscapeData.ToSharedRef();
}

TSharedRef<FCachedChunkBiomeData> AOWGChunk::GetChunkBiomeData()
{
	check( IsChunkInitialized() );

	if ( !CachedBiomeData.IsValid() )
	{
		CachedBiomeData = MakeShared<FCachedChunkBiomeData>();
		CachedBiomeData->ChunkToWorld = GetActorTransform();
		CachedBiomeData->BiomePalette = *GetBiomePalette();
		CachedBiomeData->BiomeMap = ChunkData2D.FindChecked( ChunkDataID::BiomeMap );
	}
	return CachedBiomeData.ToSharedRef();
}
