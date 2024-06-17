// Copyright Nikita Zolotukhin. All Rights Reserved.

#include "PCG/PCGFilterTargetBiomes.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "Data/PCGPointData.h"
#include "Generation/PCGChunkGenerator.h"
#include "Helpers/PCGAsync.h"
#include "Partition/OWGChunk.h"

#define LOCTEXT_NAMESPACE "PCGFilterTargetBiomesSettings"

UPCGFilterTargetBiomesSettings::UPCGFilterTargetBiomesSettings() : bAddBiomeMetadataToPoints( false )
{
}

#if WITH_EDITOR

FName UPCGFilterTargetBiomesSettings::GetDefaultNodeName() const
{
	return FName(TEXT("OWG_FilterTargetBiomes"));
}

FText UPCGFilterTargetBiomesSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "OWG: Filter Target Biomes");
}

FText UPCGFilterTargetBiomesSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Determines the point's biome location, and filters it out if the point is not located in the biome this PCG graph belongs to.");
}

EPCGSettingsType UPCGFilterTargetBiomesSettings::GetType() const
{
	return EPCGSettingsType::Spatial;
}

#endif

TArray<FPCGPinProperties> UPCGFilterTargetBiomesSettings::InputPinProperties() const
{
	return DefaultPointInputPinProperties();
}

TArray<FPCGPinProperties> UPCGFilterTargetBiomesSettings::OutputPinProperties() const
{
	return DefaultPointOutputPinProperties();
}

FPCGElementPtr UPCGFilterTargetBiomesSettings::CreateElement() const
{
	return MakeShared<FPCGFilterTargetBiomesElement>();
}

FPCGContext* FPCGFilterTargetBiomesElement::Initialize( const FPCGDataCollection& InputData, TWeakObjectPtr<UPCGComponent> SourceComponent, const UPCGNode* Node )
{
	FPCGFilterTargetBiomesContext* Context = new FPCGFilterTargetBiomesContext();

	Context->InputData = InputData;
	Context->SourceComponent = SourceComponent;
	Context->Node = Node;

	if (UPCGComponent* SourceComponentRef = SourceComponent.Get() )
	{
		AOWGChunk* OwnerChunk = Cast<AOWGChunk>( SourceComponentRef->GetOwner() );
		
		UOWGChunkGenerator* OwnerChunkGenerator = nullptr;
		const auto ValueRetrievalResult = SourceComponentRef->GetGraphInstance()->GetUserParametersStruct()->GetValueObject( UPCGChunkGenerator::ChunkGeneratorPropertyName );
		
		if ( ValueRetrievalResult.HasValue() )
		{
			OwnerChunkGenerator = Cast<UOWGChunkGenerator>( ValueRetrievalResult.GetValue() );
		}

		if ( OwnerChunk )
		{
			Context->CachedChunkBiomeData = OwnerChunk->GetChunkBiomeData();
		}
		if ( OwnerChunkGenerator )
		{
			Context->TargetBiomes = OwnerChunkGenerator->TargetBiomes;
		}
	}

	return Context;
}

bool FPCGFilterTargetBiomesElement::ExecuteInternal( FPCGContext* Context ) const
{
	FPCGFilterTargetBiomesContext* CastContext = static_cast<FPCGFilterTargetBiomesContext*>( Context );
	const UPCGFilterTargetBiomesSettings* Settings = Context->GetInputSettings<UPCGFilterTargetBiomesSettings>();
	const bool bAddBiomeMetadataToPoints = Settings->bAddBiomeMetadataToPoints;

	TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputs();
	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	if ( !CastContext->CachedChunkBiomeData )
	{
		PCGE_LOG(Error, GraphAndLog, LOCTEXT("NoValidChunk", "Current PCG component does not have a valid Chunk associated with it"));
		return true;
	}
	if ( CastContext->TargetBiomes.IsEmpty() )
	{
		PCGE_LOG(Error, GraphAndLog, LOCTEXT("NoValidBiomes", "Current PCG Graph does not have a valid Biome-bound Chunk Generator assigned to it"));
		return true;
	}

	TSet<FBiomePaletteIndex> FilterValidIndices;
	for ( UOWGBiome* Biome : CastContext->TargetBiomes )
	{
		FilterValidIndices.Add( CastContext->CachedChunkBiomeData->BiomePalette.FindBiomeIndex( Biome ) );
	}

	for (const FPCGTaggedData& Input : Inputs)
	{
		FPCGTaggedData& Output = Outputs.Add_GetRef(Input);

		if (!Input.Data || Cast<UPCGSpatialData>(Input.Data) == nullptr)
		{
			PCGE_LOG(Error, GraphAndLog, LOCTEXT("InvalidInputData", "Invalid input data"));
			continue;
		}

		const UPCGPointData* OriginalData = Cast<UPCGSpatialData>(Input.Data)->ToPointData(Context);

		if (!OriginalData)
		{
			PCGE_LOG(Error, GraphAndLog, LOCTEXT("NoPointDataInInput", "Unable to get point data from input"));
			continue;
		}

		const TArray<FPCGPoint>& Points = OriginalData->GetPoints();
		
		UPCGPointData* FilteredData = NewObject<UPCGPointData>();
		FilteredData->InitializeFromData(OriginalData);
		TArray<FPCGPoint>& FilteredPoints = FilteredData->GetMutablePoints();

		UPCGMetadata* FilteredMetadata = FilteredData->MutableMetadata();

		const FChunkBiomePalette BiomePalette = CastContext->CachedChunkBiomeData->BiomePalette;
		TArray<FPCGMetadataAttribute<bool>*> BiomeAttributeByIndex;
		BiomeAttributeByIndex.SetNumZeroed( BiomePalette.NumBiomeMappings() );

		// Register attributes and add them to the array mapping to our biome palette index
		if ( bAddBiomeMetadataToPoints && FilteredMetadata )
		{
			for ( UOWGBiome* Biome : CastContext->TargetBiomes )
			{
				if ( Biome->PCGMetadataAttributeName != NAME_None )
				{
					// We override the parent value because we do not want parent values to carry over through filter biomes
					FPCGMetadataAttribute<bool>* BoolAttribute = FilteredMetadata->CreateAttribute<bool>( Biome->PCGMetadataAttributeName, false, false, true );
					BiomeAttributeByIndex[ BiomePalette.FindBiomeIndex( Biome ) ] = BoolAttribute;
				}
			}
		}
		
		Output.Data = FilteredData;
		const FCachedChunkBiomeData* ChunkBiomeData = CastContext->CachedChunkBiomeData.Get();

		FPCGAsync::AsyncPointProcessing(Context, Points.Num(), FilteredPoints, [&Points, ChunkBiomeData, bAddBiomeMetadataToPoints, &BiomeAttributeByIndex, &FilterValidIndices](int32 Index, FPCGPoint& OutPoint)
		{
			const FPCGPoint& Point = Points[Index];
			const FVector ChunkLocalPosition = ChunkBiomeData->ChunkToWorld.InverseTransformPosition( Point.Transform.GetLocation() );

			// Filter out positions outside of the chunk, or directly on the edges
			if ( FMath::Abs( ChunkLocalPosition.X ) >= FChunkCoord::ChunkSizeWorldUnits / 2.0f || FMath::Abs( ChunkLocalPosition.Y ) >= FChunkCoord::ChunkSizeWorldUnits / 2.0f )
			{
				return false;
			}

			const FVector2f NormalizedPosition = FChunkData2D::ChunkLocalPositionToNormalized( ChunkLocalPosition );
			const FBiomePaletteIndex PaletteIndex = ChunkBiomeData->BiomeMap.GetClosestElementAt<FBiomePaletteIndex>( NormalizedPosition );

			// Filter out the biomes that we do not care about
			if ( !FilterValidIndices.Contains( PaletteIndex ) )
			{
				return false;
			}

			OutPoint = Point;

			// Populate metadata attribute for the point
			if ( bAddBiomeMetadataToPoints && OutPoint.MetadataEntry != INDEX_NONE )
			{
				if ( FPCGMetadataAttribute<bool>* Attribute = BiomeAttributeByIndex[ PaletteIndex ] )
				{
					Attribute->SetValue( OutPoint.MetadataEntry, true );
				}
			}
			return true;
		});

		PCGE_LOG(Verbose, LogOnly, FText::Format(LOCTEXT("GenerationInfo", "Generated {0} points out of {1} source points"), FilteredPoints.Num(), Points.Num()));
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
