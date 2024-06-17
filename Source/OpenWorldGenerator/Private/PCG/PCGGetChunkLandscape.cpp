// Copyright Nikita Zolotukhin. All Rights Reserved.

#include "PCG/PCGGetChunkLandscape.h"
#include "Partition/OWGChunk.h"
#include "PCG/PCGChunkLandscapeData.h"

#define LOCTEXT_NAMESPACE "PCGGetChunkLandscape"

UPCGGetChunkLandscapeSettings::UPCGGetChunkLandscapeSettings() : bIncludeMetadata( true )
{
}

#if WITH_EDITOR

FName UPCGGetChunkLandscapeSettings::GetDefaultNodeName() const
{
	return FName(TEXT("OWG_GetChunkLandscape"));
}

FText UPCGGetChunkLandscapeSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "OWG: Get Chunk Landscape");
}

FText UPCGGetChunkLandscapeSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Retrieves the landscape information from the chunk as Surface data.");
}

EPCGSettingsType UPCGGetChunkLandscapeSettings::GetType() const
{
	return EPCGSettingsType::Spawner;
}

#endif

TArray<FPCGPinProperties> UPCGGetChunkLandscapeSettings::InputPinProperties() const
{
	// We have no inputs, the input is the chunk in the context of which we are being evaluated
	return TArray<FPCGPinProperties>{};
}

TArray<FPCGPinProperties> UPCGGetChunkLandscapeSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Properties;
	Properties.Emplace_GetRef( TEXT("Landscape"), EPCGDataType::Surface ).bAllowMultipleData = false;
	return Properties;
}

FPCGElementPtr UPCGGetChunkLandscapeSettings::CreateElement() const
{
	return MakeShared<FPCGGetChunkLandscapeElement>();
}

FPCGContext* FPCGGetChunkLandscapeElement::Initialize( const FPCGDataCollection& InputData, TWeakObjectPtr<UPCGComponent> SourceComponent, const UPCGNode* Node )
{
	FPCGGetChunkLandscapeContext* Context = new FPCGGetChunkLandscapeContext();

	Context->InputData = InputData;
	Context->SourceComponent = SourceComponent;
	Context->Node = Node;

	if ( const UPCGComponent* SourceComponentRef = SourceComponent.Get() )
	{
		AOWGChunk* OwnerChunk = Cast<AOWGChunk>( SourceComponentRef->GetOwner() );

		if ( OwnerChunk )
		{
			Context->LandscapeData = OwnerChunk->GetChunkLandscapeSourceData();
			Context->BiomeData = OwnerChunk->GetChunkBiomeData();
		}
	}
	return Context;
}

bool FPCGGetChunkLandscapeElement::ExecuteInternal( FPCGContext* Context ) const
{
	const FPCGGetChunkLandscapeContext* CastContext = static_cast<FPCGGetChunkLandscapeContext*>( Context );
	const UPCGGetChunkLandscapeSettings* Settings = Context->GetInputSettings<UPCGGetChunkLandscapeSettings>();

	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	if ( !CastContext->LandscapeData.IsValid() )
	{
		PCGE_LOG(Error, GraphAndLog, LOCTEXT("NoValidChunk", "Current PCG component does not have a valid Chunk associated with it"));
		return true;
	}

	UPCGChunkLandscapeData* LandscapeData = NewObject<UPCGChunkLandscapeData>();
	LandscapeData->Initialize( CastContext->LandscapeData, CastContext->BiomeData, Settings->bIncludeMetadata );

	FPCGTaggedData& OutputData = Outputs.AddDefaulted_GetRef();
	OutputData.Pin = TEXT("Landscape");
	OutputData.Data = LandscapeData;
	return true;
}

#undef LOCTEXT_NAMESPACE
