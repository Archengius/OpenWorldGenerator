// Copyright Nikita Zolotukhin. All Rights Reserved.

#include "OpenWorldGeneratorSubsystem.h"
#include "IInterface_OWGGameMode.h"
#include "OpenWorldGeneratorEditorSettings.h"
#include "OpenWorldGeneratorSettings.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "Misc/PackageName.h"
#include "Net/UnrealNetwork.h"
#include "Partition/OWGChunkManagerInterface.h"
#include "Partition/OWGServerChunkManager.h"
#include "Rendering/ChunkTextureManager.h"
#include "UObject/Package.h"

UOpenWorldGeneratorSubsystem::UOpenWorldGeneratorSubsystem()
{
	TextureManager = CreateDefaultSubobject<UChunkTextureManager>( TEXT("ChunkTextureManager") );
}

UOpenWorldGeneratorSubsystem* UOpenWorldGeneratorSubsystem::Get( const UObject* WorldContext )
{
	if ( const UWorld* World = GEngine->GetWorldFromContextObject( WorldContext, EGetWorldErrorMode::ReturnNull ) )
	{
		return World->GetSubsystem<UOpenWorldGeneratorSubsystem>();
	}
	return nullptr;
}

void UOpenWorldGeneratorSubsystem::GetLifetimeReplicatedProps( TArray<FLifetimeProperty>& OutLifetimeProps ) const
{
	Super::GetLifetimeReplicatedProps( OutLifetimeProps );

	DOREPLIFETIME( ThisClass, WorldGeneratorDefinition );
	DOREPLIFETIME( ThisClass, WorldSeed );
}

void UOpenWorldGeneratorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	IInterface_OWGGameMode* GameMode = CastChecked<IInterface_OWGGameMode>(GetWorld()->GetAuthGameMode());
	
	FOWGSaveGameData LoadedSaveGameData;
	if (!GameMode->GetOWGSaveGameData(LoadedSaveGameData))
	{
		// If there is no valid save game data, we need to create the new data
		FOWGNewWorldCreationData CreationData;
		CreationData.WorldGenerator = UOpenWorldGeneratorSettings::Get()->DefaultWorldGenerator.LoadSynchronous();
		CreationData.WorldSeed = FMath::Rand();
#if WITH_EDITOR
		if (GetWorld()->WorldType == EWorldType::PIE)
		{
			const UOpenWorldGeneratorEditorSettings* EditorSettings = GetDefault<UOpenWorldGeneratorEditorSettings>();
			if (UOWGWorldGeneratorConfiguration* WorldGeneratorOverrideSettings = EditorSettings->PIEGeneratorSettingsOverride.LoadSynchronous())
			{
				CreationData.WorldGenerator = WorldGeneratorOverrideSettings;
			}
			if (EditorSettings->bStablePIESeed)
			{
				CreationData.WorldSeed = EditorSettings->PIEWorldSeed;
			}
		}
#endif
		GameMode->ModifyNewOWGWorldParameters(CreationData);

		checkf(CreationData.WorldGenerator, TEXT("Failed to resolve world generator for a new world. Make sure DefaultWorldGenerator is valid, or that GameMode overrides it."));
		WorldGeneratorDefinition = CreationData.WorldGenerator;
		WorldSeed = CreationData.WorldSeed;
	}
	// Load SaveGame data from the save provided by the game mode
	else
	{
		WorldGeneratorDefinition = LoadedSaveGameData.WorldGenerator.LoadSynchronous();
		WorldSeed = LoadedSaveGameData.WorldSeed;
		checkf(WorldGeneratorDefinition, TEXT("Failed to resolve world generator for a new world. Make sure DefaultWorldGenerator is valid, or that GameMode overrides it."));
	}

	// We should already have a valid NetMode at this stage, since the world is already bound to a valid WorldContext
	const bool bIsServer = GetWorld()->GetNetMode() != NM_Client;
	const UClass* ChunkManagerClass = bIsServer ? UOWGServerChunkManager::StaticClass() : nullptr;
	checkf( ChunkManagerClass, TEXT("Failed to setup valid chunk manager class") );
	
	ChunkManager = NewObject<UObject>( this, ChunkManagerClass, TEXT("ServerChunkManager"), RF_NoFlags );
	ChunkManager->Initialize();

	// Update the server chunk manager to point to the correct region folder path
	if (UOWGServerChunkManager* ServerChunkManager = Cast<UOWGServerChunkManager>(ChunkManager.GetObject()))
	{
		ServerChunkManager->SetRegionFolderPath(GameMode->GetOWGSaveGameRegionFolderPath());
	}
}

void UOpenWorldGeneratorSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	
	ChunkManager->BeginPlay();
}

bool UOpenWorldGeneratorSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UOpenWorldGeneratorSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	UWorld* World = CastChecked<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World->GetAuthGameMode() &&
		World->GetAuthGameMode()->Implements<UInterface_OWGGameMode>();
}

UOWGWorldGeneratorConfiguration* UOpenWorldGeneratorSubsystem::LoadWorldGeneratorPackageFromShortName( const FString& InWorldGeneratorName )
{
	// Attempt to resolve potentially short package name into the full path
	FName LongPackageName = *InWorldGeneratorName;
	if ( FPackageName::IsShortPackageName( LongPackageName ) )
	{
		const IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
		LongPackageName = AssetRegistry.GetFirstPackageByName( LongPackageName.ToString() );
	}

	// Attempt to load the package if we have one
	if ( LongPackageName != NAME_None )
	{
		const UPackage* WorldGeneratorPackage = FindPackage( nullptr, *LongPackageName.ToString() );
		if ( !WorldGeneratorPackage )
		{
			WorldGeneratorPackage = LoadPackage( nullptr, *LongPackageName.ToString(), LOAD_None );
		}
		TArray<UObject*> TopLevelObjects;
		GetObjectsWithPackage( WorldGeneratorPackage, TopLevelObjects, false );

		// Find the asset inside of the package.
		for ( UObject* TopLevelObject : TopLevelObjects )
		{
			if ( UOWGWorldGeneratorConfiguration* WorldGeneratorDefinition = Cast<UOWGWorldGeneratorConfiguration>( TopLevelObject ) )
			{
				return WorldGeneratorDefinition;
			}
		}
	}
	return nullptr;
}

void UOpenWorldGeneratorSubsystem::Deinitialize()
{
	Super::Deinitialize();

	ChunkManager->Deinitialize();
	TextureManager->ReleasePooledTextures();
}

void UOpenWorldGeneratorSubsystem::Tick( float DeltaTime )
{
	Super::Tick( DeltaTime );

	if ( ChunkManager )
	{
		ChunkManager->Tick( DeltaTime );
	}
}
