// Copyright Nikita Zolotukhin. All Rights Reserved.

#include "Partition/OWGPlayerStreamingProvider.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

UOWGPlayerStreamingProvider::UOWGPlayerStreamingProvider()
{
}

void UOWGPlayerStreamingProvider::GetStreamingSources( TArray<FChunkStreamingSource>& OutStreamingSources ) const
{
	if ( const UWorld* World = GetWorld() )
	{
		for ( FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It ) 
		{
			if ( const APlayerController* PlayerController = It->Get() )
			{
				FVector PlayerViewPointLocation{};
				FRotator PlayerRotation{};
				PlayerController->GetPlayerViewPoint( PlayerViewPointLocation, PlayerRotation );

				for ( const FPlayerStreamingDescriptor& StreamingDescriptor : StreamingDescriptors )
				{
					OutStreamingSources.Add( FChunkStreamingSource( StreamingDescriptor.GenerationStage, StreamingDescriptor.ChunkLOD,
						PlayerViewPointLocation, StreamingDescriptor.StreamingRadius ) );
				}
			}
		}
	}
}
