// Copyright Nikita Zolotukhin. All Rights Reserved.

#include "Partition/OWGChunkSerialization.h"
#include "Misc/EngineVersion.h"
#include "Partition/OWGChunk.h"
#include "Engine/World.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY( LogChunkSerialization );

const FGuid FOpenWorldGeneratorVersion::GUID( 0x62E62C6A, 0x9DDD11EE, 0x8C900242, 0xAC120002 );
static FCustomVersionRegistration GOWGVersionRegistration( FOpenWorldGeneratorVersion::GUID, FOpenWorldGeneratorVersion::LatestVersion, TEXT("Open World Generator Version") );

void FChunkPackageSummary::SetToLatest()
{
	ChunkPackageVersion = EChunkPackageVersion::Latest;
	PackageFileVersion = GPackageFileUEVersion;
	EngineVersion = FEngineVersion::Current();
}

FArchive& operator<<( FArchive& Ar, FChunkPackageSummary& PackageSummary )
{
	// Serialize version data
	Ar << PackageSummary.ChunkPackageVersion;
	Ar << PackageSummary.PackageFileVersion;
	Ar << PackageSummary.EngineVersion;

	// Serialize offsets
	Ar << PackageSummary.CustomVersionsOffset;
	Ar << PackageSummary.NameMapOffset;
	Ar << PackageSummary.ImportMapOffset;
	Ar << PackageSummary.ExportMapOffset;
	Ar << PackageSummary.ChunkExportIndex;
	return Ar;
}

FChunkObjectEntry::FChunkObjectEntry( UObject* Object ) : ObjectName( Object->GetFName() ), ClassName( Object->GetClass()->GetClassPathName() ), XObject( Object )
{
	if ( Object->IsA<AOWGChunk>() )
	{
		ChunkObjectFlags = EChunkObjectFlags::Chunk;
	}
	if ( Object->IsA<AActor>() )
	{
		ChunkObjectFlags = EChunkObjectFlags::Actor;
	}
	if ( Object->IsA<UWorld>() )
	{
		ChunkObjectFlags = EChunkObjectFlags::MapPackage;
	}
	if ( Object->IsA<ULevel>() )
	{
		ChunkObjectFlags = EChunkObjectFlags::MapLevel;
	}
	if ( Object->IsA<UOWGRegionContainer>() )
	{
		ChunkObjectFlags = EChunkObjectFlags::RegionContainer;
	}
}

FArchive& operator<<( FArchive& Ar, FChunkObjectEntry& ObjectImport )
{
	Ar << ObjectImport.ClassName;
	Ar << ObjectImport.ChunkObjectFlags;

	if ( !EnumHasAnyFlags( ObjectImport.ChunkObjectFlags, EChunkObjectFlags::MapPackage | EChunkObjectFlags::RegionContainer ) )
	{
		Ar << ObjectImport.OuterIndex;
		Ar << ObjectImport.ObjectName;
	}
	return Ar;
}

FArchive& operator<<( FArchive& Ar, FChunkObjectImport& ObjectImport )
{
	Ar << static_cast<FChunkObjectEntry&>( ObjectImport );
	return Ar;
}

FChunkObjectExport::FChunkObjectExport( UObject* Object ) : FChunkObjectEntry( Object ), ObjectFlags( Object->GetFlags() & RF_Load )
{
	if ( const AActor* Actor = Cast<AActor>( Object ) )
	{
		ActorTransform = Actor->GetTransform();
	}
}

FArchive& operator<<( FArchive& Ar, FChunkObjectExport& ObjectExport )
{
	Ar << static_cast<FChunkObjectEntry&>( ObjectExport );
	Ar << ObjectExport.ObjectFlags;

	Ar << ObjectExport.SerializedDataOffset;
	Ar << ObjectExport.SerializedDataSize;

	if ( EnumHasAnyFlags( ObjectExport.ChunkObjectFlags, EChunkObjectFlags::Actor ) )
	{
		Ar << ObjectExport.ActorOwner;
		Ar << ObjectExport.ActorTransform;
	}
	return Ar;
}

FChunkSerializationContext::FChunkSerializationContext( FArchive& Ar, AOWGChunk* InChunk ) : FArchiveProxy( Ar ),
	RegionContainer( InChunk->GetOwnerRegionContainer() ), ChunkCoord( InChunk->GetChunkCoord() ), ChunkObject( InChunk )
{
}

FChunkSerializationContext::FChunkSerializationContext( FArchive& Ar, UOWGRegionContainer* InRegionContainer, FChunkCoord InChunkCoord ) : FArchiveProxy( Ar ),
	RegionContainer( InRegionContainer ), ChunkCoord( InChunkCoord )
{
}

FArchive& FChunkSerializationContext::operator<<( FWeakObjectPtr& Obj ) { return FArchiveUObject::SerializeWeakObjectPtr( *this, Obj ); }
FArchive& FChunkSerializationContext::operator<<( FSoftObjectPtr& Value ) { return FArchiveUObject::SerializeSoftObjectPtr( *this, Value ); }
FArchive& FChunkSerializationContext::operator<<( FSoftObjectPath& Value ) { return FArchiveUObject::SerializeSoftObjectPath( *this, Value ); }
FArchive& FChunkSerializationContext::operator<<( FObjectPtr& Obj ) { return FArchiveUObject::SerializeObjectPtr( *this, Obj ); }

void FChunkSerializationContext::SerializePackageSummary()
{
	if ( IsSaving() )
	{
		PackageSummaryOffset = Tell();
		PackageSummary.SetToLatest();
	}
	*this << PackageSummary;
}

void FChunkSerializationContext::SerializeCustomVersions()
{
	if ( IsSaving() )
	{
		PackageSummary.CustomVersionsOffset = Tell();

		int32 CustomVersionFormat = ECustomVersionSerializationFormat::Latest;
		FCustomVersionContainer LocalCustomVersionContainer = GetCustomVersions();

		*this << CustomVersionFormat;
		LocalCustomVersionContainer.Serialize( *this );
	}
	else if ( IsLoading() )
	{
		check( PackageSummary.CustomVersionsOffset != INDEX_NONE );
		Seek( PackageSummary.CustomVersionsOffset );

		int32 CustomVersionFormat = ECustomVersionSerializationFormat::Unknown;
		*this << CustomVersionFormat;

		FCustomVersionContainer LocalCustomVersionContainer;
		LocalCustomVersionContainer.Serialize( *this, static_cast<ECustomVersionSerializationFormat::Type>( CustomVersionFormat ) );
		SetCustomVersions( LocalCustomVersionContainer );
	}
}

void FChunkSerializationContext::SerializeNameMap()
{
	if ( IsSaving() )
	{
		PackageSummary.NameMapOffset = Tell();

		// Serialize name map in a compact format used by the linker
		int32 NameMapCount = NameMap.Num();
		*this << NameMapCount;
		
		for ( int32 i = 0; i < NameMap.Num(); i++ )
		{
			NameMap[i].GetDisplayNameEntry()->Write( *this );
		}
	}
	else if ( IsLoading() )
	{
		check( PackageSummary.NameMapOffset != INDEX_NONE );
		Seek( PackageSummary.NameMapOffset );

		// Serialize name map in a compact format used by the linker
		int32 NameMapCount = 0;
		*this << NameMapCount;

		check( NameMap.IsEmpty() );
		for ( int32 i = 0; i < NameMapCount; i++ )
		{
			FNameEntrySerialized NameEntry( ENAME_LinkerConstructor );
			*this << NameEntry;

			const FName ResultName( NameEntry );
			NameMap.Add( ResultName );
			NameLookupMap.Add( ResultName, i );
		}
	}
}

FArchive& FChunkSerializationContext::operator<<( FName& Value )
{
	if ( IsSaving() )
	{
		// Name map never contains name numbers, they are serialized separately - so strip one
		const FName NameWithoutNumber = FName( Value, 0 );
		int32 NameNumber = Value.GetNumber();
		
		if ( int32* ExistingNameIndex = NameLookupMap.Find( NameWithoutNumber ) )
		{
			*this << *ExistingNameIndex;
			*this << NameNumber;
			return *this;
		}
		int32 NewNameIndex = NameMap.Add( NameWithoutNumber );
		NameLookupMap.Add( NameWithoutNumber, NewNameIndex );

		*this << NewNameIndex;
		*this << NameNumber;
		return *this;
	}
	if ( IsLoading() )
	{
		int32 NameMapIndex = INDEX_NONE;
		int32 NameNumber = NAME_NO_NUMBER_INTERNAL;
		*this << NameMapIndex;
		*this << NameNumber;

		// Names in the name map are never numbered, so we need to construct a new name with a correct number
		check( NameMap.IsValidIndex( NameMapIndex ) );
		Value = FName( NameMap[ NameMapIndex ], NameNumber );
		return *this;
	}
	return *this;
}

int32 FChunkSerializationContext::WriteImport( UObject* Object )
{
	if ( const int32* ExistingImportIndex = ImportIndexMap.Find( Object ) )
	{
		return *ExistingImportIndex;
	}
	const int32 NewImportIndex = ImportMap.Add( FChunkObjectImport( Object ) );
	ImportIndexMap.Add( Object, NewImportIndex );

	// Sanity check map package references
	const bool bIsMapPackage = EnumHasAnyFlags( ImportMap[ NewImportIndex ].ChunkObjectFlags, EChunkObjectFlags::MapPackage );
	checkf( !bIsMapPackage || Object == RegionContainer->GetWorld(), TEXT("Cannot Import MapPackage '%s' that is different from the current World '%s'"),
		*Object->GetPackage()->GetName(), *RegionContainer->GetWorld()->GetPackage()->GetName() );

	// Sanity check chunk object references. We should never attempt to import objects from other chunks
	const bool bIsChunkReference = EnumHasAnyFlags( ImportMap[ NewImportIndex ].ChunkObjectFlags, EChunkObjectFlags::Chunk );
	checkf( !bIsChunkReference, TEXT("Illegal reference to external Chunk object '%s' while serializing Chunk '%s'"),
		*Object->GetName(), *ChunkObject->GetName() );

	// Sanity check region container references. We should never attempt to import region containers other than our own.
	const bool bIsRegionContainer = EnumHasAnyFlags( ImportMap[ NewImportIndex ].ChunkObjectFlags, EChunkObjectFlags::RegionContainer );
	checkf( !bIsRegionContainer || Object == RegionContainer, TEXT("Cannot Import Region Container '%s' that is different from the current Chunk's Region Container '%s'"),
		*Object->GetName(), *RegionContainer->GetName() );

	// Make sure we are not attempting to reference a non-public object. Non-Public object references are okay in the context of our world
	const UWorld* WorldOfObjectBeingSerialized = Object->GetWorld();
	checkf( WorldOfObjectBeingSerialized == RegionContainer->GetWorld() || Object->HasAnyFlags( RF_Public ), TEXT("Illegal reference to Private object '%s' while serializing Chunk '%s'"),
		*Object->GetPathName(), *ChunkObject->GetName() );

	// Serialize outer once we've added ourselves to the map, unless we're a map package
	if ( !bIsMapPackage )
	{
		ImportMap[ NewImportIndex ].OuterIndex = WriteObject( Object->GetOuter() );
	}
	return NewImportIndex;
}

UObject* FChunkSerializationContext::ResolveImport( int32 ImportIndex )
{
	check( ImportMap.IsValidIndex( ImportIndex ) );
	FChunkObjectImport& ObjectImport = ImportMap[ ImportIndex ];

	// We do not support circular dependency resolution
	if ( ObjectImport.XObject || !ensure( !ObjectImport.bCurrentlyResolving ) )
	{
		return ObjectImport.XObject;
	}
	return CreateImport( ObjectImport );
}

UObject* FChunkSerializationContext::CreateImport( FChunkObjectImport& ObjectImport )
{
	// We do not support circular dependency resolution
	TGuardValue ScopedIsResolvingImport( ObjectImport.bCurrentlyResolving, true );

	// Resolve the class of the object we are trying to load
	UClass* ImportClass = LoadObject<UClass>( nullptr, *ObjectImport.ClassName.ToString(), nullptr, LOAD_None );
	if ( !ImportClass )
	{
		UE_LOG( LogChunkSerialization, Warning, TEXT("Failed to resolve Import Class '%s'"), *ObjectImport.ClassName.ToString() );
		return nullptr;
	}

	// Handle map package references/region container references first
	if ( EnumHasAnyFlags( ObjectImport.ChunkObjectFlags, EChunkObjectFlags::MapPackage | EChunkObjectFlags::RegionContainer ) )
	{
		check( ObjectImport.OuterIndex == 0 );
		check( ObjectImport.ObjectName == NAME_None );

		if ( EnumHasAnyFlags( ObjectImport.ChunkObjectFlags, EChunkObjectFlags::MapPackage ) )
		{
			ObjectImport.XObject = RegionContainer->GetWorld();
		}
		else
		{
			ObjectImport.XObject = RegionContainer;
		}
		check( ObjectImport.XObject->IsA( ImportClass ) );
		return ObjectImport.XObject;
	}

	// Sanity check against chunk imports. They are not supported
	check( !EnumHasAnyFlags( ObjectImport.ChunkObjectFlags, EChunkObjectFlags::Chunk ) );

	// If we have no outer object reference, we should be deserializing a package
	if ( ObjectImport.OuterIndex == 0 )
	{
		check( ImportClass->IsChildOf( UPackage::StaticClass() ) );
		const FString PackageName = ObjectImport.ObjectName.ToString();

		if ( UPackage* FoundPackage = FindPackage( nullptr, *PackageName ) )
		{
			return FoundPackage;
		}
		return LoadPackage( nullptr, *PackageName, LOAD_None );
	}

	// Resolve the outer object otherwise
	UObject* OuterObject = ResolveObject( ObjectImport.OuterIndex );
	if ( !OuterObject )
	{
		UE_LOG( LogChunkSerialization, Warning, TEXT("Failed to resolve Outer for Object Import '%s'"), *ObjectImport.ObjectName.ToString() );
		return nullptr;
	}

	// Attempt to find the object first (this lookup is really fast), and only load it in case we cannot find it
	if ( UObject* FoundObject = StaticFindObjectFast( ImportClass, OuterObject, ObjectImport.ObjectName ) )
	{
		return FoundObject;
	}
	// Fallback to loading the object
	return StaticLoadObject( ImportClass, OuterObject, *ObjectImport.ObjectName.ToString(), nullptr, LOAD_None );
}

int32 FChunkSerializationContext::WriteExport( UObject* Object )
{
	// Make sure we are not attempting to serialize exports that are pending kill
	check( IsValid( Object ) );
	if ( const int32* ExistingExportIndex = ExportIndexMap.Find( Object ) )
	{
		return *ExistingExportIndex;
	}
	const int32 NewExportIndex = ExportMap.Add( FChunkObjectExport( Object ) );
	ExportIndexMap.Add( Object, NewExportIndex );

	// Serialize outer and owner once we've added ourselves to the map
	ExportMap[ NewExportIndex ].OuterIndex = WriteObject( Object->GetOuter() );

	if ( const AActor* Actor = Cast<AActor>( Object ) )
	{
		ExportMap[ NewExportIndex ].ActorOwner = WriteObject( Actor->GetOwner() );
	}
	return NewExportIndex;
}

UObject* FChunkSerializationContext::ResolveExport( int32 ExportIndex )
{
	check( ExportMap.IsValidIndex( ExportIndex ) );
	FChunkObjectExport& ObjectExport = ExportMap[ ExportIndex ];

	// We do not support circular dependency resolution
	if ( ObjectExport.XObject || !ensure( !ObjectExport.bCurrentlyResolving ) )
	{
		return ObjectExport.XObject;
	}
	return CreateExport( ObjectExport );
}

UObject* FChunkSerializationContext::CreateExport( FChunkObjectExport& ObjectExport )
{
	// We do not support circular dependency resolution
	TGuardValue ScopedIsResolvingExport( ObjectExport.bCurrentlyResolving, true );

	// Resolve the class of the object we are trying to construct
	UClass* ExportClass = LoadObject<UClass>( nullptr, *ObjectExport.ClassName.ToString(), nullptr, LOAD_None );
	if ( !ExportClass )
	{
		UE_LOG( LogChunkSerialization, Warning, TEXT("Failed to resolve Objext Export class '%s'"), *ObjectExport.ClassName.ToString() );
		return nullptr;
	}

	// Resolve outer object. We should always have one for the exports.
	UObject* OuterObject = ResolveObject( ObjectExport.OuterIndex );
	if ( !OuterObject )
	{
		UE_LOG( LogChunkSerialization, Warning, TEXT("Failed to resolve Outer for Object Export '%s'"), *ObjectExport.ObjectName.ToString() );
		return nullptr;
	}

	// Never attempt to create class default objects or default subobjects. They should always already exist
	// Archetypes are okay to create.
	constexpr EObjectFlags DoNotCreateOnlyLoadObjectFlags = RF_ClassDefaultObject | RF_DefaultSubObject;

	// Attempt to resolve an existing object first
	if ( UObject* FoundObject = StaticFindObjectFast( ExportClass, OuterObject, ObjectExport.ObjectName ) )
	{
		// If the object in question is pending kill, move it to transient package to make space for a new object
		if ( !IsValid( FoundObject ) )
		{
			UE_LOG( LogChunkSerialization, Log, TEXT("Moving Stale Object '%s' to Transient Package from '%s' to avoid name clash with a deserialized object"),
				*FoundObject->GetName(), *GetPathNameSafe( FoundObject->GetOuter() ) );
			FoundObject->Rename( nullptr, GetTransientPackage(), REN_DoNotDirty | REN_DontCreateRedirectors );
		}
		else
		{
			// Found object should always be a default sub object or a CDO as well
			ensureMsgf( FoundObject->GetMaskedFlags( DoNotCreateOnlyLoadObjectFlags ) == ( ObjectExport.ObjectFlags & DoNotCreateOnlyLoadObjectFlags ),
				TEXT("Found Object '%s' is not a Default Sub Object, but was serialized as one"), *FoundObject->GetFullName() );

			check( IsValid( FoundObject ) );
			ObjectExport.XObject = FoundObject;
			return ObjectExport.XObject;
		}
	}

	// Sanity check, we should not attempt to go further if we failed to resolve an default sub object
	if ( ( ObjectExport.ObjectFlags & DoNotCreateOnlyLoadObjectFlags ) != 0 )
	{
		UE_LOG( LogChunkSerialization, Warning, TEXT("Failed to find Default Sub Object '%s' inside of Outer '%s'"), *ObjectExport.ObjectName.ToString(), *OuterObject->GetFullName() );
		return nullptr;
	}

	// Attempt to spawn the actor into the world first if the class represents an actor
	if ( ExportClass->IsChildOf( AActor::StaticClass() ) )
	{
		// Actors only support a small subset of object flags that can be set on them in runtime
		constexpr EObjectFlags AllowedOnActorObjectFlags = RF_Transactional | RF_TextExportTransient | RF_DuplicateTransient | RF_NonPIEDuplicateTransient;
		AActor* ActorOwner = Cast<AActor>( ResolveObject( ObjectExport.ActorOwner ) );
		
		FActorSpawnParameters ActorSpawnParameters{};
		ActorSpawnParameters.Owner = ActorOwner;
		ActorSpawnParameters.Name = ObjectExport.ObjectName;
		ActorSpawnParameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_Fatal;
		ActorSpawnParameters.ObjectFlags = (EObjectFlags) ( ObjectExport.ObjectFlags & AllowedOnActorObjectFlags );
		ActorSpawnParameters.bDeferConstruction = true;

		// We are okay with name clashes with other actors, we serialize a closed off object sub-graph and nobody else should keep references to our actors,
		// which means regardless of the actual actor names we should be able to resolve them
		ActorSpawnParameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;

		ObjectExport.XObject = RegionContainer->GetWorld()->SpawnActor( ExportClass, &ObjectExport.ActorTransform, ActorSpawnParameters );
		ObjectExport.bNeedsFinishSpawning = true;
		return ObjectExport.XObject;
	}

	// The object is not an actor, we need to directly construct the object ourselves
	// Do not allow setting RF_Standalone on loaded sub-objects. RF_Public is not really applicable either.
	constexpr EObjectFlags AllowedConstructObjectFlags = RF_Transactional | RF_ArchetypeObject | RF_TextExportTransient | RF_DuplicateTransient | RF_NonPIEDuplicateTransient;

	FStaticConstructObjectParameters ConstructObjectParameters{ ExportClass };
	ConstructObjectParameters.Outer = OuterObject;
	ConstructObjectParameters.Name = ObjectExport.ObjectName;
	ConstructObjectParameters.SetFlags = (EObjectFlags)ObjectExport.ObjectFlags & AllowedConstructObjectFlags;

	ObjectExport.XObject = StaticConstructObject_Internal( ConstructObjectParameters );
	return ObjectExport.XObject;
}

int32 FChunkSerializationContext::WriteObject( UObject* Object )
{
	// Null/transient objects are serialized as index 0
	if ( !IsValid( Object ) || Object->HasAnyFlags( RF_Transient ) )
	{
		return 0;
	}

	// Exported objects have indices > 0, export indices start at 0
	if ( ShouldExportObject( Object ) )
	{
		return WriteExport( Object ) + 1;
	}

	// Imported objects have indices < 0, import indices start at 0
	return -WriteImport( Object ) - 1;
}

UObject* FChunkSerializationContext::ResolveObject( int32 ObjectIndex )
{
	// Null/transient objects are serialized as index 0
	if ( ObjectIndex == 0 )
	{
		return nullptr;
	}
	// Exported objects have indices > 0, export indices start at 0
	if ( ObjectIndex > 0 )
	{
		return ResolveExport( ObjectIndex - 1 );
	}
	// Imported objects have indices < 0, import indices start at 0
	return ResolveImport( -ObjectIndex - 1 );
}

bool FChunkSerializationContext::ShouldExportObject( UObject* Object ) const
{
	// Exit early on null objects
	if ( Object == nullptr )
	{
		return false;
	}

	// Current chunk is always considered an export
	if ( Object == ChunkObject )
	{
		return true;
	}

	// To be considered an export, Actor should be directly or indirectly owned by the Chunk
	if ( const AActor* Actor = Cast<AActor>( Object ) )
	{
		const AActor* CurrentActorOwner = Actor->GetOwner();
		while ( CurrentActorOwner != nullptr && CurrentActorOwner != ChunkObject )
		{
			CurrentActorOwner = CurrentActorOwner->GetOwner();
		}
		return CurrentActorOwner == ChunkObject;
	}

	// To be considered an export, Actor Component should be owned by a serialized actor
	if ( const UActorComponent* ActorComponent = Cast<UActorComponent>( Object ) )
	{
		return ShouldExportObject( ActorComponent->GetOwner() );
	}

	// To be considered an export, Object should have an exported actor as it's outer
	return ShouldExportObject( Object->GetTypedOuter<AActor>() );
}

FArchive& FChunkSerializationContext::operator<<( UObject*& Value )
{
	if ( IsSaving() )
	{
		int32 ObjectIndex = WriteObject( Value );
		*this << ObjectIndex;
	}
	else if ( IsLoading() )
	{
		int32 ObjectIndex = 0;
		*this << ObjectIndex;
		Value = ResolveObject( ObjectIndex );
	}
	return *this;
}

void FChunkSerializationContext::SerializeImportMap()
{
	if ( IsSaving() )
	{
		PackageSummary.ImportMapOffset = Tell();

		int32 NumImports = ImportMap.Num();
		*this << NumImports;

		for ( int32 i = 0; i < ImportMap.Num(); i++ )
		{
			*this << ImportMap[i];
		}
	}
	else if ( IsLoading() )
	{
		check( PackageSummary.ImportMapOffset != INDEX_NONE );
		Seek( PackageSummary.ImportMapOffset );

		int32 NumImports = 0;
		*this << NumImports;

		check( ImportMap.IsEmpty() );
		for ( int32 i = 0; i < NumImports; i++ )
		{
			*this << ImportMap.AddDefaulted_GetRef();
		}
	}
}

void FChunkSerializationContext::SerializeExportMap()
{
	if ( IsSaving() )
	{
		PackageSummary.ExportMapOffset = Tell();

		int32 NumExports = ExportMap.Num();
		*this << NumExports;

		for ( int32 i = 0; i < ExportMap.Num(); i++ )
		{
			*this << ExportMap[i];
		}
	}
	else if ( IsLoading() )
	{
		check( PackageSummary.ExportMapOffset != INDEX_NONE );
		Seek( PackageSummary.ExportMapOffset );
		
		int32 NumExports = 0;
		*this << NumExports;

		check( ExportMap.IsEmpty() );
		for ( int32 i = 0; i < NumExports; i++ )
		{
			*this << ExportMap.AddDefaulted_GetRef();
		}
	}
}

void FChunkSerializationContext::SerializeExports()
{
	if ( IsSaving() )
	{
		// Serialize the root object first. It will bring the rest along with it
		check( IsValid( ChunkObject ) );
		PackageSummary.ChunkExportIndex = WriteExport( ChunkObject );

		// As we are serializing the objects, we will add new exports to the end of the array.
		for ( int32 i = 0; i < ExportMap.Num(); i++ )
		{
			check( IsValid( ExportMap[ i ].XObject ) );

			// Record the offset before the serialization and serialize the data
			ExportMap[ i ].SerializedDataOffset = Tell();
			ExportMap[ i ].XObject->Serialize( *this );

			// The exports array might have been re-allocated now to account for newly referenced objects, so make sure not to use the old reference
			const int32 NumDataBytes = (int32) Tell();
			check( NumDataBytes >= ExportMap[ i ].SerializedDataOffset );
			ExportMap[ i ].SerializedDataSize = NumDataBytes - ExportMap[ i ].SerializedDataOffset;
		}
	}
	else if ( IsLoading() )
	{
		check( PackageSummary.ChunkExportIndex != INDEX_NONE );
		ChunkObject = CastChecked<AOWGChunk>( ResolveExport( PackageSummary.ChunkExportIndex ) );
		ChunkObject->SetupChunk( RegionContainer, ChunkCoord );

		for ( int32 i = 0; i < ExportMap.Num(); i++ )
		{
			FChunkObjectExport& ObjectExport = ExportMap[ i ];

			check( ObjectExport.SerializedDataOffset != INDEX_NONE );
			Seek( ObjectExport.SerializedDataOffset );

			UObject* ResolvedExport = ResolveExport( i );
			check( IsValid( ResolvedExport ) );
			ResolvedExport->Serialize( *this );

			const int32 ReadDataSize = (int32) ( Tell() - ObjectExport.SerializedDataOffset );
			checkf( ObjectExport.SerializedDataSize == ReadDataSize, TEXT("Serial size mismatch: %d bytes read vs %d bytes written"), ReadDataSize, ObjectExport.SerializedDataSize );
		}
		ChunkObject->OnChunkLoaded();
	}
}

void FChunkSerializationContext::PatchUpPackageSummary()
{
	check( PackageSummaryOffset != INDEX_NONE );
	Seek( PackageSummaryOffset );
	*this << PackageSummary;
}

void FChunkSerializationContext::DispatchFinishSpawnOnExports()
{
	for ( FChunkObjectExport& ObjectExport : ExportMap )
	{
		AActor* Actor = Cast<AActor>( ObjectExport.XObject );
		if ( Actor != nullptr && ObjectExport.bNeedsFinishSpawning )
		{
			Actor->FinishSpawning( ObjectExport.ActorTransform, false );
			ObjectExport.bNeedsFinishSpawning = false;
		}
	}
}

AOWGChunk* FChunkSerializationContext::DeserializeChunk( UOWGRegionContainer* RegionContainer, FChunkCoord ChunkCoord, const TArray<uint8>& ChunkSerializedData, TFunctionRef<void(AOWGChunk*)> PostChunkLoaded )
{
	// TRACE_CPUPROFILER_EVENT_SCOPE(FChunkSerializationContext::DeserializeChunk);

	FMemoryReader MemoryReader( ChunkSerializedData, true );
	FChunkSerializationContext SerializationContext( MemoryReader, RegionContainer, ChunkCoord );

	AOWGChunk* LoadedChunk = SerializationContext.DoChunkDeserialize();
	PostChunkLoaded( LoadedChunk );
	SerializationContext.DispatchFinishSpawnOnExports();
	return LoadedChunk;
}

void FChunkSerializationContext::SerializeChunk( AOWGChunk* Chunk, TArray<uint8>& OutChunkSerializedData )
{
	// TRACE_CPUPROFILER_EVENT_SCOPE(FChunkSerializationContext::SerializeChunk);

	FMemoryWriter MemoryWriter( OutChunkSerializedData, true );
	FChunkSerializationContext SerializationContext( MemoryWriter, Chunk );

	SerializationContext.DoChunkSerialize();
}

AOWGChunk* FChunkSerializationContext::DoChunkDeserialize()
{
	// Read the package summary first
	SerializePackageSummary();

	// Read custom versions after that
	SerializeCustomVersions();

	// Read name map, it's needed to resolve imports and exports
	SerializeNameMap();

	// Read import and export maps
	SerializeImportMap();
	SerializeExportMap();

	// Serialize export data from the archive. This will actually trigger the instantiation of exports
	SerializeExports();

	return ChunkObject;
}

void FChunkSerializationContext::DoChunkSerialize()
{
	// Serialize initial package summary, it needs to be the first entry in the archive
	SerializePackageSummary();
	
	// Serialize exports, that will populate import and export maps
	SerializeExports();

	// Serialize import and export maps
	SerializeImportMap();
	SerializeExportMap();

	// Serialize name map
	SerializeNameMap();

	// Serialize custom versions
	SerializeCustomVersions();

	// Patch up the package summary with the new offsets
	PatchUpPackageSummary();
}
