// Copyright Nikita Zolotukhin. All Rights Reserved.

#include "PCG/PCGChunkLandscapeData.h"

#include "Data/PCGPointData.h"
#include "Helpers/PCGBlueprintHelpers.h"
#include "Partition/ChunkCoord.h"
#include "Partition/OWGChunk.h"
#include "Rendering/OWGChunkLandscapeLayer.h"

void UPCGChunkLandscapeData::Initialize( const TSharedPtr<FCachedChunkLandscapeData>& InLandscapeData, const TSharedPtr<FCachedChunkBiomeData>& InBiomeData, bool InUseMetadata )
{
	check( InLandscapeData.IsValid() );

	LandscapeData = InLandscapeData;
	BiomeData = InBiomeData;
	bUseMetadata = InUseMetadata;

	// Create metadata for landscape layers
	for ( const UOWGChunkLandscapeLayer* LandscapeLayer : LandscapeData->WeightMapDescriptor.GetAllLayers() )
	{
		if ( LandscapeLayer && LandscapeLayer->PCGMetadataAttributeName != NAME_None )
		{
			Metadata->CreateAttribute<float>( LandscapeLayer->PCGMetadataAttributeName, 0.0f, true, false );
		}
	}
	// Create metadata for biomes
	if ( BiomeData.IsValid() )
	{
		for ( const UOWGBiome* Biome : BiomeData->BiomePalette.GetAllBiomes() )
		{
			if ( Biome && Biome->PCGMetadataAttributeName != NAME_None )
			{
				Metadata->CreateAttribute<bool>( Biome->PCGMetadataAttributeName, false, false, false );
			}
		}
	}
}

FBox UPCGChunkLandscapeData::GetBounds() const
{
	if ( LandscapeData )
	{
		const FVector ChunkExtents( FChunkCoord::ChunkSizeWorldUnits / 2.0f );
		const FVector MinPos = LandscapeData->ChunkToWorld.TransformPosition( -ChunkExtents );
		const FVector MaxPos = LandscapeData->ChunkToWorld.TransformPosition( ChunkExtents );

		// Z bounds are meaningless here because we are a surface
		return FBox( MinPos, MaxPos );
	}
	return FBox();
}

FBox UPCGChunkLandscapeData::GetStrictBounds() const
{
	// Chunks do not currently support having "holes", and as such strict bounds of the chunk are the same as normal bounds
	return GetBounds();
}

bool UPCGChunkLandscapeData::SamplePoint( const FTransform& InTransform, const FBox& Bounds, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata ) const
{
	// The point is in/on the shape if it coincides with its projection. I.e. projecting on landscape does not move the point. Implementing
	// this way shares the sampling code.
	if ( ProjectPoint(InTransform, Bounds, {}, OutPoint, OutMetadata) )
	{
		if ( Bounds.IsValid )
		{
			return FMath::PointBoxIntersection( OutPoint.Transform.GetLocation(), Bounds.TransformBy( InTransform ) );
		}
		return ( InTransform.GetLocation() - OutPoint.Transform.GetLocation() ).SquaredLength() < UE_SMALL_NUMBER;
	}
	return false;
}

bool UPCGChunkLandscapeData::ProjectPoint( const FTransform& InTransform, const FBox& InBounds, const FPCGProjectionParams& InParams, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata ) const
{
	if ( !LandscapeData.IsValid() )
	{
		return false;
	}

	const FChunkLandscapePointSampler PointSampler( LandscapeData.Get(), BiomeData.Get() );

	// Make sure the point is within the chunks bounds first
	if ( PointSampler.CheckPointInBounds( InTransform.GetLocation() ) )
	{
		return false;
	}

	// Quick path - if we are not using the metadata, only sample the point's transform
	if ( !bUseMetadata || !OutMetadata )
	{
		const FTransform PointTransform = PointSampler.SamplePointTransformInterpolated( InTransform.GetLocation() );
		const int32 Seed = UPCGBlueprintHelpers::ComputeSeedFromPosition( PointTransform.GetLocation() );
		constexpr float Density = 1;

		OutPoint = FPCGPoint( PointTransform, Density, Seed );
		OutPoint.SetExtents( PointSampler.GetPointExtents() );
	}
	// Slow path - sample the metadata in addition to the transform
	else
	{
		const FChunkLandscapePoint LandscapePoint = PointSampler.SamplePointInterpolated( InTransform.GetLocation() );

		const int32 Seed = UPCGBlueprintHelpers::ComputeSeedFromPosition( LandscapePoint.Transform.GetLocation() );
		constexpr float Density = 1;

		OutPoint = FPCGPoint( LandscapePoint.Transform, Density, Seed );
		OutPoint.SetExtents( PointSampler.GetPointExtents() );

		OutPoint.Steepness = LandscapePoint.Steepness;
		PopulatePointMetadata( OutPoint, LandscapePoint, OutMetadata );
	}

	// Respect projection settings
	if (!InParams.bProjectPositions)
	{
		OutPoint.Transform.SetLocation( InTransform.GetLocation() );
	}

	if (!InParams.bProjectRotations)
	{
		OutPoint.Transform.SetRotation( InTransform.GetRotation() );
	}
	else
	{
		// Take surface transform, but respect initial point yaw (don't spin points around Z axis).
		FVector RotVector = InTransform.GetRotation().ToRotationVector();
		RotVector.X = RotVector.Y = 0;
		OutPoint.Transform.SetRotation(OutPoint.Transform.GetRotation() * FQuat::MakeFromRotationVector(RotVector));
	}

	if (!InParams.bProjectScales)
	{
		OutPoint.Transform.SetScale3D(InTransform.GetScale3D());
	}
	return true;
}

const UPCGPointData* UPCGChunkLandscapeData::CreatePointData( FPCGContext* Context, const FBox& InBounds ) const
{
	if ( !LandscapeData )
	{
		return nullptr;
	}

	UPCGPointData* Data = NewObject<UPCGPointData>();
	Data->InitializeFromData( this );
	TArray<FPCGPoint>& Points = Data->GetMutablePoints();

	FBox EffectiveBounds = InBounds;
	if (InBounds.IsValid)
	{
		EffectiveBounds = InBounds.Overlap(InBounds);
	}

	// Early out
	if (!EffectiveBounds.IsValid)
	{
		return Data;
	}

	const FChunkLandscapePointSampler PointSampler( LandscapeData.Get(), BiomeData.Get() );
	PointSampler.ForEachPointGrid( EffectiveBounds, [&Points, &PointSampler, Data, this]( const FChunkLandscapePoint& LandscapePoint )
	{
		const int32 Seed = UPCGBlueprintHelpers::ComputeSeedFromPosition( LandscapePoint.Transform.GetLocation() );
		constexpr float Density = 1;

		FPCGPoint& OutPoint = Points.Emplace_GetRef( LandscapePoint.Transform, Density, Seed );
		OutPoint.SetExtents( PointSampler.GetPointExtents() );

		if ( bUseMetadata )
		{
			OutPoint.Steepness = LandscapePoint.Steepness;
			PopulatePointMetadata( OutPoint, LandscapePoint, Data->Metadata );
		}
		return true;
	} );
	return Data;
}

void UPCGChunkLandscapeData::PopulatePointMetadata( FPCGPoint& OutPoint, const FChunkLandscapePoint& ChunkPoint, UPCGMetadata* OutMetadata )
{
	OutPoint.MetadataEntry = OutMetadata->AddEntry();

	// Setup layer weights for the point
	for ( const TPair<UOWGChunkLandscapeLayer*, float>& LayerWeight : ChunkPoint.LayerWeights )
	{
		if ( LayerWeight.Key && LayerWeight.Key->PCGMetadataAttributeName != NAME_None )
		{
			if ( FPCGMetadataAttribute<float>* LayerMetadata = OutMetadata->GetMutableTypedAttribute<float>( LayerWeight.Key->PCGMetadataAttributeName ) )
			{
				LayerMetadata->SetValue( OutPoint.MetadataEntry, LayerWeight.Value );
			}
		}
	}
	
	// Setup biome value for the point
	if ( ChunkPoint.Biome && ChunkPoint.Biome->PCGMetadataAttributeName != NAME_None )
	{
		if ( FPCGMetadataAttribute<bool>* BiomeMetadata = OutMetadata->GetMutableTypedAttribute<bool>( ChunkPoint.Biome->PCGMetadataAttributeName ) )
		{
			BiomeMetadata->SetValue( OutPoint.MetadataEntry, true );
		}
	}
}

UPCGSpatialData* UPCGChunkLandscapeData::CopyInternal() const
{
	UPCGChunkLandscapeData* NewData = NewObject<UPCGChunkLandscapeData>();

	if ( LandscapeData.IsValid() )
	{
		NewData->Initialize( LandscapeData, BiomeData, bUseMetadata );
	}
	return NewData;
}

void UPCGChunkLandscapeData::AddReferencedObjects( UObject* InThis, FReferenceCollector& Collector )
{
	Super::AddReferencedObjects( InThis, Collector );

	const UPCGChunkLandscapeData* LandscapeData = CastChecked<UPCGChunkLandscapeData>( InThis );
	if ( LandscapeData->LandscapeData.IsValid() )
	{
		LandscapeData->LandscapeData->WeightMapDescriptor.AddReferencedObjects( Collector );
	}
	if ( LandscapeData->BiomeData.IsValid() )
	{
		LandscapeData->BiomeData->BiomePalette.AddReferencedObjects( Collector );
	}
}
