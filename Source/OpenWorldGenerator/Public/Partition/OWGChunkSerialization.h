// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChunkCoord.h"
#include "Misc/EngineVersion.h"
#include "Serialization/ArchiveProxy.h"
#include "Serialization/CustomVersion.h"
#include "UObject/ObjectMacros.h"
#include "UObject/TopLevelAssetPath.h"

class UOWGRegionContainer;
class AOWGChunk;

/** Object flag used to prevent the object from being saved by the save system. RF_Dynamic is not set or read by the engine code so we can use it as we wish */
PRAGMA_DISABLE_DEPRECATION_WARNINGS
static constexpr auto RF_SaveSystemTransient = RF_Dynamic;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

DECLARE_LOG_CATEGORY_EXTERN( LogChunkSerialization, All, All );

struct OPENWORLDGENERATOR_API FOpenWorldGeneratorVersion
{
	enum Type
	{
		// Initial version of the open world generator
		InitialVersion = 1,
		
		// Add new versions above this line
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	// The GUID for this custom version number
	static const FGuid GUID;
private:
	FOpenWorldGeneratorVersion() {}
};

enum class EChunkPackageVersion : uint32
{
	InitialVersion = 0,

	// Add new versions above this line
	LatestPlusOne,
	Latest = LatestPlusOne - 1,
};

struct OPENWORLDGENERATOR_API FChunkPackageSummary
{
	EChunkPackageVersion ChunkPackageVersion{};
	FPackageFileVersion PackageFileVersion{};
	FEngineVersion EngineVersion{};
	
	int32 CustomVersionsOffset{INDEX_NONE};
	int32 NameMapOffset{INDEX_NONE};
	int32 ImportMapOffset{INDEX_NONE};
	int32 ExportMapOffset{INDEX_NONE};
	int32 ChunkExportIndex{INDEX_NONE};

	/* Sets all of the versions to latest */
	void SetToLatest();
	/** Serializes the summary to/from the archive */
	friend FArchive& operator<<( FArchive& Ar, FChunkPackageSummary& PackageSummary );
};

enum class EChunkObjectFlags : uint64
{
	None = 0x00,
	// This object is a UWorld we are loading the save game in. Object name or outer is not serialized. Only relevant for object imports.
	MapPackage = 0x01,
	// This object is a ULevel that is a Level of the world we are currently in
	MapLevel = 0x02,
	// Object is an actor, and as such contains an actor transform and actor owner Only relevant for object exports.
	Actor = 0x04,
	// Object is a chunk. There is only one chunk export in the chunk package, and there should never be any chunk imports
	Chunk = 0x08,
	// Object is a region container containing the chunk we are loading. Does not have a name or an outer
	RegionContainer = 0x10,
};
ENUM_CLASS_FLAGS( EChunkObjectFlags );

struct OPENWORLDGENERATOR_API FChunkObjectEntry
{
	int32 OuterIndex{0};
	FName ObjectName;
	FTopLevelAssetPath ClassName;
	EChunkObjectFlags ChunkObjectFlags{EChunkObjectFlags::None};

	FChunkObjectEntry() = default;
	explicit FChunkObjectEntry( UObject* Object );

	// Resolved object. Transient.
	UObject* XObject{nullptr};
	// True if we are currently resolving an object. Circular dependencies are not supported. Transient.
	bool bCurrentlyResolving{false};

	/** Serializes the import to/from the archive */
	friend FArchive& operator<<( FArchive& Ar, FChunkObjectEntry& ObjectImport );
};

struct OPENWORLDGENERATOR_API FChunkObjectImport : FChunkObjectEntry
{
	FChunkObjectImport() = default;
	explicit FChunkObjectImport( UObject* Object ) : FChunkObjectEntry( Object ) {}

	/** Serializes the import to/from the archive */
	friend FArchive& operator<<( FArchive& Ar, FChunkObjectImport& ObjectImport );
};

struct OPENWORLDGENERATOR_API FChunkObjectExport : FChunkObjectEntry
{
	int32 ObjectFlags{RF_NoFlags};
	int32 ActorOwner{0};
	FTransform ActorTransform{};
	int32 SerializedDataOffset{INDEX_NONE};
	int32 SerializedDataSize{INDEX_NONE};

	// True if this is an actor and we need to dispatch 
	bool bNeedsFinishSpawning{false};

	FChunkObjectExport() = default;
	explicit FChunkObjectExport( UObject* Object );

	/** Serializes the import to/from the archive */
	friend FArchive& operator<<( FArchive& Ar, FChunkObjectExport& ObjectExport );
};

class OPENWORLDGENERATOR_API FChunkSerializationContext : public FArchiveProxy
{
protected:
	// The world we are loading the objects into
	UOWGRegionContainer* RegionContainer{nullptr};
	// Coordinate of the chunk we are currently loading/saving
	FChunkCoord ChunkCoord{};
	// When saving or after the loading is done, the chunk object we are loading/saving to/from
	AOWGChunk* ChunkObject{nullptr};

	// Package summary and the offset to it so we can patch it up later
	int32 PackageSummaryOffset{INDEX_NONE};
	FChunkPackageSummary PackageSummary{};

	// Name map and reverse lookup map
	TMap<FName, int32> NameLookupMap;
	TArray<FName> NameMap;

	// Import map and import entries for each object
	TMap<UObject*, int32> ImportIndexMap;
	TArray<FChunkObjectImport> ImportMap;

	// Export map and export entries for each object
	TMap<UObject*, int32> ExportIndexMap;
	TArray<FChunkObjectExport> ExportMap;
public:
	FChunkSerializationContext( FArchive& Ar, AOWGChunk* InChunk );
	FChunkSerializationContext( FArchive& Ar, UOWGRegionContainer* InRegionContainer, FChunkCoord InChunkCoord );

	static AOWGChunk* DeserializeChunk( UOWGRegionContainer* RegionContainer, FChunkCoord ChunkCoord, const TArray<uint8>& ChunkSerializedData, TFunctionRef<void(AOWGChunk*)> PostChunkLoaded );
	static void SerializeChunk( AOWGChunk* Chunk, TArray<uint8>& OutChunkSerializedData );

	// Begin FArchive Interface
	virtual FArchive& operator<<( FName& Value ) override;
	virtual FArchive& operator<<( UObject*& Value ) override;
	virtual FArchive& operator<<( FWeakObjectPtr& Obj ) override;
	virtual FArchive& operator<<( FSoftObjectPtr& Value ) override;
	virtual FArchive& operator<<( FSoftObjectPath& Value ) override;
	virtual FArchive& operator<<( FObjectPtr& Obj ) override;
	// End FArchive Interface
protected:
	AOWGChunk* DoChunkDeserialize();
	void DoChunkSerialize();
	
	/** Returns true if we should export this object as a part of the chunk serialization context */
	bool ShouldExportObject( UObject* Object ) const;

	UObject* ResolveObject( int32 ObjectIndex );
	int32 WriteObject( UObject* Object );
	
	UObject* ResolveImport( int32 ImportIndex );
	UObject* ResolveExport( int32 ExportIndex );

	int32 WriteImport( UObject* Object );
	int32 WriteExport( UObject* Object );

	UObject* CreateImport( FChunkObjectImport& ObjectImport );
	UObject* CreateExport( FChunkObjectExport& ObjectExport );

	void SerializePackageSummary();
	void SerializeCustomVersions();
	void SerializeNameMap();
	void SerializeImportMap();
	void SerializeExportMap();
	void SerializeExports();
	void PatchUpPackageSummary();
	void DispatchFinishSpawnOnExports();
};
