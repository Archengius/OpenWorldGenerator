// Copyright Nikita Zolotukhin. All Rights Reserved.

#include "Generation/OWGChunkGenerator.h"
#include "OpenWorldGeneratorSubsystem.h"
#include "Partition/OWGChunk.h"
#include "Partition/OWGChunkManagerInterface.h"

DEFINE_LOG_CATEGORY( LogChunkGenerator );

UOWGChunkGenerator::UOWGChunkGenerator()
{
}

UWorld* UOWGChunkGenerator::GetWorld() const
{
	if ( !HasAnyFlags( RF_ClassDefaultObject | RF_ArchetypeObject ) )
	{
		return Super::GetWorld();
	}
	return nullptr;
}

AOWGChunk* UOWGChunkGenerator::GetChunk() const
{
	return CastChecked<AOWGChunk>( GetOuter() );
}

bool UOWGChunkGenerator::AdvanceChunkGeneration_Implementation()
{
	// End the generation immediately
	return true;
}

void UOWGChunkGenerator::EndChunkGeneration_Implementation()
{
}

bool UOWGChunkGenerator::CanPersistChunkGenerator_Implementation() const
{
	// Can persist the generator at all times by default
	return true;
}

void UOWGChunkGenerator::NotifyAboutToUnloadChunk_Implementation()
{
}

bool UOWGChunkGenerator::WaitForAdjacentChunkGeneration( EChunkGeneratorStage TargetStage, int32 Range )
{
	if ( const UOpenWorldGeneratorSubsystem* OpenWorldGeneratorSubsystem = UOpenWorldGeneratorSubsystem::Get( this ) )
	{
		const FChunkCoord SelfChunkCoord = GetChunk()->GetChunkCoord();
		const TScriptInterface<IOWGChunkManagerInterface> ChunkManager = OpenWorldGeneratorSubsystem->GetChunkManager();

		bool bAllChunksFinished = true;

		// We are done when all of the chunks in range have reached the target generation stage
		for ( int32 PosX = -Range; PosX <= Range; PosX++ )
		{
			for ( int32 PosY = -Range; PosY <= Range; PosY++ )
			{
				// Make sure to not accidentally generate ourselves
				const FChunkCoord NewChunkCoord( SelfChunkCoord.PosX + PosX, SelfChunkCoord.PosY + PosY );
				if ( NewChunkCoord != SelfChunkCoord )
				{
					if ( AOWGChunk* NewChunk = ChunkManager->LoadOrCreateChunk( NewChunkCoord ) )
					{
						NewChunk->RequestChunkGeneration( TargetStage );
						bAllChunksFinished &= NewChunk->GetCurrentGenerationStage() >= TargetStage;
					}
				}
			}
		}
		return bAllChunksFinished;
	}
	return false;
}
