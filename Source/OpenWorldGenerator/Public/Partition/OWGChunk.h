// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChunkData2D.h"
#include "ChunkLandscapeWeight.h"
#include "OWGRegionContainer.h"
#include "PCGComponent.h"
#include "PCGData.h"
#include "TerraformingBrush.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "GameFramework/Actor.h"
#include "Generation/OWGBiome.h"
#include "Partition/ChunkLandscapeMaterialManager.h"
#include "Partition/ChunkLandscapeMeshManager.h"
#include "OWGChunk.generated.h"

class UMaterialInstance;
enum class EChunkGeneratorStage : uint8;

class UOWGChunkGenerator;
class UOWGWorldGeneratorConfiguration;
class UOWGChunkGenerator;
class UDynamicMeshComponent;
class UInstancedStaticMeshComponent;
class UOWGNoiseIdentifier;
class UChunkHeightFieldCollisionComponent;
class UMaterialInstanceDynamic;
class UTexture2D;
class UTexture2DArray;
class UChunkTextureManager;
class FChunkLandscapeWeightMapDescriptor;
class FChunkBiomePalette;
struct FTerraformingBrush;
class FChunkLandscapeMeshManager;
class FChunkLandscapeMaterialManager;
class UOWGChunkLandscapeLayer;

/** Describe landscape metrics in the particular area of the landscape */
USTRUCT( BlueprintType )
struct OPENWORLDGENERATOR_API FChunkLandscapeMetrics
{
	GENERATED_BODY()

	/** Point with the minimum height, in world space */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Landscape Metrics" )
	FVector MinimumHeightPoint{ForceInit};

	/** Point with the maximum height, in world space */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Landscape Metrics" )
	FVector MaximumHeightPoint{ForceInit};

	/** Medium height (and middle point) across points in this metric */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Landscape Metrics" )
	FVector MiddleHeightPoint{ForceInit};

	/** Maximum steepness (metric of height difference) in the given region. This is normalized, to get absolute value multiply by MaximumSteepness from OWG settings */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Landscape Metrics" )
	float MaximumSteepness{0.0f};

	/** Absolute value of maximum steepness (height difference between the given 2 points), in world units */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Landscape Metrics" )
	float MaximumSteepnessAbsolute{0.0f};

	/** Average landscape layer weights along the area, if weight collection is enabled */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Landscape Metrics" )
	TMap<UOWGChunkLandscapeLayer*, float> AverageWeights;

	/** Number of points in this metrics object */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Landscape Metrics" )
	int32 NumberOfPoints{0};

	/** Merges given metrics into one using the amount of points in each as a weight of an individual metric */
	static FChunkLandscapeMetrics Merge( const UObject* WorldContext, const TArray<FChunkLandscapeMetrics>& AllMetrics );
};

/** Describes a point on chunk's landscape, with all of the additional information attached to it */
USTRUCT( BlueprintType )
struct OPENWORLDGENERATOR_API FChunkLandscapePoint
{
	GENERATED_BODY()

	/** Transform of the point. The pointed will be rotated towards the landscape normal */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Landscape Point" )
	FTransform Transform{};
	
	/** Point steepness (metric of height difference). This is a normalized value in [0;1] range */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Landscape Point" )
	float Steepness{0.0f};

	/** Biome at this point */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Landscape Point" )
	UOWGBiome* Biome{};

	/** Weights of the layers on the landscape at this point. Weight values are normalized, and only non-zero entries are present. The map is also only populated if bIncludeWeights is set */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Landscape Point" )
	TMap<UOWGChunkLandscapeLayer*, float> LayerWeights;
};

/** Describes a modification of the chunk's landscape (height and, potentially, landscape layers weights) */
USTRUCT( BlueprintType )
struct OPENWORLDGENERATOR_API FChunkLandscapeModification
{
	GENERATED_BODY()

	/** True if the height should be modified. If false, the height is left untouched and only weight map is modified */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Landscape Modification" )
	bool bModifyHeight{true};

	/** New height the area should have */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Landscape Modification" )
	float NewHeight{0.0f};

	/** New values of the landscape layers the area should have. If empty, weight map is not modified */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Landscape Modification" )
	TMap<UOWGChunkLandscapeLayer*, float> NewLayers;
};

USTRUCT()
struct OPENWORLDGENERATOR_API FChunkGeneratorBiomeMapping
{
	GENERATED_BODY()

	/** Generators for the chunk, in order of declaration */
	UPROPERTY()
	TArray<TSubclassOf<UOWGChunkGenerator>> Generators;

	/** Biomes that instigated chunk generators to be picked */
	TMap<TSubclassOf<UOWGChunkGenerator>, TArray<UOWGBiome*>> GeneratorInstigatorBiomes;
};

/** Data needed to represent the snapshot of a chunk's landscape. Can be shared across threads and different builder tasks, immutable. Invalidated by the chunk when relevant data changes. Used for grass building, PCG sampling, and other things */
struct OPENWORLDGENERATOR_API FCachedChunkLandscapeData
{
	FTransform ChunkToWorld{};
	FChunkData2D HeightMapData;
	FChunkData2D NormalMapData;
	FChunkData2D SteepnessData;
	FChunkData2D WeightMapData;
	FChunkLandscapeWeightMapDescriptor WeightMapDescriptor;
	int32 ChangelistNumber{0};
};

/**
 * Data needed to be able to sample the biomes from the chunk. Can be shared across threads and different builder tasks, immutable.
 * Exists because original biome data can be relocated when changing the TMap
 * Does not have a changelist number because we do not currently support biome map changes after it has been populated once.
 */
struct OPENWORLDGENERATOR_API FCachedChunkBiomeData
{
	FTransform ChunkToWorld{};
	FChunkBiomePalette BiomePalette{};
	FChunkData2D BiomeMap{};
};

/** Aids in sampling points from the chunk's landscape, using either cached off-main-thread data, or live data from the chunk */
class OPENWORLDGENERATOR_API FChunkLandscapePointSampler
{
private:
	FTransform ChunkToWorld{};
	const FChunkData2D* HeightMapData{};
	const FChunkData2D* NormalMapData{};
	const FChunkData2D* SteepnessData{};
	const FChunkData2D* WeightMapData{};
	const FChunkLandscapeWeightMapDescriptor* WeightMapDescriptor{};
	const FChunkData2D* BiomeMapData{};
	const FChunkBiomePalette* BiomePalette{};
public:
	/** Constructs sampler from the chunk directly. Not safe to be called outside of game thread! */
	explicit FChunkLandscapePointSampler( const AOWGChunk* Chunk );
	/** Constructs sampler from the cached data. Usable outside of game thread. Keep in mind that it is your responsibility to make sure the data stays alive over the duration of sampling! Biome data is optional. */
	FChunkLandscapePointSampler( const FCachedChunkLandscapeData* LandscapeData, const FCachedChunkBiomeData* BiomeData );

	/** Returns extents of a single sampled point. For us this is equal to the grid resolution */
	FVector GetPointExtents() const;
	/** Checks if the point at the given world location is in the bounds of the chunk being sampled */
	bool CheckPointInBounds( const FVector& WorldLocation ) const;
	
	/** Samples point height and nothing else. Interpolates the value between adjacent points */
	FTransform SamplePointTransformInterpolated( const FVector& WorldLocation ) const;
	/** Samples point height and nothing else. Snaps to grid */
	FTransform SamplePointTransformGrid( const FVector& WorldLocation ) const;
	
	/** Samples the point from the chunk's landscape. Interpolates the result */
	FChunkLandscapePoint SamplePointInterpolated( const FVector& WorldLocation ) const;
	/** Samples the point from the chunk's landscape. Does not interpolate, snaps to the closest grid cell */
	FChunkLandscapePoint SamplePointGrid( const FVector& WorldLocation ) const;

	// TODO @open-world-generator: We could use a ParallelFor version of this, given that the operation seems relatively common
	/** Performs operation on each point within the given bounds. Return false to stop iterating over the points. */
	void ForEachPointGrid( const FBox& WorldBounds, const TFunctionRef<bool(FChunkLandscapePoint& Point)>& Operation ) const;

	/** Samples point height and nothing else. Interpolates the value between adjacent points. Point's transform is in local space, and so is input position */
	FTransform SamplePointTransformInterpolated_Local( const FVector& ChunkLocalPosition ) const;
	/** Samples point height and nothing else. Snaps to grid. Point's transform is in local space, and so is input position */
	FTransform SamplePointTransformGrid_Local( const FVector& ChunkLocalPosition ) const;
	
	/** Samples the point from the chunk's landscape. Interpolates the result. Point's transform is in local space, and so is input position */
	FChunkLandscapePoint SamplePointInterpolated_Local( const FVector& ChunkLocalPosition ) const;
	/** Samples the point from the chunk's landscape. Does not interpolate, snaps to the closest grid cell. Point's transform is in local space, and so is input position */
	FChunkLandscapePoint SamplePointGrid_Local( const FVector& ChunkLocalPosition ) const;

	/** Performs operation on each point within the given bounds. Return false to stop iterating over the points. All transforms and coordinates are in local space */
	void ForEachPointGrid_Local( const FBox& ChunkLocalBounds, const TFunctionRef<bool(FChunkLandscapePoint& Point)>& Operation ) const;
private:
	/** Populates weight data on the point */
	void PopulatePointLayerWeights( FChunkLandscapePoint& OutPoint, const FChunkLandscapeWeight& Weight ) const;
};

/**
 * Chunk is a unit of world generation and serialization
 */
UCLASS( Blueprintable, HideCategories = ("Input", "Collision", "LOD", "Cooking", "Actor") )
class OPENWORLDGENERATOR_API AOWGChunk final : public AActor
{
	GENERATED_BODY()
public:
	AOWGChunk();

	// Begin AActor interface
	virtual void GetLifetimeReplicatedProps( TArray<FLifetimeProperty>& OutLifetimeProps ) const override;
	virtual void PostActorCreated() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Serialize(FArchive& Ar) override;
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
	// End AActor interface

	////////////////////////////////////////////////////////
	// CHUNK GENERAL FUNCTIONS
	////////////////////////////////////////////////////////

	/** Returns the coordinate of this chunk */
	UFUNCTION( BlueprintPure, Category = "Chunk|General" )
	FORCEINLINE FChunkCoord GetChunkCoord() const { return ChunkCoord; }

	/** Returns the region container for this chunk. Only valid on the server */
	UFUNCTION( BlueprintPure, Category = "Chunk|Loading" )
	FORCEINLINE UOWGRegionContainer* GetOwnerRegionContainer() const { return OwnerContainer; }

	/** Returns true if this chunk is considered "Idle" and will be unloaded soon */
	UFUNCTION( BlueprintPure, Category = "Chunk|Loading" )
	FORCEINLINE bool IsChunkIdle() const { return ElapsedIdleTime > 0.0f; }

	////////////////////////////////////////////////////////
	// CHUNK GENERATION FUNCTIONS
	////////////////////////////////////////////////////////

	/** Returns true if this chunk has been initialized with a height map. This usually happens as the first generation step */
	UFUNCTION( BlueprintPure, Category = "Chunk|Generation" )
	bool IsChunkInitialized() const;

	/** Returns the target generation stage for the chunk. Only valid on the server */
	UFUNCTION( BlueprintPure, Category = "Chunk|Generation" )
	FORCEINLINE EChunkGeneratorStage GetTargetGenerationStage() const { return TargetGenerationStage; }

	/** Returns the current generation stage for the chunk. Only valid on the server */
	UFUNCTION( BlueprintPure, Category = "Chunk|Generation" )
	FORCEINLINE EChunkGeneratorStage GetCurrentGenerationStage() const { return CurrentGenerationStage; }

	/**
	 * Updates the target generation stage for this chunk. That will make the chunk attempt to advance it's generation
	 * until the target state has been reached across multiple ticks, running the chunk generator pipeline in sequence
	 */
	UFUNCTION( BlueprintCallable, BlueprintAuthorityOnly, Category = "Chunk|Generation" )
	void RequestChunkGeneration( EChunkGeneratorStage InTargetGenerationStage );

	////////////////////////////////////////////////////////
	// CHUNK LANDSCAPE FUNCTIONS
	////////////////////////////////////////////////////////

	/**
	 * Calculates the native precision of the landscape data.
	 * Any modifications more granular than this precision will not be accurately reflected on the landscape.
	 * This is the precision functions such as "Get Landscape Metrics" and "Modify Landscape" will internally use.
	 * As such, it is generally recommended to use this precision when doing landscape modification calculations, but other precisions
	 * can be used for just sampling the landscape with the given granularity.
	 * 
	 * @return the native precision object of this landscape.
	 */
	UFUNCTION( BlueprintPure, Category = "Chunk|Landscape" )
	FTerraformingPrecision GetNativeLandscapePrecision() const;

	/**
	 * Samples the landscape at the given world location, and returns the information about it's properties there
	 * If you are using C++, consider using FChunkLandscapePointSampler directly, as it is considerably more performant and is thread safe.
	 *
	 * @param WorldLocation location of the landscape point, in world space. Z is not used.
	 * @return the information about a sampled point.
	 */
	UFUNCTION( BlueprintPure, Category = "Chunk|Landscape" )
	FChunkLandscapePoint GetLandscapePoint( const FVector& WorldLocation ) const;

	/**
	 * Samples given amount of points from the landscape at the specific world location, and returns these of them that are inside of the provided shape,
	 * optionally omitting the points below the specified minimum weight (useful for cutoff).
	 *
	 * @param WorldLocation location of the origin of the shape, in world space. Z is not used
	 * @param Brush the brush to filter the points on the landscape
	 * @param MinWeight minimum weight that the point should have on the brush to be included into the output
	 * @param OutPoints the resulting set of points
	 */
	UFUNCTION( BlueprintCallable, Category = "Chunk|Landscape" )
	void GetLandscapePoints( const FVector& WorldLocation, const FPolymorphicTerraformingBrush& Brush, TArray<FChunkLandscapePoint>& OutPoints, float MinWeight = 0.0f );

	/**
	 * Samples the landscape at the given world location using the provided brush, filtering the points that are equal to or above minimum weight,
	 * and returns the information about the shape of the terrain across these points.
	 *
	 * @param WorldLocation location of the origin of the shape, in world space. Z is not used
	 * @param Brush the brush to filter the area on the landscape
	 * @param bIncludeWeights true if the averaged weight map data for the area should be included into the metrics
	 * @param MinWeight minimum weight that the point on the brush must have to be considered for sampling. Only relevant when brushes have Falloff set.
	 * @return landscape metrics for the points inside of the brush. If no points are sampled, the defaults values are zero.
	 */
	UFUNCTION( BlueprintCallable, Category = "Chunk|Landscape" )
	FChunkLandscapeMetrics GetLandscapeMetrics( const FVector& WorldLocation, const FPolymorphicTerraformingBrush& Brush, bool bIncludeWeights = true, float MinWeight = 0.0f ); 

	/**
	 * Applies the given terraforming brush to the given world location with the provided rotation and scale.
	 * Keep in mind that this function does not operate across the chunk boundaries and as such should only be used when the brush in question fully fits inside of the chunk
	 * The location provided should be in world space, and will be automatically converted to the chunk local space
	 *
	 * @param WorldLocation location of the origin of the shape, in world space. Z is not used
	 * @param Brush the brush to apply to the landscape height map
	 * @param LandscapeModification the modification to apply to the area selected by the brush
	 * @param MinWeight points with weight below that value will not be terraformed
	 */
	UFUNCTION( BlueprintCallable, Category = "Chunk|Landscape" )
	void ModifyLandscape( const FVector& WorldLocation, const FPolymorphicTerraformingBrush& Brush, const FChunkLandscapeModification& LandscapeModification, float MinWeight = 0.0f);

	////////////////////////////////////////////////////////
	// CHUNK UTILITY/ADVANCED FUNCTIONS
	////////////////////////////////////////////////////////

	/** Returns the world generator for this world. Shortcut for the world generator subsystem */
	UFUNCTION( BlueprintPure, Category = "Chunk|Utility" )
	FORCEINLINE UOWGWorldGeneratorConfiguration* GetWorldGeneratorDefinition() const { return WorldGeneratorDefinition; }

	/** Returns the world seed for the world the chunk is located in. Shortcut for the world generator subsystem */
	UFUNCTION( BlueprintPure, Category = "Chunk|Utility" )
	FORCEINLINE int32 GetWorldSeed() const { return WorldSeed; }

	/** Returns a list of landscape layers currently defined in the chunk */
	UFUNCTION( BlueprintPure, Category = "Chunk|Advanced" )
	TArray<UOWGChunkLandscapeLayer*> GetLandscapeLayers() const;

	/** Adds the given actor as a child to this chunk. That means it will be serialized and loaded with the chunk */
	UFUNCTION( BlueprintCallable, Category = "Chunk|Utility" )
	void AddChunkChildActor( AActor* InChunkChildActor );
	
	/** Updates the chunk LOD the landscape should use. This might not immediately set the provided LOD mesh active, as mesh generation is done in the background */
	void RequestChunkLOD( int32 NewChunkLOD );

	/**
	 * Returns value of the specific noise at the given world location
	 *
	 * @param WorldLocation location, in world space. Z is not used.
	 * @param NoiseIdentifier identifier of the noise value of which is retrieved
	 * @return value of the noise at that specific location
	 */
	UFUNCTION( BlueprintPure, Category = "Chunk|Advanced" )
	float GetNoiseValueAtLocation( const FVector& WorldLocation, const UOWGNoiseIdentifier* NoiseIdentifier ) const;

	/** Returns raw noise data with the given identifier */
	const FChunkData2D* FindRawNoiseData( const UOWGNoiseIdentifier* NoiseIdentifier ) const;
	/** Returns the raw, immutable chunk data for the given Chunk Data ID. This is a low level function to use when large quantity of data needs to be retrieved */
	const FChunkData2D* FindRawChunkData( FName ChunkDataID ) const;

	const FChunkBiomePalette* GetBiomePalette() const { return &BiomePalette; }
	const FChunkLandscapeWeightMapDescriptor* GetWeightMapDescriptor() const { return &WeightMapDescriptor; }

	FORCEINLINE FChunkLandscapeMeshManager* GetLandscapeMeshManager() const { return LandscapeMeshManager.Get(); }
	FORCEINLINE FChunkLandscapeMaterialManager* GetLandscapeMaterialManager() const { return LandscapeMaterialManager.Get(); }
	FORCEINLINE int32 GetCurrentChunkLOD() const { return CurrentChunkLOD; }
public:
	/** Internal function to initialize the chunk's biome palette with the given values */
	void InitializeChunkBiomePalette( FChunkBiomePalette&& InBiomePalette, FChunkData2D&& InBiomeMap );
	/** Internal function to initialize the chunk landscape with the given heightmap and the weight map */
	void InitializeChunkLandscape( FChunkLandscapeWeightMapDescriptor&& InWeightMapDescriptor, FChunkData2D&& InHeightMap, FChunkData2D&& InWeightMap );

	/** Returns cached chunk landscape source data, or allocates a new one if data in the current one is out of date */
	TSharedRef<FCachedChunkLandscapeData> GetChunkLandscapeSourceData();
	/** Returns cached chunk biome data, or allocates one if it has not been requested before */
	TSharedRef<FCachedChunkBiomeData> GetChunkBiomeData();

	/** Partially recalculate the surface data for the given area of the chunk surface. The rectangle is capped into the valid range. */
	void PartialRecalculateSurfaceData( const FBox2f& UpdateVolume );
	void PartialUpdateWeightMap( const FBox2f& UpdateVolume );

	FORCEINLINE bool IsPendingToBeUnloaded() const { return bPendingToBeUnloaded; }
protected:
	friend class UOWGRegionContainer;
	friend class FChunkSerializationContext;
	friend class UOWGServerChunkManager;

	/** Called before the chunk has begun play or has been added to the container to initialize it with basic data */
	void SetupChunk( UOWGRegionContainer* InOwnerContainer, const FChunkCoord& InChunkCoord );

	/** Called after the chunk has been deserialized from the filesystem */
	void OnChunkLoaded();

	/** Called right before the chunk is about to be persisted and then unloaded. Gives a chance to cleanup some transient resources or pending tasks */
	void OnChunkAboutToBeUnloaded();

	/** Returns true if we should defer chunk unloading because there are some pending tasks currently running that cannot be persisted in a reliable way (for example, the PCG generation running) */
	bool ShouldDeferChunkUnloading() const;

	/** Called on an empty chunk right after it has been spawned into the world and added to the region container */
	void OnChunkCreated();

	/** Samples all predefined noise generators for this chunk */
	void GenerateNoiseForChunk();

	/** Attempts to advance the chunk generation to the next state. Returns false if chunk generation is still ongoing and the callback needs to be triggered next frame, true if it is finished */
	bool ProcessChunkGeneration();

	/** Draws chunk visualization info */
	void DrawDebugHUD( class AHUD* HUD, UCanvas* Canvas, const FDebugDisplayInfo& DisplayInfo ) const;

	/** Collects references to other actors from this chunk. Called to determine which actors should be destroyed when the chunk is unloaded */
	void CollectActorReferences( TArray<AActor*>& OutActorReferences ) const;

	void ModifyLandscapeHeightsInternal( const FVector& WorldLocation, const FPolymorphicTerraformingBrush& Brush, float NewLandscapeHeight, float MinWeight );
	void ModifyLandscapeWeightsInternal( const FVector& WorldLocation, const FPolymorphicTerraformingBrush& Brush, const FChunkLandscapeWeight& NewLandscapeWeight, float MinWeight );

	/** Functions for partially updating various data across the chunk */
	void PartialUpdateSurfaceGradient( int32 StartX, int32 StartY, int32 EndX, int32 EndY );
	void PartialUpdateSurfaceNormal( int32 StartX, int32 StartY, int32 EndX, int32 EndY );

	void RecalculateCurrentStageGenerators();
protected:
	/** Mesh component for the landscape surface mesh (primary terrain floor mesh) */
	UPROPERTY( VisibleInstanceOnly, Transient, Category = "Chunk" )
	TObjectPtr<UDynamicMeshComponent> LandscapeMeshComponent;

	/** Collision component for this chunk */
	UPROPERTY( VisibleAnywhere, Category = "Chunk" )
	TObjectPtr<UChunkHeightFieldCollisionComponent> HeightFieldCollisionComponent;
	
public:
	/** Root component that can be used to attach other components to this actor */
	UPROPERTY( VisibleInstanceOnly, BlueprintReadOnly, Category = "Chunk" )
	USceneComponent* SceneRootComponent;

	/** Child actors owned by this chunk that should be serialized with it, and also destroyed when the chunk is unloaded. The actors in question should also be owned by this chunk by calling SetOwner */
	UPROPERTY( VisibleInstanceOnly, BlueprintReadWrite, Category = "Chunk" )
	TArray<AActor*> ChunkChildActors;

protected:
	friend class UChunkHeightFieldCollisionComponent;
	friend class UOWGServerChunkManager;
	friend class FChunkLandscapeMeshManager;
	friend class FChunkLandscapeMaterialManager;

	/** Noise data for each noise identifier generated for this chunk */
	TMap<TObjectPtr<UOWGNoiseIdentifier>, FChunkData2D> NoiseData;

	/** Surface data maps used during chunk generation */
	TMap<FName, FChunkData2D> ChunkData2D;

	/** Weight map descriptor for this chunk. Describes what layers are defined and where they are stored */
	FChunkLandscapeWeightMapDescriptor WeightMapDescriptor;
	/** Biome palette for the chunk. Maps local palette indices to the global biome descriptors */
	FChunkBiomePalette BiomePalette;

	/** Amount of time this chunk has been idle. Once it hits the threshold, the chunk is unloaded */
	float ElapsedIdleTime;
	/** True if we have elapsed all of our idle time and are pending to be unloaded */
	bool bPendingToBeUnloaded{false};
	/** Distance from the chunk to the closest streaming source. Used to prioritize chunk generation */
	float DistanceToClosestStreamingSource{-1.0f};

	int32 GrassSourceDataChangelistNumber{0};
	TSharedPtr<FCachedChunkLandscapeData> CachedLandscapeData;
	TSharedPtr<FCachedChunkBiomeData> CachedBiomeData;
public:
	/** PCGComponent for this actor. Never saved, created in BeginPlay and is Transient */
	UPROPERTY( VisibleInstanceOnly, Transient, BlueprintReadOnly, Category = "PCG" )
	TObjectPtr<UPCGComponent> PCGComponent;
private:
	/** World coordinate of this chunk. Populated on chunk creation/load and not serialized. */
	UPROPERTY( Transient, Replicated )
	FChunkCoord ChunkCoord;

	/** Region container that owns this chunk */
	UPROPERTY( Transient )
	TObjectPtr<UOWGRegionContainer> OwnerContainer;

public:
	/** Number of chunk LODs we should support */
	UPROPERTY( EditDefaultsOnly, Category = "Chunk|General" )
	int32 NumChunkLandscapeLODs;

protected:
	/** Material manager for this landscape */
	TUniquePtr<FChunkLandscapeMaterialManager> LandscapeMaterialManager;
	/** Mesh manager for this chunk's landscape */
	TUniquePtr<FChunkLandscapeMeshManager> LandscapeMeshManager;
	
	/** Current generation stage for this chunk. Can be advanced by calling RequestChunkGeneration */
	UPROPERTY()
	EChunkGeneratorStage CurrentGenerationStage;
	/** Chunk generation state that has been requested by the outside code. Highest value set wins. */
	UPROPERTY()
	EChunkGeneratorStage TargetGenerationStage;

	/** The chunk generator that we are currently trying to advance */
	UPROPERTY()
	int32 CurrentGeneratorIndex;
	UPROPERTY( Transient )
	FChunkGeneratorBiomeMapping CurrentStageChunkGenerators;

	/** Instance of the currently active chunk generator */
	UPROPERTY()
	TObjectPtr<UOWGChunkGenerator> CurrentGeneratorInstance;

	/** Index of the current chunk LOD */
	int32 CurrentChunkLOD{INDEX_NONE};

	// Local cache of various objects

	/** Cached world generator configuration from subsystem object */
	UPROPERTY( Transient )
	TObjectPtr<UOWGWorldGeneratorConfiguration> WorldGeneratorDefinition;
	/** Cached world seed of the world the chunk is in */
	UPROPERTY( Transient )
	int32 WorldSeed;
};
