// Copyright Nikita Zolotukhin. All Rights Reserved.

#include "Partition/OWGRegionContainer.h"
#include "OpenWorldGeneratorSettings.h"
#include "Engine/World.h"
#include "Partition/OWGChunk.h"
#include "Partition/OWGChunkSerialization.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace RegionFileFormatConstants
{
	// Compression format to use for region files. Changes to this field are backwards compatible!
	static const FName RegionCompressionFormat = NAME_LZ4;

	// Magic number used in region files. Changes are not backwards compatible. Satisfactory [Open] World Generator Region [File] = "SWGR" in ASCII
	static constexpr int32 RegionFileFormatMagic = 0x52475753;
}

AOWGChunk* UOWGRegionContainer::FindChunk( FChunkCoord ChunkCoord ) const
{
	if ( AOWGChunk* const* ExistingLoadedChunk = LoadedChunks.Find( ChunkCoord ) )
	{
		check( IsValid( *ExistingLoadedChunk ) );
		return *ExistingLoadedChunk;
	}
	return nullptr;
}

AOWGChunk* UOWGRegionContainer::LoadChunk( FChunkCoord ChunkCoord )
{
	if ( AOWGChunk* LoadedChunk = FindChunk( ChunkCoord ) )
	{
		return LoadedChunk;
	}

	if ( const TArray<uint8>* SerializedData = SerializedChunkData.Find( ChunkCoord ) )
	{
		// Add chunk to the LoadedChunks array before we dispatch BeginPlay on it so that it is fully initialized by the time all the relevant actors are fully spawned
		AOWGChunk* LoadedChunk = FChunkSerializationContext::DeserializeChunk( this, ChunkCoord, *SerializedData, [this, ChunkCoord]( AOWGChunk* TempChunk )
		{
			check( IsValid( TempChunk ) );
			LoadedChunks.Add( ChunkCoord, TempChunk );
		} );
		SerializedChunkData.Remove( ChunkCoord );
		check( IsValid( LoadedChunk ) );

		return LoadedChunk;
	}
	return nullptr;
}

AOWGChunk* UOWGRegionContainer::LoadOrCreateChunk( FChunkCoord ChunkCoord )
{
	if ( AOWGChunk* LoadedChunk = LoadChunk( ChunkCoord ) )
	{
		return LoadedChunk;
	}

	// Only attempt to generate chunks if they are within the valid section bounds
	if ( ensure( ChunkCoord.ToRegionCoord() == RegionCoord ) )
	{
		FActorSpawnParameters SpawnParameters{};
		SpawnParameters.Name = *FString::Printf( TEXT("OWGChunk_X%d_Y%d"), ChunkCoord.PosX, ChunkCoord.PosY );

		SpawnParameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
		SpawnParameters.bDeferConstruction = true;

		const UOpenWorldGeneratorSettings* OpenWorldGeneratorSettings = UOpenWorldGeneratorSettings::Get();
		AOWGChunk* NewChunk = GetWorld()->SpawnActor<AOWGChunk>( OpenWorldGeneratorSettings->ChunkClass.LoadSynchronous(), ChunkCoord.ToOriginWorldLocation(), FRotator{}, SpawnParameters );
		NewChunk->SetupChunk( this, ChunkCoord );
		NewChunk->OnChunkCreated();

		check( IsValid( NewChunk ) );
		LoadedChunks.Add( ChunkCoord, NewChunk );
		NewChunk->FinishSpawning( FTransform::Identity, true );
		check( IsValid( NewChunk ) );

		return NewChunk;
	}
	return nullptr;
}

void UOWGRegionContainer::UnloadChunk( FChunkCoord ChunkCoord )
{
	if ( AOWGChunk* LoadedChunk = FindChunk( ChunkCoord ) )
	{
		// Notify the chunk that we are about to serialize and then immediately unload it
		LoadedChunk->OnChunkAboutToBeUnloaded();

		// Serialize the chunk
		TArray<uint8> SerializedData;
		FChunkSerializationContext::SerializeChunk( LoadedChunk, SerializedData );

		// Add data to the serialized data array, destroy and remove chunk
		SerializedChunkData.Add( ChunkCoord, MoveTemp( SerializedData ) );
		LoadedChunk->Destroy();
		LoadedChunks.Remove( ChunkCoord );
	}
}

bool UOWGRegionContainer::ChunkExists( FChunkCoord ChunkCoord ) const
{
	return FindChunk( ChunkCoord ) != nullptr || SerializedChunkData.Contains( ChunkCoord );
}

void UOWGRegionContainer::SetRegionCoord( const FChunkCoord& NewRegionCoord )
{
	RegionCoord = NewRegionCoord;
}

TArray<FChunkCoord> UOWGRegionContainer::GetLoadedChunkCoords() const
{
	TArray<FChunkCoord> AllChunkCommands;
	LoadedChunks.GenerateKeyArray( AllChunkCommands );
	return AllChunkCommands;
}

void UOWGRegionContainer::NotifyChunkDestroyed( const AOWGChunk* Chunk )
{
	const FChunkCoord ChunkCoord = Chunk->GetChunkCoord();
	if ( ensure( LoadedChunks.Contains( ChunkCoord ) ) )
	{
		check( LoadedChunks.FindChecked( ChunkCoord ) == Chunk );
		LoadedChunks.Remove( ChunkCoord );
	}
}

void UOWGRegionContainer::SerializeRegionContainerToFile(FArchive& Ar)
{
	TArray<uint8> SerializedUncompressedData;
	FMemoryWriter InnerWriter( SerializedUncompressedData, true );

	// Collect all coordinates for all chunks we have loaded or serialized
	TArray<FChunkCoord> AllChunkCoords;
	SerializedChunkData.GenerateKeyArray( AllChunkCoords );
	{
		TArray<FChunkCoord> RuntimeChunkCoords;
		LoadedChunks.GenerateKeyArray( RuntimeChunkCoords );
		AllChunkCoords.Append( RuntimeChunkCoords );
	}
	// Stable sort using less operator
	AllChunkCoords.StableSort();

	// Serialize chunks in a consistent order
	for ( FChunkCoord& ChunkCoord : AllChunkCoords )
	{
		if ( AOWGChunk* LoadedChunk = FindChunk( ChunkCoord ) )
		{
			TArray<uint8> SerializedData;
			FChunkSerializationContext::SerializeChunk( LoadedChunk, SerializedData );

			int32 ChunkSerializedDataSize = SerializedData.Num();
			InnerWriter << ChunkSerializedDataSize;
			InnerWriter.Serialize( SerializedData.GetData(), ChunkSerializedDataSize );
		}
		else if ( TArray<uint8>* SerializedData = SerializedChunkData.Find( ChunkCoord ) )
		{
			int32 ChunkSerializedDataSize = SerializedData->Num();
			InnerWriter << ChunkSerializedDataSize;
			InnerWriter.Serialize( SerializedData->GetData(), ChunkSerializedDataSize );
		}
	}
	FString CompressionFormat = RegionFileFormatConstants::RegionCompressionFormat.ToString();

	// Compress data
	TArray<uint8> CompressedSerializedData;
	const int32 MaxCompressedDataSize = FCompression::CompressMemoryBound( *CompressionFormat, SerializedUncompressedData.Num() );
	CompressedSerializedData.AddZeroed( MaxCompressedDataSize );

	int32 ResultCompressedSize = 0;
	const bool bCompressionSuccess = FCompression::CompressMemory( *CompressionFormat, CompressedSerializedData.GetData(), ResultCompressedSize,
		SerializedUncompressedData.GetData(), SerializedUncompressedData.Num() );
	checkf( bCompressionSuccess, TEXT("Failed to compress Region file using Compression Format '%s'"), *CompressionFormat );

	int32 UncompressedSize = SerializedUncompressedData.Num();
	
	// Write the data wrapped inside of the envelope

	// Write consistent file magic first
	int32 FileFormatMagic = RegionFileFormatConstants::RegionFileFormatMagic;
	Ar << FileFormatMagic;

	// Write current container file version
	ERegionContainerVersion RegionContainerVersion = ERegionContainerVersion::Latest;
	Ar << RegionContainerVersion;

	// Write number of chunks and their coordinates. This is used to determine the contents of the region file without decompressing it first
	int32 ChunkCount = AllChunkCoords.Num();
	Ar << ChunkCount;
	for (FChunkCoord& ChunkCoord : AllChunkCoords)
	{
		Ar << ChunkCoord;
	}

	// Write compressed and uncompressed data size and the compression format
	Ar << UncompressedSize;
	Ar << ResultCompressedSize;
	Ar << CompressionFormat;

	// And then write the compressed chunk data
	Ar.Serialize( CompressedSerializedData.GetData(), ResultCompressedSize );
}

bool UOWGRegionContainer::ReadRegionContainerChunkListFromFile(FArchive& Ar, TArray<FChunkCoord>& OutChunkList)
{
	// Verify file magic before we attempt to read anything
	int32 FileFormatMagic = 0;
	Ar << FileFormatMagic;
	if ( FileFormatMagic != RegionFileFormatConstants::RegionFileFormatMagic )
	{
		return false;
	}

	// Read the version of the container, and verify the version
	ERegionContainerVersion RegionContainerVersion{};
	Ar << RegionContainerVersion;
	if (Ar.IsError() || RegionContainerVersion > ERegionContainerVersion::Latest)
	{
		return false;
	}

	// Read list of chunks and their coordinates
	int32 ChunkCount = 0;
	Ar << ChunkCount;

	OutChunkList.SetNumZeroed(ChunkCount);
	for (int32 i = 0; i < ChunkCount; i++)
	{
		Ar << OutChunkList[i];
	}
	return true;
}

void UOWGRegionContainer::LoadRegionContainerFromFile(FArchive& Ar)
{
	// Verify file magic before we attempt to read anything
	int32 FileFormatMagic = 0;
	Ar << FileFormatMagic;

	if ( FileFormatMagic != RegionFileFormatConstants::RegionFileFormatMagic )
	{
		UE_LOG( LogChunkSerialization, Warning,	TEXT("Refusing to load region container file for Region '%s' because of the File Magic mismatch"), *GetName() );
		return;
	}

	// Read the version of the container, and verify the version
	ERegionContainerVersion RegionContainerVersion{};
	Ar << RegionContainerVersion;
	if (Ar.IsError() || RegionContainerVersion > ERegionContainerVersion::Latest)
	{
		UE_LOG( LogChunkSerialization, Warning,	TEXT("Refusing to load region container file for Region '%s' because of the invalid version"), *GetName() );
		return;
	}

	// Read list of chunks and their coordinates
	int32 ChunkCount = 0;
	Ar << ChunkCount;

	TArray<FChunkCoord> AllChunkCoords;
	AllChunkCoords.SetNumZeroed(ChunkCount);
	for (int32 i = 0; i < ChunkCount; i++)
	{
		Ar << AllChunkCoords[i];
	}

	// Read envelope data
	int32 UncompressedSize = 0;
	Ar << UncompressedSize;
	int32 CompressedSize = 0;
	Ar << CompressedSize;
	FString CompressionFormat;
	Ar << CompressionFormat;

	TArray<uint8> CompressedData;
	CompressedData.AddZeroed( CompressedSize );
	Ar.Serialize( CompressedData.GetData(), CompressedSize );

	TArray<uint8> UncompressedData;
	UncompressedData.AddZeroed( UncompressedSize );

	const bool bDecompressionSuccess = FCompression::UncompressMemory( *CompressionFormat, UncompressedData.GetData(), UncompressedSize, CompressedData.GetData(), CompressedSize );
	checkf( bDecompressionSuccess, TEXT("Failed to decompress Region file using Compression Format '%s'"), *CompressionFormat );

	FMemoryReader InnerReader( UncompressedData );

	// Deserialize chunk blobs from the decompressed data
	for ( int32 i = 0; i < ChunkCount; i++ )
	{
		FChunkCoord ChunkCoord = AllChunkCoords[i];
		int32 ChunkSerializedDataSize = 0;
		InnerReader << ChunkSerializedDataSize;
		
		TArray<uint8> SerializedChunkDataArray;
		SerializedChunkDataArray.AddZeroed( ChunkSerializedDataSize );
		InnerReader.Serialize( SerializedChunkDataArray.GetData(), ChunkSerializedDataSize );

		SerializedChunkData.Add( ChunkCoord, MoveTemp( SerializedChunkDataArray ) );
	}
}
