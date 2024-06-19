// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/TransformCalculus2D.h"
#include "UObject/StructOnScope.h"
#include "TerraformingBrush.generated.h"

/** Settings for terraforming brush falloff calculation */
USTRUCT( BlueprintType )
struct OPENWORLDGENERATOR_API FTerraformingBrushFalloffSettings
{
	GENERATED_BODY()

	/** Determines when the falloff should start, starting from 0 where it is in the center of the shape and extending up to 1.0 which is the border of the shape */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Brush Falloff Settings" )
	float FalloffStart{1.0f};

	/** Exponent for the exponential falloff. Exponent of 1.0 makes the falloff linear. */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Brush Falloff Settings" )
	float FalloffExponent{1.0f};

	/** Chance to apply on top of the base random falloff chance with a coefficient of the current falloff distance (normalized) */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Brush Falloff Settings" )
	float RandomFalloffDistanceScale{0.0f};

	/** Chance of the random falloff to null out the distance at the specific point */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Brush Falloff Settings" )
	float RandomFalloffChance{0.0f};

	/** Seed for the random falloff effect. Used to seed the random stream generator */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Brush Falloff Settings" )
	int32 RandomFalloffSeed{0};
};

/** Helper class for applying falloff settings */
class OPENWORLDGENERATOR_API FTerraformingBrushFalloffHelper
{
	const FTerraformingBrushFalloffSettings* Settings{nullptr};
	FRandomStream RandomFalloffStream;
public:
	explicit FTerraformingBrushFalloffHelper( const FTerraformingBrushFalloffSettings& InSettings );

	float Apply( float InDistance, float InWeight ) const;
};

/** Describes one of two possible ways to define the precision of the terraforming area - either by specifying the amount of points or by specifying the size of a single point in world units */
USTRUCT( BlueprintType )
struct OPENWORLDGENERATOR_API FTerraformingPrecision
{
	GENERATED_BODY()

	static FTerraformingPrecision DynamicGrid( float GridResolution )
	{
		FTerraformingPrecision Result;
		Result.bIsFixedGridResolution = false;
		Result.GridResolution = GridResolution;
		return Result;
	}

	static FTerraformingPrecision FixedGrid( int32 GridWidth, int32 GridHeight )
	{
		FTerraformingPrecision Result;
		Result.bIsFixedGridResolution = true;
		Result.GridWidth = GridWidth;
		Result.GridHeight = GridHeight;
		return Result;
	}

	/** Calculates the grid size from the brush extents for this precision */
	FIntPoint CalculateGridSize( const FVector2f& BrushExtents ) const;
	
	/** True if this is a fixed grid resolution. If that is the case, GridWidth and GridHeight are used */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Terraforming Precision" )
	bool bIsFixedGridResolution{false};

	/** Width of the grid, if this is fixed grid resolution */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Terraforming Precision", meta = ( EditConditionHides, EditCondition = "bIsFixedGridResolution" ) )
	int32 GridWidth{100};

	/** Height of the grid, if this is fixed grid resolution */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Terraforming Precision", meta = ( EditConditionHides, EditCondition = "bIsFixedGridResolution" ) )
	int32 GridHeight{100};

	/** Size of the point in world space, if this is not a fixed grid */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Terraforming Precision", meta = ( EditConditionHides, EditCondition = "!bIsFixedGridResolution" ) )
	float GridResolution{100.0f};
};

/** Base class for terraforming brushes - various shapes used for modifying terrain in various weights (for example, painting heights or weights) */
USTRUCT( BlueprintType )
struct OPENWORLDGENERATOR_API FTerraformingBrush
{
	GENERATED_BODY()

	virtual ~FTerraformingBrush() = default;

	/** Returns the extents of the brush, in world space, without any transformations applied. Extents are half size of the area covered by the brush */
	virtual FVector2f GetRawExtents() const { return FVector2f::ZeroVector; }
	/** Renders the brush to the given grid, using the provided grid to world transform to convert grid coordinates to the local, non-transformed coordinates for the brush */
	virtual bool RenderBrush( const FTransform2f& GridToLocal, int32 GridWidth, int32 GridHeight, TArray<float>& OutWeights ) const { return false; }

	/**
	 * Renders this brush to the grid of the given size, applying rotation and scale, and returns the weights on the grid, in addition to the brush size and grid to world transform
	 *
	 * @param GridOrigin offset from the local center (0,0) to the grid origin. The resulting grid will be aligned to this origin, and not to the local center
	 * @param GridPrecision precision of the grid to sample this shape.
	 * @param OutGridSize output, amount of grid points across the X and Y axis. Useful when grid does not have a fixed precision.
	 * @param OutWeights output, the weights of the points on the grid
	 * @param OutWorldExtents output, the extents of the grid, in world space (centered at origin)
	 * @param OutGridToWorld output, the transform to map grid point coordinates to world space point locations (centered at origin)
	 */
	void RenderBrushToGrid( const FVector2f& GridOrigin, const FTerraformingPrecision& GridPrecision, FIntPoint& OutGridSize, TArray<float>& OutWeights, FVector2f& OutWorldExtents, FTransform2f& OutGridToWorld ) const;

	/** Returns the full extents of the brush in world, with all of the transformations applied */
	FVector2f GetBrushExtents() const;
	
	void RenderBrushToSizedGrid( const FVector2f& Origin, float GridOriginOffset, float GridCellSize, FIntPoint& OutGridPosXY, FIntVector2& OutGridSizeXY, TArray<float>& OutWeights, FBox2f* OutBrushBounds = nullptr ) const;

	/** Rotation of this brush, in degrees */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Brush" )
	float Rotation{0.0f};

	/** Scale of this brush, in degrees */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Brush" )
	float Scale{1.0f};
};

/** Type to wrap polymorphic brushes and pass them as function parameters */
USTRUCT( BlueprintType )
struct OPENWORLDGENERATOR_API FPolymorphicTerraformingBrush
{
	GENERATED_BODY()

	FPolymorphicTerraformingBrush();
	FPolymorphicTerraformingBrush( const FPolymorphicTerraformingBrush& InOther );

	FPolymorphicTerraformingBrush& operator=( const FPolymorphicTerraformingBrush& InOther );

	/** Explicit constructor to copy a non-polymorphic brush into this polymorphic brush */
	template<typename T>
	explicit FPolymorphicTerraformingBrush( const T& InBrush )  : InnerBrush( new TStructOnScope<FTerraformingBrush>{} )
	{
		InnerBrush->InitializeAs<T>( InBrush );
	}

	/** Type traits */
	void AddStructReferencedObjects(FReferenceCollector& Collector) const;
	bool Identical(const FPolymorphicTerraformingBrush* Other, uint32 PortFlags) const;
	bool Serialize(FArchive& Ar);

	const FTerraformingBrush* operator->() const;
private:
	/** Inner brush of the polymorphic type that this struct is wrapping */
	TUniquePtr<TStructOnScope<FTerraformingBrush>> InnerBrush;
};

template<>
struct TStructOpsTypeTraits<FPolymorphicTerraformingBrush> : TStructOpsTypeTraitsBase2<FPolymorphicTerraformingBrush>
{
	enum
	{
		WithSerializer = true,
		WithIdentical = true,
		WithAddStructReferencedObjects = true,
	};
};

/** A box centered at the origin with the given extents */
USTRUCT( BlueprintType )
struct OPENWORLDGENERATOR_API FBoxTerraformingBrush : public FTerraformingBrush
{
	GENERATED_BODY()

	FBoxTerraformingBrush() = default;
	explicit FBoxTerraformingBrush( const FVector2f& InExtents ) : Extents( InExtents ) {}

	// Begin FTerraformingBrush interface
	virtual FVector2f GetRawExtents() const override;
	virtual bool RenderBrush(const FTransform2f& GridToLocal, int32 GridWidth, int32 GridHeight, TArray<float>& OutWeights) const override;
	// End FTerraformingBrush interface

	/** Extents of the box this brush represents */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Box Brush" )
	FVector2f Extents{ForceInit};

	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Box Brush" )
	FTerraformingBrushFalloffSettings FalloffSettings;
};

/** An ellipse centered at the origin with the given extents */
USTRUCT( BlueprintType )
struct OPENWORLDGENERATOR_API FEllipseTerraformingBrush : public FTerraformingBrush
{
	GENERATED_BODY()

	// Begin FTerraformingBrush interface
	virtual FVector2f GetRawExtents() const override;
	virtual bool RenderBrush(const FTransform2f& GridToLocal, int32 GridWidth, int32 GridHeight, TArray<float>& OutWeights) const override;
	// End FTerraformingBrush interface

	/** Extents of the ellipse this brush represents */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Ellipse Brush" )
	FVector2f Extents{ForceInit};

	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Ellipse Brush" )
	FTerraformingBrushFalloffSettings FalloffSettings;
};

/** A terraforming brush that is formed by an intersection or overlap of multiple independent sub-brushes */
/*USTRUCT( BlueprintType )
struct OPENWORLDGENERATOR_API FComplexTerraformingBrush : public FTerraformingBrush
{
	GENERATED_BODY()

	// Begin FTerraformingBrush interface
	virtual FVector2f GetRawExtents() const override;
	virtual bool RenderBrush(const FTransform2f& GridToLocal, int32 GridWidth, int32 GridHeight, TArray<float>& OutWeights) const override;
	// End FTerraformingBrush interface
};*/
