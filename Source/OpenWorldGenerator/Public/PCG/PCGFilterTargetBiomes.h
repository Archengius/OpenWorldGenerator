// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "PCGContext.h"
#include "PCGSettings.h"
#include "PCGPin.h"
#include "PCGFilterTargetBiomes.generated.h"

class UOWGBiome;
struct FCachedChunkBiomeData;

/**
 * Filters points to only be physically located inside one of the biomes associated with the currently running chunk generator that instigated this PCG graph execution
 * To be used in conjunction with PCGChunkGenerator. If the PCG chunk generator is not used, this element will unconditionally filter out all points and return empty set.
 */
UCLASS( BlueprintType, ClassGroup = ("Procedural") )
class OPENWORLDGENERATOR_API UPCGFilterTargetBiomesSettings : public UPCGSettings
{
	GENERATED_BODY()
public:
	UPCGFilterTargetBiomesSettings();

	// Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override;
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGSettingsType GetType() const override;
#endif
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
protected:
	virtual FPCGElementPtr CreateElement() const override;
	// End UPCGSettings
public:
	/** True if we should add biome metadata to each point. This is only useful when you want to conditionally do biome-specific logic in a shared PCG graph */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Settings" )
	bool bAddBiomeMetadataToPoints;
};

class FPCGFilterTargetBiomesContext : public FPCGContext
{
public:
	TSharedPtr<FCachedChunkBiomeData> CachedChunkBiomeData;
	TArray<TWeakObjectPtr<UOWGBiome>> TargetBiomes;
};

class FPCGFilterTargetBiomesElement : public IPCGElement
{
public:
	virtual FPCGContext* Initialize(const FPCGDataCollection& InputData, TWeakObjectPtr<UPCGComponent> SourceComponent, const UPCGNode* Node) override;
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
