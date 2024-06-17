// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GrassInstancedStaticMeshComponent.h"
#include "OWGChunkLandscapeLayer.h"
#include "Async/AsyncWork.h"
#include "Partition/ChunkCoord.h"
#include "ChunkLandscapeGrassSubsystem.generated.h"

class AOWGChunk;
struct FCachedChunkLandscapeData;
struct FOWGLandscapeGrassVariety;
class FChunkLandscapeGrassBuildTask;
class UOWGChunkLandscapeLayer;

struct FChunkGrassMeshComponentData
{
	FChunkCoord OwnerChunkCoord{};
	UOWGChunkLandscapeLayer* OwnerLandscapeLayer{};
	int32 GrassVarietyIndex{INDEX_NONE};
	FOWGLandscapeGrassVariety GrassVariety{};
	float DensityScale{1.0f};
	int32 BaseHamiltonIndex{1};
	UGrassInstancedStaticMeshComponent* StaticMeshComponent{};
	int32 ActiveChangelist{INDEX_NONE};
	int32 LastScheduledRebuildChangelist{INDEX_NONE};
	float LastScheduledRebuildWorldSeconds{0.0f};
	TSharedPtr<FCachedChunkLandscapeData> PendingRebuildSourceData;
	TSharedPtr<FThreadSafeCounter> ChunkUnloadedCounter;
	int32 ChunkWeightIndex{0};

	void AddReferencedObjects( FReferenceCollector& ReferenceCollector );
};

struct FChunkLandscapeGrassData
{
	TMap<UOWGChunkLandscapeLayer*, TArray<FChunkGrassMeshComponentData>> GrassStaticMeshComponents;
	TSharedRef<FThreadSafeCounter> ChunkUnloadedCounter;
	float LastTimeUsed{};
	
	FChunkLandscapeGrassData();
	void AddReferencedObjects( FReferenceCollector& ReferenceCollector );
};

UCLASS()
class OPENWORLDGENERATOR_API UChunkLandscapeGrassSubsystem : public UTickableWorldSubsystem
{
public:
	GENERATED_BODY()

	UChunkLandscapeGrassSubsystem();
	virtual ~UChunkLandscapeGrassSubsystem() override;

	// Begin UTickableWorldSubsystem interface
	virtual void Tick(float DeltaTime) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual TStatId GetStatId() const override;
	// End UTickableWorldSubsystem interface

	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
private:
	void UpdateChunkGrass( const TArray<FVector>& InCameraLocations );
	void CleanupStaleChunkGrass();
	void PullResultsFromCompletedTasks( bool bBlocking = false );
	static UGrassInstancedStaticMeshComponent* CreateStaticMeshComponentForGrassVariety( AOWGChunk* Chunk, const FOWGLandscapeGrassVariety& GrassVariety );

protected:
	TMap<FChunkCoord, FChunkLandscapeGrassData> PerChunkComponents;
	TArray<FAsyncTask<FChunkLandscapeGrassBuildTask>*> AsyncFoliageTasks;
	float TimeBeforeGrassUpdate{0.0f};
};

class FChunkLandscapeGrassBuildTask : public FNonAbandonableTask
{
public:
	explicit FChunkLandscapeGrassBuildTask( const FChunkGrassMeshComponentData* PendingRebuildData );

	static int32 CalculateMaxInstancesSqrt( const FOWGLandscapeGrassVariety& GrassVariety, const FVector& Extents, float DensityScale );

	void DoWork();
	bool ShouldAbort() const;
	TStatId GetStatId() const;
	void CompleteOnGameThread( FChunkGrassMeshComponentData* FinishedRebuildData );
private:
	void SampleLandscapeAtLocationLocal( const FVector& InLocation, FVector& OutLocation, float& OutLayerWeight, FVector* OutNormal = nullptr );
	bool IsUsingRandomScale() const;
	FVector GetDefaultScale() const;
	FVector GetRandomScale() const;
	bool IsExcluded( const FVector& LocationWithHeight );
public:
	// Data that should be retrieve-able from the outside
	FChunkCoord ChunkCoord{};
	TWeakObjectPtr<UOWGChunkLandscapeLayer> OwnerLandscapeLayer;
	int32 GrassVarietyIndex{INDEX_NONE};
protected:
	// Source data for building
	int32 ChunkWeightIndex{0};
	FOWGLandscapeGrassVariety GrassVariety;
	FRandomStream RandomStream;
	TSharedPtr<FCachedChunkLandscapeData> ChunkGrassSourceData;
	int32 HaltonBaseIndex{0};
	int32 SqrtMaxInstances{0};
	FBox MeshBox{};
	int32 DesiredInstancesPerLeaf{0};
	TWeakObjectPtr<UGrassInstancedStaticMeshComponent> RebuildInitiatorComponent;
	TSharedPtr<FThreadSafeCounter> ChunkUnloadedCounter;

	// Data for splitting up the chunk landscape into smaller segments. We currently build whole chunks, so these have predefined values
	FMatrix LocalToComponentRelative;
	FVector LocalOrigin{};
	FVector LocalExtents{};
	TArray<FBox> ExcludedBoxes;

	// Data we are building
	int32 TotalInstances{0};
	TArray<FInstancedStaticMeshInstanceData> InstanceData;
	FStaticMeshInstanceData InstanceBuffer;
	TArray<FClusterNode> ClusterTree;
	int32 OutOcclusionLayerNum{0};
	double BuildTime{0.0f};
};