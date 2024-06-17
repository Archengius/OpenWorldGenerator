// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "OpenWorldGeneratorEditorSettings.generated.h"

class UOWGWorldGeneratorConfiguration;

UCLASS( Config = "EditorPerProjectUserSettings", meta = (DisplayName = "OWG Local Settings") )
class OPENWORLDGENERATOR_API UOpenWorldGeneratorEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	/** True if PIE seed should not be randomized */
	UPROPERTY( EditAnywhere, Config, Category = "Open World Generator" )
	bool bStablePIESeed{false};

	/** Generator settings override for local PIE */
	UPROPERTY( EditAnywhere, Config, Category = "Open World Generator" )
	TSoftObjectPtr<UOWGWorldGeneratorConfiguration> PIEGeneratorSettingsOverride; 
	
	/** Stable world seed to use when playing in the Editor. Not used if PIE seed is not stable */
	UPROPERTY( EditAnywhere, Config, Category = "Open World Generator", meta = ( EditConditionHides, EditCondition = "bStablePIESeed" ) )
	int32 PIEWorldSeed{0};
};
