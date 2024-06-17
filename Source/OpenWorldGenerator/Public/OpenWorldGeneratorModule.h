// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

OPENWORLDGENERATOR_API DECLARE_LOG_CATEGORY_EXTERN(LogOpenWorldGenerator, All, All);

class FOpenWorldGeneratorModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
