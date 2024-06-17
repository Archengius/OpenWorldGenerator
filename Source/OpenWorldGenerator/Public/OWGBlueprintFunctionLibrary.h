// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Partition/OWGChunk.h"
#include "Partition/TerraformingBrush.h"
#include "OWGBlueprintFunctionLibrary.generated.h"

class AOWGChunk;
struct FChunkLandscapeModification;

UCLASS()
class OPENWORLDGENERATOR_API UOWGBlueprintFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/**
	 * Begins a process of actor spawn.
	 * This will create the actor instance and add it to the world, but will not dispatch FinishSpawning or BeginPlay it.
	 * You need to manually finish this up by calling FinishSpawning to be able to use the resulting actor.
	 * This function is very useful when certain attributes of the actor need to be tweaked before they get passed to the Construction Script or Begin Play of the actor in question.
	 */
	UFUNCTION( BlueprintCallable, Category = "Chunk|Generation|Advanced", meta = ( WorldContext = "WorldContext", DeterminesOutputType = "ActorClass" ) )
	static AActor* BeginSpawnActorDeferred( const UObject* WorldContext, TSubclassOf<AActor> ActorClass, const FTransform& ActorTransform );

	/**
	 * Finishes a process of spawning an actor. To be called on an actor spawned through Begin Spawn Actor Deferred
	 */
	UFUNCTION( BlueprintCallable, Category = "Chunk|Generation|Advanced" )
	static void FinishSpawnActor( AActor* Actor, bool bOverrideTransform = false, const FTransform& OverrideTransform = FTransform() );

	/** Converts a box terraforming brush into a polymorphic brush */
	UFUNCTION( BlueprintPure, meta = ( DisplayName = "To Polymorphic Brush", CompactNodeTitle = "->", BlueprintAutocast ), Category = "Chunk|Landscape" )
	static FPolymorphicTerraformingBrush Conv_BoxToPolymorphicBrush( const FBoxTerraformingBrush& BoxTerraformingBrush );

	/** Converts an ellipse terraforming brush into a polymorphic brush */
	UFUNCTION( BlueprintPure, meta = ( DisplayName = "To Polymorphic Brush", CompactNodeTitle = "->", BlueprintAutocast ), Category = "Chunk|Landscape" )
	static FPolymorphicTerraformingBrush Conv_EllipseToPolymorphicBrush( const FEllipseTerraformingBrush& BoxTerraformingBrush );

	/** Returns the world space extents of this polymorphic brush */
	UFUNCTION( BlueprintPure, Category = "Chunk|Landscape" )
	static FVector2D GetPolymorphicBrushExtents( const FPolymorphicTerraformingBrush& PolymorphicBrush );

	/**
	 * Samples the landscape at the given world location, and returns the information about it's properties there
	 *
	 * @param WorldContext the world to operate on. Must be an object that is associated with a UWorld.
	 * @param WorldLocation location of the landscape point, in world space. Z is not used.
	 * @return the information about a sampled point.
	 */
	UFUNCTION( BlueprintCallable, Category = "Chunk|Landscape", meta = ( WorldContext = "WorldContext" ) )
	static FChunkLandscapePoint GetChunkLandscapePoint( const UObject* WorldContext, const FVector& WorldLocation );

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
	UFUNCTION( BlueprintCallable, Category = "Chunk|Landscape", meta = ( WorldContext = "WorldContext" ) )
	static FChunkLandscapeMetrics GetChunkLandscapeMetrics( const UObject* WorldContext, const FVector& WorldLocation, const FPolymorphicTerraformingBrush& Brush, bool bIncludeWeights = true, float MinWeight = 0.0f ); 

	/**
	 * Applies the given terraforming brush to the given world location with the provided rotation and scale.
	 * This function will modify all of the chunks in the extents of the brush in question, with the condition that they should be loaded and generated beforehand.
	 * The location provided should be in world space, and will be automatically converted to the chunk local space
	 *
	 * @param WorldContext the world to operate on. Must be an object that is associated with a UWorld.
	 * @param WorldLocation location of the origin of the shape, in world space. Z is not used
	 * @param Brush the brush to apply to the landscape height map
	 * @param LandscapeModification the modification to apply to the area selected by the brush
	 * @param MinWeight points with weight below that value will not be terraformed
	 */
	UFUNCTION( BlueprintCallable, Category = "Chunk|Landscape", meta = ( WorldContext = "WorldContext" ) )
	static void ModifyWorldLandscape( const UObject* WorldContext, const FVector& WorldLocation, const FPolymorphicTerraformingBrush& Brush, const FChunkLandscapeModification& LandscapeModification, float MinWeight = 0.0f);

	/** Returns all loaded chunks contained inside the bounding box centered at the world location with the size provided in the box extents */
	UFUNCTION( BlueprintCallable, Category = "Chunk", meta = ( WorldContext = "WorldContext" ) )
	static void GetLoadedChunksInBoundingBox( const UObject* WorldContext, const FVector& WorldLocation, const FVector& BoxExtents, TArray<AOWGChunk*>& OutChunks );
	
	UFUNCTION( BlueprintCallable, Category = "Chunk|Landscape" )
	static FChunkLandscapeMetrics GetChunkLandscapeMetrics_1( const FPolymorphicTerraformingBrush& Brush );
};
