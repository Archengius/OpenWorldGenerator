// Copyright Nikita Zolotukhin. All Rights Reserved.

#include "OpenWorldGeneratorSettings.h"
#include "Partition/OWGChunk.h"
#include "Partition/OWGPlayerStreamingProvider.h"

UOpenWorldGeneratorSettings::UOpenWorldGeneratorSettings() :
	ChunkClass( AOWGChunk::StaticClass() ),
	RegionContainerClass( UOWGRegionContainer::StaticClass() ),
	ChunkUnloadIdleTime( 20.0f )
{
}

UOpenWorldGeneratorSettings* UOpenWorldGeneratorSettings::Get()
{
	return GetMutableDefault<UOpenWorldGeneratorSettings>();
}
