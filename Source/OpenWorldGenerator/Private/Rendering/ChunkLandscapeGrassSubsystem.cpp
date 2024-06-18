// Copyright Nikita Zolotukhin. All Rights Reserved.

#include "Rendering/ChunkLandscapeGrassSubsystem.h"
#include "OpenWorldGeneratorSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/InstancedStaticMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Math/Halton.h"
#include "Partition/ChunkCoord.h"
#include "Partition/OWGChunk.h"
#include "Partition/OWGChunkManagerInterface.h"
#include "Rendering/OWGChunkLandscapeLayer.h"

DECLARE_CYCLE_STAT( TEXT("Chunk Landscape Grass Update"), STAT_ChunkLandscapeGrassUpdate, STATGROUP_Game );
DECLARE_CYCLE_STAT( TEXT("Async Chunk Landscape Grass Build"), STAT_AsyncChunkLandscapeGrassAsyncBuildTime, STATGROUP_Game );
DECLARE_CYCLE_STAT( TEXT("Chunk Landscape Grass Accept Prebuilt Tree"), STAT_ChunkLandscapeGrassAcceptPrebuiltTree, STATGROUP_Game );

static TAutoConsoleVariable CVarChunkGrassEnable(
	TEXT("owg.grass.EnableGrass"),
	true,
	TEXT("Whenever landscape grass is enabled for chunks."),
	ECVF_Scalability );

static TAutoConsoleVariable CVarChunkGrassBuildDistance(
	TEXT("owg.grass.BuildDistance"),
	12800.0f,
	TEXT("Build distance of the grass. Chunks within that distance from the player will have their grass data built."),
	ECVF_Scalability );

static TAutoConsoleVariable CVarChunkGrassCullDistanceScale(
	TEXT("owg.grass.CullDistanceScale"),
	1.0f,
	TEXT("Multiplier on all grass cull distances."),
	ECVF_Scalability );

static TAutoConsoleVariable CVarChunkGrassDensityScale(
	TEXT("owg.grass.DensityScale"),
	1.0f,
	TEXT("Multiplier on the scale of the grass."),
	ECVF_Scalability );

static TAutoConsoleVariable CVarChunkGrassMaxAsyncBuildTasks(
	TEXT("owg.grass.MaxAsyncBuildTasks"),
	32,
	TEXT("Maximum amount of async grass build tasks active at the same time. Default: 32"),
	ECVF_Scalability );

static TAutoConsoleVariable CVarChunkGrassMaxTasksPerFrame(
	TEXT("owg.grass.MaxTasksPerFrame"),
	4,
	TEXT("Maximum amount of async build tasks that can be scheduled per frame. Default: 4"),
	ECVF_Scalability );

static TAutoConsoleVariable CVarChunkGrassUpdateFrequency(
	TEXT("owg.grass.UpdateFrequency"),
	0.25f,
	TEXT("How frequently should the grass visibility be recalculated"),
	ECVF_Scalability );

static TAutoConsoleVariable CVarChunkGrassDestroyTimeout(
	TEXT("owg.grass.DestroyTimeout"),
	8.0f,
	TEXT("How frequently will the inactive grass components be purged from the world when the player is no longer close to them"),
	ECVF_Scalability );

void FChunkGrassMeshComponentData::AddReferencedObjects( FReferenceCollector& ReferenceCollector )
{
	ReferenceCollector.AddStableReference( &OwnerLandscapeLayer );
	ReferenceCollector.AddPropertyReferencesWithStructARO( FOWGLandscapeGrassVariety::StaticStruct(), &GrassVariety );
	ReferenceCollector.AddStableReference( &StaticMeshComponent );
}

FChunkLandscapeGrassData::FChunkLandscapeGrassData() : ChunkUnloadedCounter( MakeShared<FThreadSafeCounter>() )
{
}

void FChunkLandscapeGrassData::AddReferencedObjects( FReferenceCollector& ReferenceCollector )
{
	for ( TPair<TObjectPtr<UOWGChunkLandscapeLayer>, TArray<FChunkGrassMeshComponentData>>& Pair : GrassStaticMeshComponents )
	{
		ReferenceCollector.AddStableReference( &Pair.Key );
		for ( FChunkGrassMeshComponentData& ComponentData : Pair.Value )
		{
			ComponentData.AddReferencedObjects( ReferenceCollector );
		}
	}
}

UChunkLandscapeGrassSubsystem::UChunkLandscapeGrassSubsystem()
{
}

UChunkLandscapeGrassSubsystem::~UChunkLandscapeGrassSubsystem()
{
	checkf( AsyncFoliageTasks.IsEmpty(), TEXT("UChunkLandscapeGrassSubsystem destroyed without being Deinitialized first!") );
}

void UChunkLandscapeGrassSubsystem::Deinitialize()
{
	Super::Deinitialize();
	PullResultsFromCompletedTasks( true );
}

void UChunkLandscapeGrassSubsystem::Tick( float DeltaTime )
{
	Super::Tick( DeltaTime );

	TimeBeforeGrassUpdate -= DeltaTime;
	if ( TimeBeforeGrassUpdate <= 0.0f )
	{
		if ( CVarChunkGrassEnable.GetValueOnGameThread() )
		{
			UpdateChunkGrass( GetWorld()->ViewLocationsRenderedLastFrame );	
		}
		TimeBeforeGrassUpdate = CVarChunkGrassUpdateFrequency.GetValueOnGameThread();
	}
	PullResultsFromCompletedTasks();
	CleanupStaleChunkGrass();
}

TStatId UChunkLandscapeGrassSubsystem::GetStatId() const
{
	return GET_STATID( STAT_ChunkLandscapeGrassUpdate );
}

bool UChunkLandscapeGrassSubsystem::ShouldCreateSubsystem( UObject* Outer ) const
{
	const UWorld* World = CastChecked<UWorld>( Outer );
	const UGameInstance* GameInstance = World->GetGameInstance();

	// Do not create subsystem on dedicated servers or PIE instances running as dedicated server
	return Super::ShouldCreateSubsystem( Outer ) && ( GameInstance == nullptr || !GameInstance->IsDedicatedServerInstance() ) &&
		!IsRunningDedicatedServer();
}

bool UChunkLandscapeGrassSubsystem::DoesSupportWorldType( const EWorldType::Type WorldType ) const
{
	return WorldType == EWorldType::PIE || WorldType == EWorldType::Game;
}

void UChunkLandscapeGrassSubsystem::AddReferencedObjects( UObject* InThis, FReferenceCollector& Collector )
{
	Super::AddReferencedObjects( InThis, Collector );

	UChunkLandscapeGrassSubsystem* Subsystem = CastChecked<UChunkLandscapeGrassSubsystem>( InThis );
	for ( TPair<FChunkCoord, FChunkLandscapeGrassData>& Pair : Subsystem->PerChunkComponents )
	{
		Pair.Value.AddReferencedObjects( Collector );
	}
}

void UChunkLandscapeGrassSubsystem::UpdateChunkGrass( const TArray<FVector>& InCameraLocations )
{
	const UOpenWorldGeneratorSubsystem* OpenWorldGeneratorSubsystem = UOpenWorldGeneratorSubsystem::Get( GetWorld() );
	if ( !OpenWorldGeneratorSubsystem ) return;

	const TScriptInterface<IOWGChunkManagerInterface> ChunkManager = OpenWorldGeneratorSubsystem->GetChunkManager();

	const float LandscapeGrassRenderDistance = CVarChunkGrassBuildDistance.GetValueOnGameThread() * CVarChunkGrassCullDistanceScale.GetValueOnGameThread();
	const float LandscapeGrassDensityScale = CVarChunkGrassDensityScale.GetValueOnGameThread();
	const int32 MaxAsyncChunkGrassBuildTasks = CVarChunkGrassMaxAsyncBuildTasks.GetValueOnGameThread();
	const int32 MaxAsyncChunkGrassBuildTasksDispatchPerTick = CVarChunkGrassMaxTasksPerFrame.GetValueOnGameThread();

	TArray<AOWGChunk*> RelevantChunks;
	for ( const FVector& CameraLocation : InCameraLocations )
	{
		const FChunkCoord MinChunkCoord = FChunkCoord::FromWorldLocation( CameraLocation - FVector( LandscapeGrassRenderDistance ) );
		const FChunkCoord MaxChunkCoord = FChunkCoord::FromWorldLocation( CameraLocation + FVector( LandscapeGrassRenderDistance ) );

		for ( int32 ChunkX = MinChunkCoord.PosX; ChunkX <= MaxChunkCoord.PosX; ChunkX++ )
		{
			for ( int32 ChunkY = MinChunkCoord.PosY; ChunkY <= MaxChunkCoord.PosY; ChunkY++ )
			{
				AOWGChunk* Chunk = ChunkManager->FindChunk( FChunkCoord( ChunkX, ChunkY ) );

				// Take loaded, initialized chunks with a valid LOD
				if ( Chunk != nullptr && Chunk->IsChunkInitialized() && !Chunk->IsChunkIdle() && !Chunk->IsPendingToBeUnloaded() && Chunk->GetCurrentChunkLOD() != INDEX_NONE )
				{
					RelevantChunks.Add( Chunk );
				}
			}
		}
	}

	// Allocate components for each task, and record data for them
	const float WorldTimeSeconds = GetWorld()->TimeSeconds;
	TArray<FChunkGrassMeshComponentData*> PendingChunkGrassMeshComponentData;
	
	for ( AOWGChunk* Chunk : RelevantChunks )
	{
		FChunkLandscapeGrassData& ChunkLandscapeGrassData = PerChunkComponents.FindOrAdd( Chunk->GetChunkCoord() );
		const TSharedRef<FCachedChunkLandscapeData> GrassSourceData = Chunk->GetChunkLandscapeSourceData();
		ChunkLandscapeGrassData.LastTimeUsed = WorldTimeSeconds;
		const FChunkLandscapeWeightMapDescriptor* WeightMapDescriptor = Chunk->GetWeightMapDescriptor();

		int32 HamiltonIndex = 1;
		for ( int32 LayerIndex = 0; LayerIndex < WeightMapDescriptor->GetNumLayers(); LayerIndex++ )
		{
			UOWGChunkLandscapeLayer* LandscapeLayer = WeightMapDescriptor->GetLayerDescriptor( LayerIndex );
			if ( !LandscapeLayer->LandscapeGrass ) continue;
			const TArray<FOWGLandscapeGrassVariety>& GrassVarieties = LandscapeLayer->LandscapeGrass->GrassVarieties;
			const bool bEnableDensityScaling = LandscapeLayer->LandscapeGrass->bEnableDensityScaling;
	
			TArray<FChunkGrassMeshComponentData>& LandscapeLayerComponents = ChunkLandscapeGrassData.GrassStaticMeshComponents.FindOrAdd( LandscapeLayer );
			
			LandscapeLayerComponents.SetNumZeroed( GrassVarieties.Num() );
			for ( int32 GrassVarietyIndex = 0; GrassVarietyIndex < GrassVarieties.Num(); GrassVarietyIndex++ )
			{
				FChunkGrassMeshComponentData& GrassInstanceComponent = LandscapeLayerComponents[ GrassVarietyIndex ];
				if ( !IsValid( GrassInstanceComponent.StaticMeshComponent ) )
				{
					GrassInstanceComponent.OwnerChunkCoord = Chunk->GetChunkCoord();
					GrassInstanceComponent.OwnerLandscapeLayer = LandscapeLayer;
					GrassInstanceComponent.GrassVarietyIndex = GrassVarietyIndex;
					GrassInstanceComponent.GrassVariety = GrassVarieties[ GrassVarietyIndex ];
					GrassInstanceComponent.StaticMeshComponent = CreateStaticMeshComponentForGrassVariety( Chunk, GrassVarieties[ GrassVarietyIndex ] );
					GrassInstanceComponent.LastScheduledRebuildChangelist = INDEX_NONE;
					GrassInstanceComponent.LastScheduledRebuildWorldSeconds = 0.0f;
					GrassInstanceComponent.ChunkUnloadedCounter = ChunkLandscapeGrassData.ChunkUnloadedCounter;
				}
				GrassInstanceComponent.PendingRebuildSourceData = GrassSourceData;
				GrassInstanceComponent.ChunkWeightIndex = LayerIndex;

				// Make sure we have enough points for Hamilton non-grid randomization, since it's shared by all grass types in chunk. If we do not share it, the points would overlap
				GrassInstanceComponent.DensityScale = bEnableDensityScaling ? LandscapeGrassDensityScale : 1.0f;
				if ( !GrassVarieties[ GrassVarietyIndex ].bUseGrid )
				{
					const FVector ChunkExtents( FChunkCoord::ChunkSizeWorldUnits );
					const int32 MaxGrassInstancesSqrt = FChunkLandscapeGrassBuildTask::CalculateMaxInstancesSqrt( GrassVarieties[ GrassVarietyIndex ], ChunkExtents, GrassInstanceComponent.DensityScale );
					
					HamiltonIndex += FMath::Square( MaxGrassInstancesSqrt );
					GrassInstanceComponent.BaseHamiltonIndex = HamiltonIndex;
				}

				// Rebuild grass for the chunk if it is outdated
				if ( GrassInstanceComponent.LastScheduledRebuildChangelist != GrassSourceData->ChangelistNumber )
				{
					PendingChunkGrassMeshComponentData.Add( &GrassInstanceComponent );
				}
			}
		}
	}

	// Sort pending rebuilds. Prioritize chunk data that has not yet been built with the current changelist, and then data that was updated the last
	PendingChunkGrassMeshComponentData.StableSort([]( const FChunkGrassMeshComponentData& A, const FChunkGrassMeshComponentData& B )
	{
		const bool bComponentUpToDateA = A.LastScheduledRebuildChangelist == A.PendingRebuildSourceData->ChangelistNumber;
		const bool bComponentUpToDateB = B.LastScheduledRebuildChangelist == B.PendingRebuildSourceData->ChangelistNumber;

		// If one of the components is not up to date, we prefer the component that is not up to date
		if ( bComponentUpToDateA != bComponentUpToDateB )
		{
			// If B is up to date, then A is not, and as such we should return true from sort function and put A ahead
			return bComponentUpToDateB;
		}
		// Prefer component that has the lowest changelist number, e.g. is the least up to date with the changes
		if ( A.LastScheduledRebuildChangelist != B.LastScheduledRebuildChangelist )
		{
			return A.LastScheduledRebuildChangelist < B.LastScheduledRebuildChangelist;
		}
		// Otherwise, pick the component that has been updated the longest time ago. At this point, the components are pretty much equally not up to date
		return A.LastScheduledRebuildWorldSeconds < B.LastScheduledRebuildWorldSeconds;
	} );

	// Schedule tasks as long as we are not overrun with the tasks that are presently running
	const int32 NumTasksToDispatch = FMath::Min( FMath::Min( PendingChunkGrassMeshComponentData.Num(),  MaxAsyncChunkGrassBuildTasks - AsyncFoliageTasks.Num() ), MaxAsyncChunkGrassBuildTasksDispatchPerTick );

	for ( int32 TaskIndex = 0; TaskIndex < NumTasksToDispatch; TaskIndex++ )
	{
		FChunkGrassMeshComponentData* ChunkGrassMeshComponentData = PendingChunkGrassMeshComponentData[ TaskIndex ];

		ChunkGrassMeshComponentData->LastScheduledRebuildChangelist = ChunkGrassMeshComponentData->PendingRebuildSourceData->ChangelistNumber;
		ChunkGrassMeshComponentData->LastScheduledRebuildWorldSeconds = WorldTimeSeconds;

		FAsyncTask<FChunkLandscapeGrassBuildTask>* GrassBuildTask = new FAsyncTask<FChunkLandscapeGrassBuildTask>( ChunkGrassMeshComponentData );
		AsyncFoliageTasks.Add( GrassBuildTask );
		GrassBuildTask->StartBackgroundTask();
	}
}

void UChunkLandscapeGrassSubsystem::PullResultsFromCompletedTasks( bool bBlocking )
{
	for ( int32 TaskIndex = AsyncFoliageTasks.Num() - 1; TaskIndex >= 0; TaskIndex-- )
	{
		FAsyncTask<FChunkLandscapeGrassBuildTask>* ChunkBuildGrassTank = AsyncFoliageTasks[ TaskIndex ];
		if ( bBlocking )
		{
			ChunkBuildGrassTank->EnsureCompletion();
		}
		if ( !ChunkBuildGrassTank->IsDone() )
		{
			continue;
		}
		AsyncFoliageTasks.RemoveAtSwap( TaskIndex );

		if ( FChunkLandscapeGrassData* GrassData = PerChunkComponents.Find( ChunkBuildGrassTank->GetTask().ChunkCoord ) )
		{
			const UOWGChunkLandscapeLayer* OwnerLandscapeLayer = ChunkBuildGrassTank->GetTask().OwnerLandscapeLayer.Get();
			if ( OwnerLandscapeLayer && GrassData->GrassStaticMeshComponents.Contains( OwnerLandscapeLayer ) )
			{
				TArray<FChunkGrassMeshComponentData>& ComponentDataArray = GrassData->GrassStaticMeshComponents.FindChecked( OwnerLandscapeLayer );
				const int32 GrassVarietyIndex = ChunkBuildGrassTank->GetTask().GrassVarietyIndex;
				
				if ( ComponentDataArray.IsValidIndex( GrassVarietyIndex ) )
				{
					ChunkBuildGrassTank->GetTask().CompleteOnGameThread( &ComponentDataArray[ GrassVarietyIndex ] );
				}
			}
		}
		delete ChunkBuildGrassTank;
	}
}

void UChunkLandscapeGrassSubsystem::CleanupStaleChunkGrass()
{
	const UOpenWorldGeneratorSubsystem* OpenWorldGeneratorSubsystem = UOpenWorldGeneratorSubsystem::Get( GetWorld() );
	if ( !OpenWorldGeneratorSubsystem ) return;

	const TScriptInterface<IOWGChunkManagerInterface> ChunkManager = OpenWorldGeneratorSubsystem->GetChunkManager();

	const float DestroyTimeoutSeconds = CVarChunkGrassDestroyTimeout.GetValueOnGameThread();
	const float WorldTimeSeconds = GetWorld()->TimeSeconds;
	const bool bGrassDisabled = !CVarChunkGrassEnable.GetValueOnGameThread();

	TArray<FChunkCoord> ActiveGrassChunkCoords;
	PerChunkComponents.GenerateKeyArray( ActiveGrassChunkCoords );

	for ( const FChunkCoord& ChunkCoord : ActiveGrassChunkCoords )
	{
		const AOWGChunk* LoadedChunk = ChunkManager->FindChunk( ChunkCoord );
		
		if ( LoadedChunk == nullptr || !LoadedChunk->IsChunkInitialized() || LoadedChunk->IsPendingToBeUnloaded() || bGrassDisabled ||
			PerChunkComponents.FindChecked( ChunkCoord ).LastTimeUsed + DestroyTimeoutSeconds <= WorldTimeSeconds )
		{
			FChunkLandscapeGrassData ChunkLandscapeGrassData;
			PerChunkComponents.RemoveAndCopyValue( ChunkCoord, ChunkLandscapeGrassData );

			for ( TPair<TObjectPtr<UOWGChunkLandscapeLayer>, TArray<FChunkGrassMeshComponentData>>& Pair : ChunkLandscapeGrassData.GrassStaticMeshComponents )
			{
				for ( FChunkGrassMeshComponentData& MeshComponentData : Pair.Value )
				{
					MeshComponentData.StaticMeshComponent->DestroyComponent();
					MeshComponentData.StaticMeshComponent = nullptr;
				}
			}
			ChunkLandscapeGrassData.ChunkUnloadedCounter->Increment();
		}
	}
}

// Mostly copied from LandscapeGrass.cpp
UGrassInstancedStaticMeshComponent* UChunkLandscapeGrassSubsystem::CreateStaticMeshComponentForGrassVariety( AOWGChunk* Chunk, const FOWGLandscapeGrassVariety& GrassVariety )
{
	UGrassInstancedStaticMeshComponent* GrassInstancedStaticMeshComponent = NewObject<UGrassInstancedStaticMeshComponent>( Chunk, NAME_None, RF_Transient );

	GrassInstancedStaticMeshComponent->Mobility = EComponentMobility::Static;
	GrassInstancedStaticMeshComponent->SetStaticMesh(GrassVariety.GrassMesh);
	GrassInstancedStaticMeshComponent->MinLOD = GrassVariety.MinLOD;
	GrassInstancedStaticMeshComponent->bSelectable = false;
	GrassInstancedStaticMeshComponent->bHasPerInstanceHitProxies = false;
	GrassInstancedStaticMeshComponent->bReceivesDecals = true;

	static FName NoCollision(TEXT("NoCollision"));
	GrassInstancedStaticMeshComponent->SetCollisionProfileName(NoCollision);
	GrassInstancedStaticMeshComponent->bDisableCollision = true;
	GrassInstancedStaticMeshComponent->SetCanEverAffectNavigation(false);

	GrassInstancedStaticMeshComponent->bCastStaticShadow = false;
	GrassInstancedStaticMeshComponent->CastShadow = true;
	GrassInstancedStaticMeshComponent->bCastContactShadow = true;
	GrassInstancedStaticMeshComponent->bCastDynamicShadow = false;
	GrassInstancedStaticMeshComponent->bAffectDistanceFieldLighting = false;
	GrassInstancedStaticMeshComponent->OverrideMaterials = GrassVariety.OverrideMaterials;
	GrassInstancedStaticMeshComponent->bEvaluateWorldPositionOffset = true;
	GrassInstancedStaticMeshComponent->WorldPositionOffsetDisableDistance = GrassVariety.InstanceWorldPositionOffsetDisableDistance;

	// Derive random from the chunk coordinate's type hash, this should be deterministic and consistent, even if the chunk object is reloaded
	GrassInstancedStaticMeshComponent->InstancingRandomSeed = GetTypeHash( Chunk->GetChunkCoord() ) + 1;

	GrassInstancedStaticMeshComponent->PrecachePSOs();

	// Apply GPU culling from the console variable
	const float CullDistanceScale = CVarChunkGrassCullDistanceScale.GetValueOnGameThread();
	GrassInstancedStaticMeshComponent->InstanceStartCullDistance = GrassVariety.StartCullDistance * CullDistanceScale;
	GrassInstancedStaticMeshComponent->InstanceEndCullDistance = GrassVariety.EndCullDistance * CullDistanceScale;

	// Attach to the chunk's root component
	GrassInstancedStaticMeshComponent->RegisterComponent();
	GrassInstancedStaticMeshComponent->AttachToComponent( Chunk->SceneRootComponent, FAttachmentTransformRules::KeepRelativeTransform );
	
	return GrassInstancedStaticMeshComponent;
}

FChunkLandscapeGrassBuildTask::FChunkLandscapeGrassBuildTask( const FChunkGrassMeshComponentData* PendingRebuildData ) : InstanceBuffer( true )
{
	ChunkCoord = PendingRebuildData->OwnerChunkCoord;
	OwnerLandscapeLayer = PendingRebuildData->OwnerLandscapeLayer;
	GrassVarietyIndex = PendingRebuildData->GrassVarietyIndex;
	RebuildInitiatorComponent = PendingRebuildData->StaticMeshComponent;

	ChunkWeightIndex = PendingRebuildData->ChunkWeightIndex;
	GrassVariety = PendingRebuildData->GrassVariety;
	ChunkGrassSourceData = PendingRebuildData->PendingRebuildSourceData;
	HaltonBaseIndex = PendingRebuildData->BaseHamiltonIndex;
	LocalToComponentRelative = ChunkGrassSourceData->ChunkToWorld.ToMatrixNoScale() * PendingRebuildData->StaticMeshComponent->GetComponentTransform().ToMatrixWithScale().Inverse();
	DesiredInstancesPerLeaf = PendingRebuildData->StaticMeshComponent->DesiredInstancesPerLeaf();
	ChunkUnloadedCounter = PendingRebuildData->ChunkUnloadedCounter.ToSharedRef();
	RandomStream = FRandomStream{ PendingRebuildData->StaticMeshComponent->InstancingRandomSeed };

	// Cache data from the grass variety to avoid inconsistent results if CVars change from the main thread, since accessing them is not thread safe
	MeshBox = GrassVariety.GrassMesh->GetBounds().GetBox();

	LocalOrigin = FVector( -FChunkCoord::ChunkSizeWorldUnits / 2.0f );
	LocalExtents = FVector( FChunkCoord::ChunkSizeWorldUnits );
	SqrtMaxInstances = CalculateMaxInstancesSqrt( GrassVariety, LocalExtents, PendingRebuildData->DensityScale );

	InstanceBuffer.SetAllowCPUAccess( false );
}

int32 FChunkLandscapeGrassBuildTask::CalculateMaxInstancesSqrt( const FOWGLandscapeGrassVariety& GrassVariety, const FVector& Extents, float DensityScale )
{
	const float GrassDensity = GrassVariety.GrassDensity * DensityScale;
	return FMath::CeilToInt( FMath::Sqrt( FMath::Abs( Extents.X * Extents.Y * GrassDensity / 1000.0f / 1000.0f) ) );
}

void FChunkLandscapeGrassBuildTask::DoWork()
{
	double StartTime = FPlatformTime::Seconds();
	const bool bUsingRandomScale = IsUsingRandomScale();
	const FVector DefaultScale = GetDefaultScale();

	float Div = 1.0f / float(SqrtMaxInstances);

	TArray<FMatrix> InstanceTransforms;

	if ( HaltonBaseIndex && !GrassVariety.bUseGrid )
	{
		int32 MaxNum = SqrtMaxInstances * SqrtMaxInstances;
		InstanceTransforms.Reserve(MaxNum);

		for ( int32 InstanceIndex = 0; InstanceIndex < MaxNum; InstanceIndex++ )
		{
			float HaltonX = Halton( InstanceIndex + HaltonBaseIndex, 2 );
			float HaltonY = Halton( InstanceIndex + HaltonBaseIndex, 3 );
			FVector Location( LocalOrigin.X + HaltonX * LocalExtents.X, LocalOrigin.Y + HaltonY * LocalExtents.Y, 0.0f );
			FVector LocationWithHeight;
			FVector ComputedNormal;
			float Weight = 0.f;
			SampleLandscapeAtLocationLocal(Location, LocationWithHeight, Weight, GrassVariety.AlignToSurface ? &ComputedNormal : nullptr);

			bool bKeep = Weight > 0.0f && Weight >= RandomStream.GetFraction() && !IsExcluded(LocationWithHeight);
			if (bKeep)
			{
				const FVector Scale = bUsingRandomScale ? GetRandomScale() : DefaultScale;
				const float Rot = GrassVariety.RandomRotation ? RandomStream.GetFraction() * 360.0f : 0.0f;
				const FMatrix BaseXForm = FScaleRotationTranslationMatrix( Scale, FRotator(0.0f, Rot, 0.0f), FVector::ZeroVector );
				FMatrix OutXForm;

				if ( GrassVariety.AlignToSurface && !ComputedNormal.IsNearlyZero() )
				{
					const FVector NewZ = ComputedNormal * FMath::Sign(ComputedNormal.Z);
					const FVector NewX = (FVector(0, -1, 0) ^ NewZ).GetSafeNormal();
					const FVector NewY = NewZ ^ NewX;
					const FMatrix Align = FMatrix(NewX, NewY, NewZ, FVector::ZeroVector);
					OutXForm = (BaseXForm * Align).ConcatTranslation(LocationWithHeight) * LocalToComponentRelative;
				}
				else
				{
					OutXForm = BaseXForm.ConcatTranslation(LocationWithHeight) * LocalToComponentRelative;
				}
				InstanceTransforms.Add(OutXForm);
			}
		}
		if (InstanceTransforms.Num())
		{
			TotalInstances += InstanceTransforms.Num();
			InstanceBuffer.AllocateInstances(InstanceTransforms.Num(), 0, EResizeBufferFlags::AllowSlackOnGrow | EResizeBufferFlags::AllowSlackOnReduce, true);
			for (int32 InstanceIndex = 0; InstanceIndex < InstanceTransforms.Num(); InstanceIndex++)
			{
				const FMatrix& OutXForm = InstanceTransforms[InstanceIndex];
				InstanceBuffer.SetInstance( InstanceIndex, FMatrix44f( OutXForm ), RandomStream.GetFraction() );
			}
		}
	}
	else
	{
		int32 NumKept = 0;
		float MaxJitter1D = FMath::Clamp<float>( GrassVariety.PlacementJitter, 0.0f, 0.99f ) * Div * 0.5f;
		FVector MaxJitter(MaxJitter1D, MaxJitter1D, 0.0f);
		MaxJitter *= LocalExtents;
		LocalOrigin += LocalExtents * (Div * 0.5f);
		struct FInstanceLocal
		{
			FVector Pos;
			bool bKeep;
		};
		TArray<FInstanceLocal> Instances;
		Instances.AddUninitialized(SqrtMaxInstances * SqrtMaxInstances);
		{
			int32 InstanceIndex = 0;
			for (int32 xStart = 0; xStart < SqrtMaxInstances; xStart++)
			{
				for (int32 yStart = 0; yStart < SqrtMaxInstances; yStart++)
				{
					FVector Location( LocalOrigin.X + float(xStart) * Div * LocalExtents.X, LocalOrigin.Y + float(yStart) * Div * LocalExtents.Y, 0.0f );

					const float FirstRandom = RandomStream.GetFraction();
					const float SecondRandom = RandomStream.GetFraction();
					Location += FVector(FirstRandom * 2.0f - 1.0f, SecondRandom * 2.0f - 1.0f, 0.0f) * MaxJitter;

					FInstanceLocal& Instance = Instances[InstanceIndex];
					float Weight = 0.0f;

					SampleLandscapeAtLocationLocal(Location, Instance.Pos, Weight);
					Instance.bKeep = Weight > 0.0f && Weight >= RandomStream.GetFraction() && !IsExcluded(Instance.Pos);
					if (Instance.bKeep)
					{
						NumKept++;
					}
					InstanceIndex++;
				}
			}
		}
		if (NumKept)
		{
			InstanceTransforms.AddUninitialized(NumKept);
			TotalInstances += NumKept;
			{
				InstanceBuffer.AllocateInstances(NumKept, 0, EResizeBufferFlags::AllowSlackOnGrow | EResizeBufferFlags::AllowSlackOnReduce, true);
				int32 InstanceIndex = 0;
				int32 OutInstanceIndex = 0;
				for (int32 xStart = 0; xStart < SqrtMaxInstances; xStart++)
				{
					for (int32 yStart = 0; yStart < SqrtMaxInstances; yStart++)
					{
						const FInstanceLocal& Instance = Instances[InstanceIndex];
						if (Instance.bKeep)
						{
							const FVector Scale = bUsingRandomScale ? GetRandomScale() : DefaultScale;
							const float Rot = GrassVariety.RandomRotation ? RandomStream.GetFraction() * 360.0f : 0.0f;
							const FMatrix BaseXForm = FScaleRotationTranslationMatrix(Scale, FRotator(0.0f, Rot, 0.0f), FVector::ZeroVector);
							FMatrix OutXForm;
							if ( GrassVariety.AlignToSurface )
							{
								FVector PosX1 = xStart ? Instances[InstanceIndex - SqrtMaxInstances].Pos : Instance.Pos;
								FVector PosX2 = (xStart + 1 < SqrtMaxInstances) ? Instances[InstanceIndex + SqrtMaxInstances].Pos : Instance.Pos;
								FVector PosY1 = yStart ? Instances[InstanceIndex - 1].Pos : Instance.Pos;
								FVector PosY2 = (yStart + 1 < SqrtMaxInstances) ? Instances[InstanceIndex + 1].Pos : Instance.Pos;

								if (PosX1 != PosX2 && PosY1 != PosY2)
								{
									FVector NewZ = ((PosX1 - PosX2) ^ (PosY1 - PosY2)).GetSafeNormal();
									NewZ *= FMath::Sign(NewZ.Z);

									const FVector NewX = (FVector(0, -1, 0) ^ NewZ).GetSafeNormal();
									const FVector NewY = NewZ ^ NewX;

									FMatrix Align = FMatrix(NewX, NewY, NewZ, FVector::ZeroVector);
									OutXForm = (BaseXForm * Align).ConcatTranslation(Instance.Pos) * LocalToComponentRelative;
								}
								else
								{
									OutXForm = BaseXForm.ConcatTranslation(Instance.Pos) * LocalToComponentRelative;
								}
							}
							else
							{
								OutXForm = BaseXForm.ConcatTranslation(Instance.Pos) * LocalToComponentRelative;
							}
							InstanceTransforms[OutInstanceIndex] = OutXForm;
							InstanceBuffer.SetInstance( OutInstanceIndex++, FMatrix44f( OutXForm ), RandomStream.GetFraction() );
						}
						InstanceIndex++;
					}
				}
			}
		}
	}

	int32 NumInstances = InstanceTransforms.Num();
	if (NumInstances)
	{
		TArray<int32> SortedInstances;
		TArray<int32> InstanceReorderTable;
		TArray<float> InstanceCustomDataDummy;
		UGrassInstancedStaticMeshComponent::BuildTreeAnyThread(InstanceTransforms, InstanceCustomDataDummy, 0, MeshBox, ClusterTree, SortedInstances, InstanceReorderTable, OutOcclusionLayerNum, DesiredInstancesPerLeaf, false);

		// in-place sort the instances and generate the sorted instance data
		for (int32 FirstUnfixedIndex = 0; FirstUnfixedIndex < NumInstances; FirstUnfixedIndex++)
		{
			int32 LoadFrom = SortedInstances[FirstUnfixedIndex];				

			if (LoadFrom != FirstUnfixedIndex)
			{
				check(LoadFrom > FirstUnfixedIndex);
				InstanceBuffer.SwapInstance(FirstUnfixedIndex, LoadFrom);

				int32 SwapGoesTo = InstanceReorderTable[FirstUnfixedIndex];
				check(SwapGoesTo > FirstUnfixedIndex);
				check(SortedInstances[SwapGoesTo] == FirstUnfixedIndex);
				SortedInstances[SwapGoesTo] = LoadFrom;
				InstanceReorderTable[LoadFrom] = SwapGoesTo;

				InstanceReorderTable[FirstUnfixedIndex] = FirstUnfixedIndex;
				SortedInstances[FirstUnfixedIndex] = FirstUnfixedIndex;
			}
		}
	}
	BuildTime = FPlatformTime::Seconds() - StartTime;
}

void FChunkLandscapeGrassBuildTask::CompleteOnGameThread( FChunkGrassMeshComponentData* FinishedRebuildData )
{
	if ( FinishedRebuildData->StaticMeshComponent == RebuildInitiatorComponent && ChunkUnloadedCounter->GetValue() == 0 )
	{
		// Make sure to not attempt to overwrite data with an older version
		if ( FinishedRebuildData->ActiveChangelist <= ChunkGrassSourceData->ChangelistNumber )
		{
			const int32 NumBuiltInstances = InstanceBuffer.GetNumInstances();
			if ( NumBuiltInstances > 0)
			{
				SCOPE_CYCLE_COUNTER( STAT_ChunkLandscapeGrassAcceptPrebuiltTree );
				UGrassInstancedStaticMeshComponent* GrassMeshComponent = FinishedRebuildData->StaticMeshComponent;

				GrassMeshComponent->AcceptPrebuiltTree(ClusterTree, OutOcclusionLayerNum, NumBuiltInstances, &InstanceBuffer);
			}
			FinishedRebuildData->ActiveChangelist = ChunkGrassSourceData->ChangelistNumber;
		}
	}
}

bool FChunkLandscapeGrassBuildTask::ShouldAbort() const
{
	return ChunkUnloadedCounter->GetValue() > 0;
}

TStatId FChunkLandscapeGrassBuildTask::GetStatId() const
{
	return GET_STATID( STAT_AsyncChunkLandscapeGrassAsyncBuildTime );
}

void FChunkLandscapeGrassBuildTask::SampleLandscapeAtLocationLocal( const FVector& InLocation, FVector& OutLocation, float& OutLayerWeight, FVector* OutNormal )
{
	const FVector2f UnitLocation = FChunkData2D::ChunkLocalPositionToNormalized( InLocation );
	
	const float Height = ChunkGrassSourceData->HeightMapData.GetInterpolatedElementAt<float>( UnitLocation );
	OutLocation = FVector( InLocation.X, InLocation.Y, Height );

	const FChunkLandscapeWeight Weight = ChunkGrassSourceData->WeightMapData.GetInterpolatedElementAt<FChunkLandscapeWeight>( UnitLocation );
	OutLayerWeight = Weight.GetNormalizedWeight( ChunkWeightIndex );

	if ( OutNormal )
	{
		const FVector3f Normal = ChunkGrassSourceData->NormalMapData.GetInterpolatedElementAt<FVector3f>( UnitLocation );
		*OutNormal = FVector( Normal );
	}
}

bool FChunkLandscapeGrassBuildTask::IsUsingRandomScale() const
{
	switch ( GrassVariety.Scaling )
	{
		case EOWGLandscapeGrassScaling::Uniform:
			return GrassVariety.ScaleX.Size() > 0;
		case EOWGLandscapeGrassScaling::Free:
			return GrassVariety.ScaleX.Size() > 0 || GrassVariety.ScaleY.Size() > 0 || GrassVariety.ScaleZ.Size() > 0;
		case EOWGLandscapeGrassScaling::LockXY:
			return GrassVariety.ScaleX.Size() > 0 || GrassVariety.ScaleZ.Size() > 0;
		default:
			check(0);
	}
	return false;
}

FVector FChunkLandscapeGrassBuildTask::GetDefaultScale() const
{
	FVector Result( GrassVariety.ScaleX.Min > 0.0f && FMath::IsNearlyZero( GrassVariety.ScaleX.Size() ) ? GrassVariety.ScaleX.Min : 1.0f,
					GrassVariety.ScaleY.Min > 0.0f && FMath::IsNearlyZero( GrassVariety.ScaleY.Size() ) ? GrassVariety.ScaleY.Min : 1.0f,
					GrassVariety.ScaleZ.Min > 0.0f && FMath::IsNearlyZero( GrassVariety.ScaleZ.Size() ) ? GrassVariety.ScaleZ.Min : 1.0f );
	switch ( GrassVariety.Scaling )
	{
		case EOWGLandscapeGrassScaling::Uniform:
			Result.Y = Result.X;
			Result.Z = Result.X;
			break;
		case EOWGLandscapeGrassScaling::Free:
			break;
		case EOWGLandscapeGrassScaling::LockXY:
			Result.Y = Result.X;
			break;
		default:
			check(0);
	}
	return Result;
}

FVector FChunkLandscapeGrassBuildTask::GetRandomScale() const
{
	FVector Result( 1.0f );

	switch ( GrassVariety.Scaling )
	{
		case EOWGLandscapeGrassScaling::Uniform:
			Result.X = GrassVariety.ScaleX.Interpolate(RandomStream.GetFraction());
			Result.Y = Result.X;
			Result.Z = Result.X;
		break;
		case EOWGLandscapeGrassScaling::Free:
			Result.X = GrassVariety.ScaleX.Interpolate(RandomStream.GetFraction());
			Result.Y = GrassVariety.ScaleY.Interpolate(RandomStream.GetFraction());
			Result.Z = GrassVariety.ScaleZ.Interpolate(RandomStream.GetFraction());
		break;
		case EOWGLandscapeGrassScaling::LockXY:
			Result.X = GrassVariety.ScaleX.Interpolate(RandomStream.GetFraction());
			Result.Y = Result.X;
			Result.Z = GrassVariety.ScaleZ.Interpolate(RandomStream.GetFraction());
		break;
		default:
			check(0);
	}
	return Result;
}

bool FChunkLandscapeGrassBuildTask::IsExcluded(const FVector& LocationWithHeight)
{
	for ( const FBox& Box : ExcludedBoxes )
	{
		if ( Box.IsInside(LocationWithHeight) )
		{
			return true;
		}
	}
	return false;
}
