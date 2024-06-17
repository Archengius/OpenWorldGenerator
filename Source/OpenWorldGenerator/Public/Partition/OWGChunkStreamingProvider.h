// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChunkCoord.h"
#include "Generation/OWGChunkGenerator.h"
#include "UObject/Interface.h"
#include "OWGChunkStreamingProvider.generated.h"

enum class EChunkGeneratorStage : uint8;

/** Information about a loaded chunk from the streaming source */
struct FLoadedChunkInfo
{
	EChunkGeneratorStage GeneratorStage{};
	int32 ChunkLOD{INDEX_NONE};
	float DistanceToChunk{0.0f};
};

/** Describes a streaming source for loading chunks around it */
struct OPENWORLDGENERATOR_API FChunkStreamingSource
{
	FBoxSphereBounds BoxSphereBounds;
	bool bIsRadiusSource{false};
	EChunkGeneratorStage ChunkGeneratorStage{};
	int32 ChunkLOD{INDEX_NONE};

	FChunkStreamingSource() = default;

	/** Constructs a box-shaped streaming source with a given origin and box extents */
	FChunkStreamingSource( EChunkGeneratorStage InTargetStage, int32 InChunkLOD, const FVector& InOrigin, const FVector& InExtent ) : BoxSphereBounds( InOrigin, InExtent, InExtent.GetMax() ),
		ChunkGeneratorStage( InTargetStage ), ChunkLOD( InChunkLOD )
	{
	}

	/** Constructs a sphere-shaped streaming source with a given origin and radius */
	 FChunkStreamingSource( EChunkGeneratorStage InTargetStage, int32 InChunkLOD, const FVector& InOrigin, float InRadius ) : BoxSphereBounds( InOrigin, FVector( InRadius ), InRadius ),
		bIsRadiusSource( true ), ChunkGeneratorStage( InTargetStage ), ChunkLOD( InChunkLOD )
	{
	}

	/** Returns a set of chunk coordinates loaded by this source */
	void GetLoadedChunkCoords( TMap<FChunkCoord, FLoadedChunkInfo>& OutLoadedChunkCoords ) const
	{
		const int32 ChunkRadiusSquared = FMath::Square( FMath::CeilToInt32( BoxSphereBounds.SphereRadius / FChunkCoord::ChunkSizeWorldUnits ) );

		const FChunkCoord OriginChunkCoord = FChunkCoord::FromWorldLocation( BoxSphereBounds.Origin );
		const FChunkCoord MinChunkCoord = FChunkCoord::FromWorldLocation( BoxSphereBounds.Origin - BoxSphereBounds.BoxExtent );
		const FChunkCoord MaxChunkCoord = FChunkCoord::FromWorldLocation( BoxSphereBounds.Origin + BoxSphereBounds.BoxExtent );

		for ( int32 ChunkX = MinChunkCoord.PosX; ChunkX <= MaxChunkCoord.PosX; ChunkX++ )
		{
			for ( int32 ChunkY = MinChunkCoord.PosY; ChunkY <= MaxChunkCoord.PosY; ChunkY++ )
			{
				// Strip corner chunks in case of sphere like streaming source
				if ( bIsRadiusSource )
				{
					const int32 ChunkDistanceSquared = FMath::Square( ChunkX - OriginChunkCoord.PosX ) + FMath::Square( ChunkY - OriginChunkCoord.PosY );
					if ( ChunkDistanceSquared > ChunkRadiusSquared ) continue;
				}

				// If we already have a chunk in the map, take the element with the biggest stage
				const FChunkCoord SelfChunkCoord( ChunkX, ChunkY );

				// Non-radius based streaming sources do not LOD chunks at all
				const float ChunkDistance = bIsRadiusSource ? FVector::Dist2D( BoxSphereBounds.Origin, SelfChunkCoord.ToOriginWorldLocation() ) : 0.0f;
		
				if ( FLoadedChunkInfo* ExistingChunkInfo = OutLoadedChunkCoords.Find( SelfChunkCoord ) )
				{
					ExistingChunkInfo->GeneratorStage = FMath::Max( ExistingChunkInfo->GeneratorStage, ChunkGeneratorStage );
					ExistingChunkInfo->ChunkLOD = FMath::Min( ExistingChunkInfo->ChunkLOD, ChunkLOD );
					ExistingChunkInfo->DistanceToChunk = FMath::Min( ExistingChunkInfo->DistanceToChunk, ChunkDistance );
				}
				else
				{
					FLoadedChunkInfo LoadedChunkInfo;
					LoadedChunkInfo.GeneratorStage = ChunkGeneratorStage;
					LoadedChunkInfo.ChunkLOD = ChunkLOD;
					LoadedChunkInfo.DistanceToChunk = ChunkDistance;

					OutLoadedChunkCoords.Add( SelfChunkCoord, LoadedChunkInfo );
				}
			}
		}
	}
};

UINTERFACE()
class OPENWORLDGENERATOR_API UOWGChunkStreamingProvider : public UInterface
{
	GENERATED_BODY()
};

class OPENWORLDGENERATOR_API IOWGChunkStreamingProvider
{
	GENERATED_BODY()
public:
	/** Populates a list of streaming sources for this provider */
	virtual void GetStreamingSources( TArray<FChunkStreamingSource>& OutStreamingSources ) const = 0;
};