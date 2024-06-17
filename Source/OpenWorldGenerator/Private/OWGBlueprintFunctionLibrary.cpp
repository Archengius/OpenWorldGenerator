// Copyright Nikita Zolotukhin. All Rights Reserved.

#include "OWGBlueprintFunctionLibrary.h"
#include "OpenWorldGeneratorSubsystem.h"
#include "Partition/OWGChunk.h"
#include "Partition/OWGChunkManagerInterface.h"

AActor* UOWGBlueprintFunctionLibrary::BeginSpawnActorDeferred( const UObject* WorldContext, TSubclassOf<AActor> ActorClass, const FTransform& ActorTransform )
{
	if ( UWorld* World = GEngine->GetWorldFromContextObject( WorldContext, EGetWorldErrorMode::ReturnNull ) )
	{
		FActorSpawnParameters SpawnInfo;
		SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnInfo.TransformScaleMethod = ESpawnActorScaleMethod::MultiplyWithRoot;
		SpawnInfo.bDeferConstruction = true;
	
		return World->SpawnActor( ActorClass, &ActorTransform, SpawnInfo );
	}
	return nullptr;
}

void UOWGBlueprintFunctionLibrary::FinishSpawnActor( AActor* Actor, bool bOverrideTransform, const FTransform& OverrideTransform )
{
	if ( IsValid( Actor ) && !Actor->HasActorBegunPlay() )
	{
		Actor->FinishSpawning( OverrideTransform, !bOverrideTransform );
	}
}

FPolymorphicTerraformingBrush UOWGBlueprintFunctionLibrary::Conv_BoxToPolymorphicBrush( const FBoxTerraformingBrush& BoxTerraformingBrush )
{
	return FPolymorphicTerraformingBrush( BoxTerraformingBrush );
}

FPolymorphicTerraformingBrush UOWGBlueprintFunctionLibrary::Conv_EllipseToPolymorphicBrush( const FEllipseTerraformingBrush& BoxTerraformingBrush )
{
	return FPolymorphicTerraformingBrush( BoxTerraformingBrush );
}

FVector2D UOWGBlueprintFunctionLibrary::GetPolymorphicBrushExtents( const FPolymorphicTerraformingBrush& PolymorphicBrush )
{
	return FVector2D( PolymorphicBrush->GetBrushExtents() );
}

void UOWGBlueprintFunctionLibrary::ModifyWorldLandscape( const UObject* WorldContext, const FVector& WorldLocation, const FPolymorphicTerraformingBrush& Brush, const FChunkLandscapeModification& LandscapeModification, float MinWeight )
{
	TArray<AOWGChunk*> LoadedChunks;
	const FVector BrushExtents = FVector( FVector2d( Brush->GetBrushExtents() ), 0.0f );
	GetLoadedChunksInBoundingBox( WorldContext, WorldLocation, BrushExtents, LoadedChunks );

	// Apply the modification to each chunk
	for ( AOWGChunk* LoadedChunk : LoadedChunks )
	{
		LoadedChunk->ModifyLandscape( WorldLocation, Brush, LandscapeModification, MinWeight );
	}
}

FChunkLandscapeMetrics UOWGBlueprintFunctionLibrary::GetChunkLandscapeMetrics( const UObject* WorldContext, const FVector& WorldLocation, const FPolymorphicTerraformingBrush& Brush, bool bIncludeWeights, float MinWeight )
{
	TArray<AOWGChunk*> LoadedChunks;
	const FVector BrushExtents = FVector( FVector2d( Brush->GetBrushExtents() ), 0.0f );
	GetLoadedChunksInBoundingBox( WorldContext, WorldLocation, BrushExtents, LoadedChunks );

	// Sample the metrics from each chunk along the way, and then combine them all
	TArray<FChunkLandscapeMetrics> PerChunkMetrics;
	for ( AOWGChunk* LoadedChunk : LoadedChunks )
	{
		PerChunkMetrics.Add( LoadedChunk->GetLandscapeMetrics( WorldLocation, Brush, bIncludeWeights, MinWeight ) );
	}
	return FChunkLandscapeMetrics::Merge( WorldContext, PerChunkMetrics );
}

FChunkLandscapePoint UOWGBlueprintFunctionLibrary::GetChunkLandscapePoint( const UObject* WorldContext, const FVector& WorldLocation )
{
	if ( const UOpenWorldGeneratorSubsystem* OpenWorldGeneratorSubsystem = UOpenWorldGeneratorSubsystem::Get( WorldContext ) )
	{
		const FChunkCoord ChunkCoord = FChunkCoord::FromWorldLocation( WorldLocation );
		AOWGChunk* Chunk = OpenWorldGeneratorSubsystem->GetChunkManager()->FindChunk( ChunkCoord );

		if ( Chunk && Chunk->IsChunkInitialized() )
		{
			return Chunk->GetLandscapePoint( WorldLocation );
		}
	}

	// Return empty point with only position and normal populated
	FChunkLandscapePoint EmptyLandscapePoint{};
	return EmptyLandscapePoint;
}

void UOWGBlueprintFunctionLibrary::GetLoadedChunksInBoundingBox( const UObject* WorldContext, const FVector& WorldLocation, const FVector& BoxExtents, TArray<AOWGChunk*>& OutChunks )
{
	OutChunks.Reset();
	
	if ( const UOpenWorldGeneratorSubsystem* OpenWorldGeneratorSubsystem = UOpenWorldGeneratorSubsystem::Get( WorldContext ) )
	{
		const FChunkCoord MinChunkCoord = FChunkCoord::FromWorldLocation( WorldLocation - BoxExtents );
		const FChunkCoord MaxChunkCoord = FChunkCoord::FromWorldLocation( WorldLocation + BoxExtents );

		for ( int32 ChunkX = MinChunkCoord.PosX; ChunkX <= MaxChunkCoord.PosX; ChunkX++ )
		{
			for ( int32 ChunkY = MinChunkCoord.PosY; ChunkY <= MaxChunkCoord.PosY; ChunkY++ )
			{
				const FChunkCoord ThisChunkCoord( ChunkX, ChunkY );
				AOWGChunk* LoadedChunk = OpenWorldGeneratorSubsystem->GetChunkManager()->FindChunk( ThisChunkCoord );
				if ( LoadedChunk && LoadedChunk->IsChunkInitialized() )
				{
					OutChunks.Add( LoadedChunk );
				}
			}
		}
	}
}

FChunkLandscapeMetrics UOWGBlueprintFunctionLibrary::GetChunkLandscapeMetrics_1( const FPolymorphicTerraformingBrush& Brush )
{
	return FChunkLandscapeMetrics{};
}
