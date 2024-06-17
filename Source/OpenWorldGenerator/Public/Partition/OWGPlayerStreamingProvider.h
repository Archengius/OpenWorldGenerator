// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OWGChunkStreamingProvider.h"
#include "OWGPlayerStreamingProvider.generated.h"

USTRUCT()
struct FPlayerStreamingDescriptor
{
	GENERATED_BODY()

	/** Radius in which chunks around players should be streamed in */
	UPROPERTY( EditAnywhere, Category = "Streaming Descriptor" )
	float StreamingRadius{};

	/** The generation stage in which the chunks in the radius should be */
	UPROPERTY( EditAnywhere, Category = "Streaming Descriptor" )
	EChunkGeneratorStage GenerationStage{};

	/** LOD of the chunk landscape mesh we should target */
	UPROPERTY( EditAnywhere, Category = "Streaming Descriptor" )
	int32 ChunkLOD{};
};

UCLASS( Blueprintable )
class OPENWORLDGENERATOR_API UOWGPlayerStreamingProvider : public UObject, public IOWGChunkStreamingProvider
{
	GENERATED_BODY()
public:
	UOWGPlayerStreamingProvider();

	// Begin IOWGChunkStreamingProvider interface
	virtual void GetStreamingSources(TArray<FChunkStreamingSource>& OutStreamingSources) const override;
	// End IOWGChunkStreamingProvider interface

	/** Streaming distances for the players */
	UPROPERTY( EditAnywhere, Category = "Player Streaming Provider" )
	TArray<FPlayerStreamingDescriptor> StreamingDescriptors;
};