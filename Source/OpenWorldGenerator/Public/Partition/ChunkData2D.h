// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChunkCoord.h"
#include "VectorUtil.h"

// Whenever chunk surface data functions should check for out of bounds writes and data element size
#define SAFE_CHUNK_SURFACE_DATA !(UE_BUILD_TEST || UE_BUILD_SHIPPING) || WITH_EDITOR

/** IDs of the common chunk data */
namespace ChunkDataID
{
	/** float heightmap of the chunk surface */
	extern OPENWORLDGENERATOR_API const FName SurfaceHeightmap;
	/** FVector3f - contains normal for each heightmap point. NORMALIZED */
	extern OPENWORLDGENERATOR_API const FName SurfaceNormal;
	/** FVector2f - contains gradient vector for each heightmap point. Normalized. */
	extern OPENWORLDGENERATOR_API const FName SurfaceGradient;
	/** FVector2f - contains normalized measurement of surface steepness at the given point. */
	extern OPENWORLDGENERATOR_API const FName SurfaceSteepness;
	/** FChunkLandscapeWeight - contains information about the landscape layers present in the chunk. Resolution of this grid is different from the resolution of the heightmap. */
	extern OPENWORLDGENERATOR_API const FName SurfaceWeights;
	/** FBiomePaletteIndex - contains index of the biome in the chunk's biome palette that each point has */
	extern OPENWORLDGENERATOR_API const FName BiomeMap;
};

namespace ChunkDataInternal
{
	// Helpers for normalization of elements for interpolation. When interpolating normals, intermediate interpolation results need to be normalized.
	template<typename T> FORCEINLINE T GetSafeNormal( const T& InElement ) { return InElement; }
	template<typename T> FORCEINLINE UE::Math::TVector4<T> GetSafeNormal( const UE::Math::TVector4<T>& InElement ) { return InElement.GetSafeNormal(); }
	template<typename T> FORCEINLINE UE::Math::TVector<T> GetSafeNormal( const UE::Math::TVector<T>& InElement ) { return InElement.GetSafeNormal(); }
	template<typename T> FORCEINLINE UE::Math::TVector2<T> GetSafeNormal( const UE::Math::TVector2<T>& InElement ) { return InElement.GetSafeNormal(); }
}

/** A data container for some kind of data about of chunk stored in a 2-dimensional array */
class OPENWORLDGENERATOR_API FChunkData2D
{
	void* SurfaceDataPtr{nullptr};
	int32 DataElementSize{0};	
	int32 SurfaceResolutionXY{0};
	bool bAllowInterpolation{true};
public:
	explicit FChunkData2D();
	FChunkData2D( int32 InSurfaceResolutionXY, int32 InDataElementSize, bool InAllowInterpolation );
	~FChunkData2D();

	FChunkData2D( const FChunkData2D& InOther );
	FChunkData2D( FChunkData2D&& InOther ) noexcept;

	FChunkData2D& operator=( const FChunkData2D& InOther );
	FChunkData2D& operator=( FChunkData2D&& InOther ) noexcept;

	template<typename T>
	static FChunkData2D Create( int32 InSurfaceResolutionXY, bool bAllowInterpolation = true )
	{
		static_assert( TIsPODType<T>::Value, "FChunkSurfaceData only supports POD types" );
		return FChunkData2D( InSurfaceResolutionXY, sizeof(T), bAllowInterpolation );
	}

	FORCEINLINE bool IsEmpty() const { return SurfaceResolutionXY == 0; }
	FORCEINLINE int32 GetSurfaceResolutionXY() const { return SurfaceResolutionXY; }
	FORCEINLINE int32 GetSurfaceElementCount() const { return FMath::Square( SurfaceResolutionXY ); }
	FORCEINLINE int32 GetDataElementSize() const { return DataElementSize; }

	FORCEINLINE const void* GetRawDataPtr() const { return SurfaceDataPtr; }
	FORCEINLINE void* GetRawMutableDataPtr() { return SurfaceDataPtr; }

	FORCEINLINE void* GetRawElementAt( int32 InPosX, int32 InPosY )
	{
#if SAFE_CHUNK_SURFACE_DATA
		check( InPosX >= 0 && InPosX < SurfaceResolutionXY );
		check( InPosY >= 0 && InPosY < SurfaceResolutionXY );
#endif

		const int32 ElementOffset = InPosY * SurfaceResolutionXY + InPosX;
		return static_cast<uint8*>( SurfaceDataPtr ) + ElementOffset * DataElementSize;
	}

	FORCEINLINE const void* GetRawElementAt( int32 InPosX, int32 InPosY ) const
	{
#if SAFE_CHUNK_SURFACE_DATA
		check( InPosX >= 0 && InPosX < SurfaceResolutionXY );
		check( InPosY >= 0 && InPosY < SurfaceResolutionXY );
#endif

		const int32 ElementOffset = InPosY * SurfaceResolutionXY + InPosX;
		return static_cast<const uint8*>( SurfaceDataPtr ) + ElementOffset * DataElementSize;
	}

	/** Automatically casts data ptr to the provided type, ensuring the element size matches */
	template<typename T>
	FORCEINLINE T* GetMutableDataPtr()
	{
		static_assert( TIsPODType<T>::Value, "FChunkSurfaceData only supports POD types" );
#if SAFE_CHUNK_SURFACE_DATA
		checkf( SurfaceDataPtr == nullptr || DataElementSize == sizeof(T), TEXT("FChunkSurfaceData used with invalid DataElementSize=%d sizeof(T)=%d"), DataElementSize, sizeof(T) );
#endif
	
		return static_cast<T*>( SurfaceDataPtr );
	}

	/** Automatically casts data ptr to the provided type, ensuring the element size matches */
	template<typename T>
	FORCEINLINE const T* GetDataPtr() const
	{
		static_assert( TIsPODType<T>::Value, "FChunkSurfaceData only supports POD types" );
#if SAFE_CHUNK_SURFACE_DATA
		checkf( SurfaceDataPtr == nullptr || DataElementSize == sizeof(T), TEXT("FChunkSurfaceData used with invalid DataElementSize=%d sizeof(T)=%d"), DataElementSize, sizeof(T) );
#endif
	
		return static_cast<const T*>( SurfaceDataPtr );
	}

	/** Returns the element at the given position by value */
	template<typename T>
	FORCEINLINE T GetElementAt( int32 InPosX, int32 InPosY ) const
	{
		static_assert( TIsPODType<T>::Value, "FChunkSurfaceData only supports POD types" );
#if SAFE_CHUNK_SURFACE_DATA
		checkf( SurfaceDataPtr == nullptr || DataElementSize == sizeof(T), TEXT("FChunkSurfaceData used with invalid DataElementSize=%d sizeof(T)=%d"), DataElementSize, sizeof(T) );
#endif
	
		return *static_cast<const T*>( GetRawElementAt( InPosX, InPosY ) );
	}

	/** Returns the closest element at the given position. Deltas are expected to be positive in a [0;1] range */
	FORCEINLINE const void* GetRawClosestElementAt( int32 InPosX, int32 InPosY, float InFractionX, float InFractionY ) const
	{
		if ( InFractionX <= 0.5f )
		{
			return InFractionY <= 0.5f ? GetRawElementAt( InPosX, InPosY ) : GetRawElementAt( InPosX, InPosY + 1 );
		}
		else
		{
			return InFractionY <= 0.5f ? GetRawElementAt( InPosX + 1, InPosY ) : GetRawElementAt( InPosX + 1, InPosY + 1 );
		}
	}

	/** Returns the closest element at the given position. Deltas are expected to be positive in a [0;1] range */
	template<typename T>
	FORCEINLINE T GetClosestElementAt( int32 InPosX, int32 InPosY, float InFractionX, float InFractionY ) const
	{
		static_assert( TIsPODType<T>::Value, "FChunkSurfaceData only supports POD types" );
#if SAFE_CHUNK_SURFACE_DATA
		checkf( SurfaceDataPtr == nullptr || DataElementSize == sizeof(T), TEXT("FChunkSurfaceData used with invalid DataElementSize=%d sizeof(T)=%d"), DataElementSize, sizeof(T) );
#endif
		return *static_cast<const T*>( GetRawClosestElementAt( InPosX, InPosY, InFractionX, InFractionY ) );
	}
	
	/** Returns the closest element at the uniform position */
	template<typename T>
	T GetClosestElementAt( const FVector2f& NormalizedPosition ) const
	{
		const FVector2D GridPosition( NormalizedPosition.X * ( SurfaceResolutionXY - 1 ), NormalizedPosition.Y * ( SurfaceResolutionXY - 1 ) );
		return GetClosestElementAt<T>( FMath::TruncToInt32( GridPosition.X ), FMath::TruncToInt32( GridPosition.Y ), FMath::Frac( GridPosition.X ), FMath::Frac( GridPosition.Y ) );
	}

	/** Snaps the given world location to this grid */
	FORCEINLINE FVector SnapToGrid( const FVector& WorldLocation ) const
	{
		constexpr float HalfChunkWorldSize = FChunkCoord::ChunkSizeWorldUnits / 2.0f;
		const float GridSizeWorldUnits = FChunkCoord::ChunkSizeWorldUnits / (SurfaceResolutionXY - 1);
		return FVector(
			FMath::GridSnap( WorldLocation.X - HalfChunkWorldSize, GridSizeWorldUnits ) + HalfChunkWorldSize,
			FMath::GridSnap( WorldLocation.Y - HalfChunkWorldSize, GridSizeWorldUnits ) + HalfChunkWorldSize,
			WorldLocation.Z
		);
	}

	/** Converts the position of the point on the grid to the chunk relative world location */
	FORCEINLINE FVector PointToChunkLocalPosition( int32 InPosX, int32 InPosY, float PointHeight ) const
	{
		return FVector(
			FMath::Clamp( InPosX, 0, SurfaceResolutionXY - 1 ) * FChunkCoord::ChunkSizeWorldUnits / ( SurfaceResolutionXY - 1 ) - FChunkCoord::ChunkSizeWorldUnits / 2.0f,
			FMath::Clamp( InPosY, 0, SurfaceResolutionXY - 1 ) * FChunkCoord::ChunkSizeWorldUnits / ( SurfaceResolutionXY - 1 ) - FChunkCoord::ChunkSizeWorldUnits / 2.0f,
			PointHeight );
	}

	/** Converts the chunk relative world location to the closest point coordinates on this grid */
	FORCEINLINE FIntVector2 ChunkLocalPositionToPoint( const FVector& ChunkLocalPosition ) const
	{
		const FVector2D NormalizedPoint(
			FMath::Clamp( 0.5f + ChunkLocalPosition.X / FChunkCoord::ChunkSizeWorldUnits, 0.0f, 1.0f ),
			FMath::Clamp( 0.5f + ChunkLocalPosition.Y / FChunkCoord::ChunkSizeWorldUnits, 0.0f, 1.0f ) );
		
		return FIntVector2( FMath::RoundToInt32( NormalizedPoint.X * ( SurfaceResolutionXY - 1 ) ), FMath::RoundToInt32( NormalizedPoint.Y * ( SurfaceResolutionXY - 1 ) ) );
	}

	/** Converts the chunk relative world location to the closest point coordinates on this grid */
	FORCEINLINE FIntVector2 ChunkLocalPositionToPoint( const FVector2f& ChunkLocalPosition ) const
	{
		const FVector2D NormalizedPoint(
			FMath::Clamp( 0.5f + ChunkLocalPosition.X / FChunkCoord::ChunkSizeWorldUnits, 0.0f, 1.0f ),
			FMath::Clamp( 0.5f + ChunkLocalPosition.Y / FChunkCoord::ChunkSizeWorldUnits, 0.0f, 1.0f ) );
		
		return FIntVector2( FMath::RoundToInt32( NormalizedPoint.X * ( SurfaceResolutionXY - 1 ) ), FMath::RoundToInt32( NormalizedPoint.Y * ( SurfaceResolutionXY - 1 ) ) );
	}

	/** Converts the chunk relative world location to the closest point with the minimal coordinates and the fractional parts describing how far away the local position was moving in positive direction */
	FORCEINLINE FIntVector2 ChunkLocalPositionToPointFractional( const FVector& ChunkLocalPosition, FVector2D& OutFractionXY ) const
	{
		const FVector2D NormalizedPoint(
			FMath::Clamp( 0.5f + ChunkLocalPosition.X / FChunkCoord::ChunkSizeWorldUnits, 0.0f, 1.0f ),
			FMath::Clamp( 0.5f + ChunkLocalPosition.Y / FChunkCoord::ChunkSizeWorldUnits, 0.0f, 1.0f ) );
		
		const FVector2D SurfaceLocalPoint( NormalizedPoint.X * ( SurfaceResolutionXY - 1 ), NormalizedPoint.Y * ( SurfaceResolutionXY - 1 ) );

		const FIntVector2 SurfaceQuadIndex( FMath::FloorToInt( SurfaceLocalPoint.X ), FMath::FloorToInt( SurfaceLocalPoint.Y ) );
		OutFractionXY = FVector2D( SurfaceLocalPoint.X - SurfaceQuadIndex.X, SurfaceLocalPoint.Y - SurfaceQuadIndex.Y );
		return SurfaceQuadIndex;
	}

	FORCEINLINE static FVector2f ChunkLocalPositionToNormalized( const FVector& ChunkLocalPosition )
	{
		return FVector2f(
			FMath::Clamp( 0.5f + ChunkLocalPosition.X / FChunkCoord::ChunkSizeWorldUnits, 0.0f, 1.0f ),
			FMath::Clamp( 0.5f + ChunkLocalPosition.Y / FChunkCoord::ChunkSizeWorldUnits, 0.0f, 1.0f ) );
	}

	/** Calculates a normal of the specific normal by averaging the normals of it's surrounding planes. Note that this distributes weights uniformly across the adjacent faces. */
	template<typename T>
	FORCENOINLINE UE::Math::TVector<T> CalculatePointNormal( int32 InPosX, int32 InPosY ) const
	{
		// TODO @open-world-generator: We might want to weight by area and angle here, to match the generated mesh as closely as possible.
		const float PointSizeWorldUnits = 1.0f / SurfaceResolutionXY * FChunkCoord::ChunkSizeWorldUnits;
		const UE::Math::TVector<T> PosX0Y0( 0.0f, 0.0f, GetElementAt<T>( InPosX, InPosY ) );

		// Order of the points matters! We want them clockwise to get the +Z normal
		UE::Math::TVector<T> ResultNormal = UE::Math::TVector<T>::Zero();
		if ( InPosX > 0 && InPosY > 0 )
		{
			const UE::Math::TVector<T> PosXNY0( -PointSizeWorldUnits, 0.0f, GetElementAt<T>( InPosX - 1, InPosY ) );
			const UE::Math::TVector<T> PosX0YN( 0.0f, -PointSizeWorldUnits, GetElementAt<T>( InPosX, InPosY - 1 ) );

			ResultNormal += UE::Geometry::VectorUtil::Normal( PosX0Y0, PosX0YN, PosXNY0 );
		}
		if ( InPosX + 1 < SurfaceResolutionXY && InPosY + 1 < SurfaceResolutionXY )
		{
			const UE::Math::TVector<T> PosXPY0( PointSizeWorldUnits, 0.0f, GetElementAt<T>( InPosX + 1, InPosY ) );
			const UE::Math::TVector<T> PosX0YP( 0.0f, PointSizeWorldUnits, GetElementAt<T>( InPosX, InPosY + 1 ) );

			ResultNormal += UE::Geometry::VectorUtil::Normal( PosX0Y0, PosX0YP, PosXPY0 );
		}
		if ( InPosX > 0 && InPosY + 1 < SurfaceResolutionXY )
		{
			const UE::Math::TVector<T> PosXNY0( -PointSizeWorldUnits, 0.0f, GetElementAt<T>( InPosX - 1, InPosY ) );
			const UE::Math::TVector<T> PosX0YP( 0.0f, PointSizeWorldUnits, GetElementAt<T>( InPosX, InPosY + 1 ) );

			ResultNormal += UE::Geometry::VectorUtil::Normal( PosX0Y0, PosXNY0, PosX0YP );
		}
		if ( InPosX + 1 < SurfaceResolutionXY && InPosY > 0 )
		{
			const UE::Math::TVector<T> PosXPY0( PointSizeWorldUnits, 0.0f, GetElementAt<T>( InPosX + 1, InPosY ) );
			const UE::Math::TVector<T> PosX0YN( 0.0f, -PointSizeWorldUnits, GetElementAt<T>( InPosX, InPosY - 1 ) );

			ResultNormal += UE::Geometry::VectorUtil::Normal( PosX0Y0, PosXPY0, PosX0YN );
		}
		return ResultNormal.GetSafeNormal();
	}

	/** Returns the interpolated value between the adjacent 4 points forming a quad. The type in question must support Lerp */
	template<typename T>
	T GetInterpolatedElementAt( int32 InPosX, int32 InPosY, float InFractionX, float InFractionY ) const
	{
		// Exit early and return closest element if interpolation is not allowed for this data
		if ( !bAllowInterpolation )
		{
			return GetClosestElementAt<T>( InPosX, InPosY, InFractionX, InFractionY );
		}

		// Special case - Last column. That implies DeltaX == 0, which we can remap to pre-last column with DeltaX == 1
		if ( InPosX == SurfaceResolutionXY - 1 )
		{
			InPosX = InPosX - 1;
			InFractionX = 1.0f;
		}
		// Special case - Last row. That implies DeltaY == 0, which we can remap to pre-last row with DeltaY == 1
		if ( InPosY == SurfaceResolutionXY - 1 )
		{
			InPosY = InPosY - 1;
			InFractionY = 1.0f;
		}

		// Interpolate the data between the adjacent points
		const T DataX0Y0 = GetElementAt<T>( InPosX, InPosY );
		const T DataX1Y0 = GetElementAt<T>( InPosX + 1, InPosY );
		const T DataX0Y1 = GetElementAt<T>( InPosX, InPosY + 1 );
		const T DataX1Y1 = GetElementAt<T>( InPosX + 1, InPosY + 1 );

		const T LerpDataY0 = ChunkDataInternal::GetSafeNormal( FMath::Lerp( DataX0Y0, DataX1Y0, InFractionX ) );
		const T LerpDataY1 = ChunkDataInternal::GetSafeNormal( FMath::Lerp( DataX0Y1, DataX1Y1, InFractionX ) );

		return ChunkDataInternal::GetSafeNormal( FMath::Lerp( LerpDataY0, LerpDataY1, InFractionY ) );
	}

	/** Returns interpolated value between the adjacent points, using the normalized coordinate in [0;1] range. The type in question must support Lerp */
	template<typename T>
	T GetInterpolatedElementAt( const FVector2f& NormalizedPosition ) const
	{
		const FVector2D GridPosition( NormalizedPosition.X * ( SurfaceResolutionXY - 1 ), NormalizedPosition.Y * ( SurfaceResolutionXY - 1 ) );
		return GetInterpolatedElementAt<T>( FMath::TruncToInt32( GridPosition.X ), FMath::TruncToInt32( GridPosition.Y ), FMath::Frac( GridPosition.X ), FMath::Frac( GridPosition.Y ) );
	}

	/** Updates the element's value at the given position */
	template<typename T>
	FORCEINLINE void SetElementAt( int32 InPosX, int32 InPosY, const T NewElementValue )
	{
		static_assert( TIsPODType<T>::Value, "FChunkSurfaceData only supports POD types" );
#if SAFE_CHUNK_SURFACE_DATA
		checkf( SurfaceDataPtr == nullptr || DataElementSize == sizeof(T), TEXT("FChunkSurfaceData used with invalid DataElementSize=%d sizeof(T)=%d"), DataElementSize, sizeof(T) );
#endif
	
		*static_cast<T*>( GetRawElementAt( InPosX, InPosY ) ) = NewElementValue;
	}

	/** Serializes this chunk data into/out of the archive */
	void Serialize( FArchive& Ar );

	friend FArchive& operator<<( FArchive& Ar, FChunkData2D& ChunkData )
	{
		ChunkData.Serialize( Ar );
		return Ar;
	}
};
