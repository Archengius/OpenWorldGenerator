// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChunkCoord.generated.h"

/** Chunk coordinate type */
USTRUCT( BlueprintType )
struct FChunkCoord
{
	GENERATED_BODY()

	// Number of chunks in a single region. All the chunks in the same section are stored as an individual file on the disk.
	static constexpr int32 ChunksPerRegion = 32;
	// Size of a single chunk across one axis, in world units.
	static constexpr int32 ChunkSizeWorldUnits = 25600.0f;

	/** X chunk coordinate */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Chunk Coord" )
	int32 PosX{};

	/** Y chunk coordinate */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Chunk Coord" )
	int32 PosY{};

	FORCEINLINE FChunkCoord() = default;
	FORCEINLINE FChunkCoord( const int32 InPosX, const int32 InPosY ) : PosX( InPosX ), PosY( InPosY ) {}

	/** Creates chunk coordinate for the given world coordinate */
	static FChunkCoord FromWorldLocation( const FVector& InOriginLocation )
	{
		return FChunkCoord( FMath::FloorToInt32( InOriginLocation.X / ChunkSizeWorldUnits ), FMath::FloorToInt32( InOriginLocation.Y / ChunkSizeWorldUnits ) );
	}

	/** Converts this chunk coordinate into a section coord */
	FORCEINLINE FChunkCoord ToRegionCoord() const
	{
		return FChunkCoord( FMath::FloorToInt32( PosX * 1.0f / ChunksPerRegion ), FMath::FloorToInt32( PosY * 1.0f / ChunksPerRegion ) );
	}

	/** If this is a region coord, converts it to a chunk coord */
	FORCEINLINE FChunkCoord SectionToChunkCoord( const int32 OffsetX, const int32 OffsetY ) const
	{
		check( OffsetX >= 0 && OffsetX < ChunksPerRegion );
		check( OffsetY >= 0 && OffsetY < ChunksPerRegion );
		return FChunkCoord( PosX * ChunksPerRegion + OffsetX, PosY * ChunksPerRegion + OffsetY );
	}

	/** Converts this chunk coordinate to the origin location of the chunk it represents */
	FORCEINLINE FVector ToOriginWorldLocation() const
	{
		constexpr float HalfChunkSize = ChunkSizeWorldUnits / 2.0f;
		return FVector( PosX * ChunkSizeWorldUnits + HalfChunkSize, PosY * ChunkSizeWorldUnits + HalfChunkSize, 0.0f );
	}

	/** Converts region coordinate to the origin location of it (e.g. an origin of a 32x32 chunk group) */
	FORCEINLINE FVector ToRegionOriginWorldLocation() const
	{
		constexpr float RegionSize = ChunkSizeWorldUnits * ChunksPerRegion;
		constexpr float HalfRegionSize = RegionSize;
		return FVector( PosX * RegionSize + HalfRegionSize, PosY * RegionSize + HalfRegionSize, ChunkSizeWorldUnits / 2.0f ); 
	}

	/** Comparison operators between chunk coordinates */
	FORCEINLINE friend bool operator==( const FChunkCoord A, const FChunkCoord B )
	{
		return A.PosX == B.PosX && A.PosY == B.PosY;
	}

	/** Comparison operators between chunk coordinates */
	FORCEINLINE friend bool operator<( const FChunkCoord A, const FChunkCoord B )
	{
		if ( A.PosX == B.PosX )
		{
			return A.PosY < B.PosY;
		}
		return A.PosX < B.PosX;
	}

	/** Comparison operators between chunk coordinates */
	FORCEINLINE friend FArchive& operator<<( FArchive& Ar, FChunkCoord& ChunkCoord )
	{
		Ar << ChunkCoord.PosX;
		Ar << ChunkCoord.PosY;
		return Ar;
	}
};

/** Type hash implementation for chunk coordinates */
FORCEINLINE uint32 GetTypeHash( FChunkCoord ChunkCoord )
{
	return HashCombine( (uint32)ChunkCoord.PosX, (uint32)ChunkCoord.PosY );
}
