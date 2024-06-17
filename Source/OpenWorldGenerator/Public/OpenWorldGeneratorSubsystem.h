// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "Generation/OWGWorldGeneratorConfiguration.h"
#include "OpenWorldGeneratorSubsystem.generated.h"

class UOWGWorldGeneratorConfiguration;
class IOWGChunkManagerInterface;
class UChunkTextureManager;

/** Singleton instance holding data relevant for the open world generator */
UCLASS( BlueprintType )
class OPENWORLDGENERATOR_API UOpenWorldGeneratorSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	UOpenWorldGeneratorSubsystem();

	/** Returns the open world generator subsystem instance for the world */
	UFUNCTION( BlueprintPure, Category = "Open World Generator", DisplayName = "Get World Generator Subsystem", meta = ( WorldContext = "WorldContext", DeprecatedFunction, DeprecationMessage = "Use Subsystem getter instead" ) )
	static UOpenWorldGeneratorSubsystem* Get( const UObject* WorldContext );
	
	// Begin AActor interface
	virtual void GetLifetimeReplicatedProps( TArray<FLifetimeProperty>& OutLifetimeProps ) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	// End AActor interface
	
	/** Returns the chunk manager for this world */
	UFUNCTION( BlueprintPure, Category = "Open World Generator" )
	FORCEINLINE TScriptInterface<IOWGChunkManagerInterface> GetChunkManager() const { return ChunkManager; }
	
	/** Returns the generation seed of the current world. Worlds with identical seeds will have identical generation features */
	UFUNCTION( BlueprintPure, Category = "Open World Generator" )
	FORCEINLINE int32 GetWorldSeed() const { return WorldSeed; }

	/** Returns the world generator definition used by this world */
	UFUNCTION( BlueprintPure, Category = "Open World Generator" )
	FORCEINLINE UOWGWorldGeneratorConfiguration* GetWorldGeneratorDefinition() const { return WorldGeneratorDefinition; }

	FORCEINLINE UChunkTextureManager* GetChunkTextureManager() const { return TextureManager; }

	/** Attempts to find and load a world generator package given the name */
	static UOWGWorldGeneratorConfiguration* LoadWorldGeneratorPackageFromShortName( const FString& InWorldGeneratorName );
protected:

	/** Chunk manager that actually manages the chunk I/O and loading/unloading */
	UPROPERTY()
	TScriptInterface<IOWGChunkManagerInterface> ChunkManager;

	UPROPERTY()
	UChunkTextureManager* TextureManager;

	/** World generator that has been selected for this world */
	UPROPERTY()
	UOWGWorldGeneratorConfiguration* WorldGeneratorDefinition;
	/** World seed that has been selected for this world */
	int32 WorldSeed;
};
