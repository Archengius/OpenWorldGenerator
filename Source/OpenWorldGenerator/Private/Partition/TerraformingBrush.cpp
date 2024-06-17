// Copyright Nikita Zolotukhin. All Rights Reserved.

#include "Partition/TerraformingBrush.h"

FTerraformingBrushFalloffHelper::FTerraformingBrushFalloffHelper( const FTerraformingBrushFalloffSettings& InSettings ) : Settings( &InSettings )
{
	RandomFalloffStream = FRandomStream{ InSettings.RandomFalloffSeed };
}

float FTerraformingBrushFalloffHelper::Apply( float InDistance, float InWeight ) const
{
	// No falloff until this distance
	if ( InDistance <= Settings->FalloffStart )
	{
		return InWeight;
	}

	// Apply exponential falloff
	const float NormalizedDistance = 1.0f - ( InDistance - Settings->FalloffStart ) / ( 1.0f - Settings->FalloffStart );
	InWeight = FMath::Pow( NormalizedDistance, Settings->FalloffExponent );

	// Apply random falloff if it is enabled
	const float FalloffChance = Settings->RandomFalloffChance + Settings->RandomFalloffDistanceScale * NormalizedDistance;
	if ( FalloffChance > 0.0f && RandomFalloffStream.GetFraction() <= FalloffChance )
	{
		InWeight = 0.0f;
	}
	return InWeight;
}

FPolymorphicTerraformingBrush::FPolymorphicTerraformingBrush() : InnerBrush( new TStructOnScope<FTerraformingBrush>{} )
{
	// Initialize with the empty brush by default to avoid having invalid brush
	InnerBrush->InitializeAs<FTerraformingBrush>();
}

FPolymorphicTerraformingBrush::FPolymorphicTerraformingBrush( const FPolymorphicTerraformingBrush& InOther ) : InnerBrush( new TStructOnScope<FTerraformingBrush>{} )
{
	InnerBrush->InitializeFrom( *InOther.InnerBrush );
}

FPolymorphicTerraformingBrush& FPolymorphicTerraformingBrush::operator=( const FPolymorphicTerraformingBrush& InOther )
{
	InnerBrush->InitializeFrom( *InOther.InnerBrush );
	return *this;
}

void FPolymorphicTerraformingBrush::AddStructReferencedObjects( FReferenceCollector& Collector ) const
{
	if ( InnerBrush->IsValid() )
	{
		Collector.AddPropertyReferencesWithStructARO( CastChecked<UScriptStruct>( InnerBrush->GetStruct() ), InnerBrush.Get() );
	}
}

bool FPolymorphicTerraformingBrush::Identical( const FPolymorphicTerraformingBrush* Other, uint32 PortFlags ) const
{
	if ( InnerBrush->IsValid() != Other->InnerBrush->IsValid() )
	{
		return false;
	}
	if ( InnerBrush->IsValid() )
	{
	 	const UScriptStruct* ScriptStruct = CastChecked<UScriptStruct>( InnerBrush->GetStruct() );
	 	return ScriptStruct->CompareScriptStruct( InnerBrush.Get(), Other->InnerBrush.Get(), PortFlags );
	}
	return true;
}

bool FPolymorphicTerraformingBrush::Serialize( FArchive& Ar )
{
	InnerBrush->Serialize( Ar );
	return true;
}

const FTerraformingBrush* FPolymorphicTerraformingBrush::operator->() const
{
	if ( InnerBrush->IsValid() )
	{
	 	return InnerBrush->Get();
	}
	// Return empty shared brush in case this struct somehow ended up in an invalid state
	static const FTerraformingBrush EmptyBrush{};
	return &EmptyBrush;
}

FIntPoint FTerraformingPrecision::CalculateGridSize( const FVector2f& BrushExtents ) const
{
	if ( bIsFixedGridResolution || FMath::IsNearlyZero( GridResolution ) )
	{
		return FIntPoint( GridWidth, GridHeight );
	}
	return FIntPoint( FMath::CeilToInt32( BrushExtents.X / GridResolution ), FMath::CeilToInt32( BrushExtents.Y / GridResolution ) );
}

FVector2f FTerraformingBrush::GetBrushExtents() const
{
	const FTransform2f BrushTransform = FTransform2f( FMatrix2x2f( FQuat2f( FMath::DegreesToRadians( Rotation ) ) ).Concatenate( FMatrix2x2f( Scale ) ) );

	// We take the largest extent across the X/Y axis because extents per axis cannot be trusted when rotation is involved
	return FVector2f( BrushTransform.TransformVector( GetRawExtents() ).GetAbsMax() );
}

void FTerraformingBrush::RenderBrushToSizedGrid( const FVector2f& Origin, float GridOriginOffset, float GridCellSize, FIntPoint& OutGridPosXY, FIntVector2& OutGridSizeXY, TArray<float>& OutWeights, FBox2f* OutBrushBounds ) const
{
	const FTransform2f BrushTransform = FTransform2f( FMatrix2x2f( FQuat2f( FMath::DegreesToRadians( Rotation ) ) ).Concatenate( FMatrix2x2f( Scale ) ) );

	// We take the largest extent across the X/Y axis because extents per axis cannot be trusted when rotation is involved
	const FVector2f BrushExtents = FVector2f( BrushTransform.TransformVector( GetRawExtents() ).GetAbsMax() );
	const FBox2f BrushBounds( Origin - BrushExtents, Origin + BrushExtents );

	const int32 GridStartX = FMath::FloorToInt32( ( BrushBounds.Min.X + GridOriginOffset ) / GridCellSize );
	const int32 GridStartY = FMath::FloorToInt32( ( BrushBounds.Min.Y + GridOriginOffset ) / GridCellSize );

	const int32 GridEndX = FMath::CeilToInt32( ( BrushBounds.Max.X + GridOriginOffset ) / GridCellSize );
	const int32 GridEndY = FMath::CeilToInt32( ( BrushBounds.Max.Y + GridOriginOffset ) / GridCellSize );

	OutGridPosXY = FIntPoint( GridStartX, GridStartY );
	OutGridSizeXY = FIntVector2( GridEndX - GridStartX + 1, GridEndY - GridStartY + 1 );
	OutWeights.SetNumZeroed( OutGridSizeXY.X * OutGridSizeXY.Y );
	if ( OutBrushBounds )
	{
		*OutBrushBounds = BrushBounds;
	} 

	// Scale and translate from grid size to world units and offset it by grid start in world units, and then by grid origin in world space.
	// Then, translate to the local origin by subtracting it from the world origin
	// Then apply the inverse of brush transform to translate it from brush extents to original brush coordinates that are unscaled and un-rotated
	const FTransform2f InverseBrushGridTransform = FTransform2f( FScale2f( GridCellSize ), FVector2f( GridStartX * GridCellSize, GridStartY * GridCellSize ) - FVector2f( GridOriginOffset ) - Origin )
		.Concatenate( BrushTransform.Inverse() );

	RenderBrush( InverseBrushGridTransform, OutGridSizeXY.X, OutGridSizeXY.Y, OutWeights );
}

void FTerraformingBrush::RenderBrushToGrid( const FVector2f& GridOrigin, const FTerraformingPrecision& GridPrecision, FIntPoint& OutGridSize, TArray<float>& OutWeights, FVector2f& OutWorldExtents, FTransform2f& OutGridToWorld ) const
{
	// Calculate scale and rotation transforms for the brush
	const FTransform2f BrushLocalToBrushRotated( FQuat2f( FMath::DegreesToRadians( Rotation ) ) );
	const FTransform2f BrushLocalToBrushScaledRotated( BrushLocalToBrushRotated.Concatenate( FTransform2f( Scale ) ) );

	// Determine the size of the in-world bounding box, taking the grid origin, brush extents and the scale/rotation into account
	const FVector2f BrushLocalExtents = GetRawExtents();
	FVector2f BoxPoints[] {
		BrushLocalToBrushScaledRotated.TransformPoint( -BrushLocalExtents ),
		BrushLocalToBrushScaledRotated.TransformPoint( BrushLocalExtents ),
		BrushLocalToBrushScaledRotated.TransformPoint( GridOrigin - BrushLocalExtents ),
		BrushLocalToBrushScaledRotated.TransformPoint( GridOrigin + BrushLocalExtents )
	};
	const FVector2f GridWorldSpaceOrigin = BrushLocalToBrushScaledRotated.TransformPoint( GridOrigin );
	const FBox2f BrushBoundingBox( BoxPoints, UE_ARRAY_COUNT( BoxPoints ) );
	const FVector2f BrushWorldSpaceExtents = BrushBoundingBox.GetExtent();
	
	const FIntPoint GridSize = GridPrecision.CalculateGridSize( BrushWorldSpaceExtents );

	// Resulting transform transforms points from grid coordinates (0 to NumPoints) to world space grid origin (GridOrigin - BrushExtents to GridOrigin + BrushExtents)
	const FScale2f GridToWorldSpaceScale( BrushWorldSpaceExtents.X * 2.0f / ( GridSize.X - 1 ), BrushWorldSpaceExtents.Y * 2.0f / ( GridSize.Y - 1 ) );
	const FVector2f GridToOriginTranslation( GridWorldSpaceOrigin - BrushWorldSpaceExtents );
	const FTransform2f GridToBrushLocal( GridToWorldSpaceScale, GridToOriginTranslation );

	// Calculate weights of individual points. GridToBrushLocal transform should be centered at the origin (e.g. have no world space translation) but adjusted with scale and rotation
	// Additionally we should apply the rotation transform, but not apply the scale (it would result in shape appearing smaller/bigger on the grid, rather than scaling only the grid, which we do not want)
	TArray<float> BrushWeights;
	BrushWeights.AddZeroed( GridSize.X * GridSize.Y );
	RenderBrush( GridToBrushLocal.Concatenate( BrushLocalToBrushRotated ), GridSize.X, GridSize.Y, BrushWeights );

	// The resulting transform for the external code should both scale and rotate the grid to get real coordinates that correspond to the extents we return
	OutGridSize = GridSize;
	OutWeights = MoveTemp( BrushWeights );
	OutWorldExtents = BrushWorldSpaceExtents;
	OutGridToWorld = GridToBrushLocal.Concatenate( BrushLocalToBrushScaledRotated );
}

FVector2f FBoxTerraformingBrush::GetRawExtents() const
{
	return Extents;
}

bool FBoxTerraformingBrush::RenderBrush( const FTransform2f& GridToLocal, int32 GridWidth, int32 GridHeight, TArray<float>& OutWeights ) const
{
	// Exit early if we do not have valid extents
	if ( Extents.X <= 0.0f || Extents.Y <= 0.0f )
	{
		return false;
	}

	const FTerraformingBrushFalloffHelper FalloffHelper( FalloffSettings );

	// Map grid coords to local coords and do checks
	for ( int32 GridX = 0; GridX < GridWidth; GridX++ )
	{
		for ( int32 GridY = 0; GridY < GridHeight; GridY++ )
		{
			// Points inside of the box are below it's absolute extents on both axi
			const FVector2f LocalPos = GridToLocal.TransformPoint( FVector2f( GridX, GridY ) );
			if ( FMath::Abs( LocalPos.X ) <= Extents.X && FMath::Abs( LocalPos.Y ) <= Extents.Y )
			{
            	const float Distance = FMath::Max( FMath::Abs( LocalPos.X ) / Extents.X, FMath::Abs( LocalPos.Y ) / Extents.Y );
				OutWeights[ GridWidth * GridY + GridX ] = FalloffHelper.Apply( Distance, 1.0f );
			}
		}
	}
	return true;
}

FVector2f FEllipseTerraformingBrush::GetRawExtents() const
{
	return Extents;
}

bool FEllipseTerraformingBrush::RenderBrush( const FTransform2f& GridToLocal, int32 GridWidth, int32 GridHeight, TArray<float>& OutWeights ) const
{
	// Exit early if we do not have valid extents
	if ( Extents.X <= 0.0f || Extents.Y <= 0.0f )
	{
		return false;
	}

	const FTerraformingBrushFalloffHelper FalloffHelper( FalloffSettings );

	// Map grid coords to local coords and do checks
	for ( int32 GridX = 0; GridX < GridWidth; GridX++ )
	{
		for ( int32 GridY = 0; GridY < GridHeight; GridY++ )
		{
			// Points inside of the ellipse have the coefficient of 1 and below
			const FVector2f LocalPos = GridToLocal.TransformPoint( FVector2f( GridX, GridY ) );
			const float Coefficient = FMath::Square( LocalPos.X / Extents.X ) + FMath::Square( LocalPos.Y / Extents.Y );
			if ( Coefficient <= 1.0f )
			{
				OutWeights[ GridWidth * GridY + GridX ] = FalloffHelper.Apply( Coefficient, 1.0f );
			}
		}
	}
	return true;
}
