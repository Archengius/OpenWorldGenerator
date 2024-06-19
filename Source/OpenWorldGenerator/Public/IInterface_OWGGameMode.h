// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IInterface_OWGGameMode.generated.h"

class UOWGWorldGeneratorConfiguration;

/** Save game data for Open World Generator, that should be persisted by the game mode */
USTRUCT()
struct OPENWORLDGENERATOR_API FOWGSaveGameData
{
	GENERATED_BODY()

	UPROPERTY()
	TSoftObjectPtr<UOWGWorldGeneratorConfiguration> WorldGenerator;
	UPROPERTY()
	int32 WorldSeed{0};
};

USTRUCT()
struct OPENWORLDGENERATOR_API FOWGNewWorldCreationData
{
	GENERATED_BODY()

	/** World generator that should be used for this world. By default, it is a default world generator from OWG settings */
	UPROPERTY()
	UOWGWorldGeneratorConfiguration* WorldGenerator{};

	/** World seed that should be used for this world. By default, a random seed is generated */
	UPROPERTY()
	int32 WorldSeed{0};
};

/** Interface to be implemented by the game mode to persist Open World Generator settings */
UINTERFACE(meta=(CannotImplementInterfaceInBlueprint))
class OPENWORLDGENERATOR_API UInterface_OWGGameMode : public UInterface
{
	GENERATED_BODY()
};

class OPENWORLDGENERATOR_API IInterface_OWGGameMode
{
	GENERATED_BODY()
public:
	/** Called by Open World Generator when no save game data is available, to populate the new OWG world settings */
	virtual void ModifyNewOWGWorldParameters(FOWGNewWorldCreationData& NewWorldCreationData) {}
	/** Called by the OWG during initialization. Returning true will disable OWG for this world */
	virtual bool ShouldInitializeOWG() const { return true; }
	
	/** Retrieves OWG save game data loaded for this world. Return false if no save game was loaded */
	virtual bool GetOWGSaveGameData(FOWGSaveGameData& OutLoadedData) const = 0;
	/** Updates OWG save game data for this world. Data should be returned by the call to GetOWGSaveGameData and persist across world restarts */
	virtual void SetOWGSaveGameData(const FOWGSaveGameData& NewSaveGameData) = 0;
	/** Returns the directory in which OWG should save it's regions. Return empty string if no data should be saved */
	virtual FString GetOWGSaveGameRegionFolderPath() const = 0;
};
