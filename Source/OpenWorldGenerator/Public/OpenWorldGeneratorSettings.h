// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "OpenWorldGeneratorSettings.generated.h"

class AOWGChunk;
class UOWGRegionContainer;
class UOWGWorldGeneratorConfiguration;

UCLASS( Config = "OpenWorldGenerator", DefaultConfig, meta = (DisplayName = "OWG Settings") )
class OPENWORLDGENERATOR_API UOpenWorldGeneratorSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	UOpenWorldGeneratorSettings();

	static UOpenWorldGeneratorSettings* Get();
	
	/** Class to use for chunks */
	UPROPERTY( EditAnywhere, Config, Category = "Open World Generator|General" )
	TSoftClassPtr<AOWGChunk> ChunkClass;

	/** Class to use for region containers. You probably want to keep it default */
	UPROPERTY( EditAnywhere, Config, Category = "Open World Generator|General", AdvancedDisplay )
	TSoftClassPtr<UOWGRegionContainer> RegionContainerClass;

	/** A list of chunk streaming providers that will be automatically registered. You usually want to at the very minimum stream chunks around players */
	UPROPERTY( EditAnywhere, Config, Category = "Open World Generator|General", meta = ( MustImplement = "/Script/OpenWorldGenerator.OWGChunkStreamingProvider" ) )
	TArray<TSoftClassPtr<UObject>> ChunkStreamingProviders;

	/** Amount of time the chunk should be idle before the chunk manager will unload it */
	UPROPERTY( EditAnywhere, Config, Category = "Open World Generator|General" )
	float ChunkUnloadIdleTime;

	/** World generator that will be used by default unless an override was specified through the URL */
	UPROPERTY( EditAnywhere, Config, Category = "Open World Generator|General" )
	TSoftObjectPtr<UOWGWorldGeneratorConfiguration> DefaultWorldGenerator;
};
