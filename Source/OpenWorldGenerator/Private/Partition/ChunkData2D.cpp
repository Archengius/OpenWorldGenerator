// Copyright Nikita Zolotukhin. All Rights Reserved.

#include "Partition/ChunkData2D.h"

const FName ChunkDataID::SurfaceHeightmap( TEXT("SurfaceHeightmap") );
const FName ChunkDataID::SurfaceNormal( TEXT("SurfaceNormal") );
const FName ChunkDataID::SurfaceGradient( TEXT("SurfaceGradient") );
const FName ChunkDataID::SurfaceSteepness( TEXT("SurfaceSteepness") );
const FName ChunkDataID::SurfaceWeights( TEXT("SurfaceWeights") );
const FName ChunkDataID::BiomeMap( TEXT("BiomeMap") );

FChunkData2D::FChunkData2D()
{
}

FChunkData2D::FChunkData2D( int32 InSurfaceResolutionXY, int32 InDataElementSize, bool InAllowInterpolation ) : DataElementSize( InDataElementSize ), SurfaceResolutionXY( InSurfaceResolutionXY ), bAllowInterpolation( InAllowInterpolation )
{
	check( SurfaceResolutionXY >= 0 );
	check( DataElementSize > 0 || ( DataElementSize == 0 && SurfaceResolutionXY == 0 ) );

	if ( SurfaceResolutionXY > 0 )
	{
		const int32 TotalDataSize = SurfaceResolutionXY * SurfaceResolutionXY * DataElementSize;

		SurfaceDataPtr = FMemory::Malloc( TotalDataSize );
		FMemory::Memzero( SurfaceDataPtr, TotalDataSize );
	}
}

FChunkData2D::~FChunkData2D()
{
	if ( SurfaceDataPtr )
	{
		FMemory::Free( SurfaceDataPtr );
		SurfaceDataPtr = nullptr;
	}
}

FChunkData2D::FChunkData2D( const FChunkData2D& InOther ) : DataElementSize( InOther.DataElementSize ), SurfaceResolutionXY( InOther.SurfaceResolutionXY ), bAllowInterpolation( InOther.bAllowInterpolation )
{
	if ( SurfaceResolutionXY > 0 && InOther.SurfaceDataPtr )
	{
		const int32 TotalDataSize = SurfaceResolutionXY * SurfaceResolutionXY * DataElementSize;

		SurfaceDataPtr = FMemory::Malloc( TotalDataSize );
		FMemory::Memcpy( SurfaceDataPtr, InOther.SurfaceDataPtr, TotalDataSize );
	}
}

FChunkData2D::FChunkData2D( FChunkData2D&& InOther ) noexcept : DataElementSize( InOther.DataElementSize ), SurfaceResolutionXY( InOther.SurfaceResolutionXY ), bAllowInterpolation( InOther.bAllowInterpolation )
{
	if ( SurfaceResolutionXY > 0 && InOther.SurfaceDataPtr )
	{
		SurfaceDataPtr = InOther.SurfaceDataPtr;

		InOther.SurfaceDataPtr = nullptr;
		InOther.SurfaceResolutionXY = 0;
	}
}

FChunkData2D& FChunkData2D::operator=( const FChunkData2D& InOther )
{
	if ( this != &InOther )
	{
		// Free old data
		if ( SurfaceDataPtr )
		{
			FMemory::Free( SurfaceDataPtr );
			SurfaceDataPtr = nullptr;
		}

		// Copy other data properties
		DataElementSize = InOther.DataElementSize;
		SurfaceResolutionXY = InOther.SurfaceResolutionXY;
		bAllowInterpolation = InOther.bAllowInterpolation;

		// Copy element data from the other data object if it had any
		if ( SurfaceResolutionXY > 0 && InOther.SurfaceDataPtr )
		{
			const int32 TotalDataSize = SurfaceResolutionXY * SurfaceResolutionXY * DataElementSize;

			SurfaceDataPtr = FMemory::Malloc( TotalDataSize );
			FMemory::Memcpy( SurfaceDataPtr, InOther.SurfaceDataPtr, TotalDataSize );
		}
	}
	return *this;
}

FChunkData2D& FChunkData2D::operator=( FChunkData2D&& InOther ) noexcept
{
	if ( this != &InOther )
	{
		// Swap the data with another object. Since both objects are in coherent state, the old object will end up in one too
		Swap( DataElementSize, InOther.DataElementSize );
		Swap( SurfaceResolutionXY, InOther.SurfaceResolutionXY );
		Swap( SurfaceDataPtr, InOther.SurfaceDataPtr );
		Swap( bAllowInterpolation, InOther.bAllowInterpolation );
	}
	return *this;
}

void FChunkData2D::Serialize( FArchive& Ar )
{
	// Serialize metadata about the memory first
	Ar << SurfaceResolutionXY;
	Ar << DataElementSize;
	Ar << bAllowInterpolation;

	// Sanity check data in case we have loaded it
	check( SurfaceResolutionXY >= 0 );
	check( DataElementSize > 0 || ( DataElementSize == 0 && SurfaceResolutionXY == 0 ) );

	// If we are about to load data, allocate the memory to fit it
	const int32 TotalDataSize = SurfaceResolutionXY * SurfaceResolutionXY * DataElementSize;
	if ( SurfaceResolutionXY > 0 )
	{
		SurfaceDataPtr = FMemory::Realloc( SurfaceDataPtr, TotalDataSize );
	}

	// Load/save the raw data into the archive
	check( SurfaceDataPtr || TotalDataSize == 0 );
	if ( TotalDataSize > 0 )
	{
		Ar.Serialize( SurfaceDataPtr, TotalDataSize );
	}
}
