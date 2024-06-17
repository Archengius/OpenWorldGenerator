// Copyright Nikita Zolotukhin. All Rights Reserved.

#include "Partition/ChunkLandscapeMeshManager.h"
#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "Components/DynamicMeshComponent.h"
#include "Partition/OWGChunk.h"
#include "Rendering/SurfaceMeshGenerator.h"

DECLARE_CYCLE_STAT( TEXT("Async Chunk Landscape LOD generation"), STAT_AsyncChunkLandscapeLODs, STATGROUP_Game );
DECLARE_CYCLE_STAT( TEXT("Blocking Chunk Landscape LOD generation"), STAT_BlockingChunkLandscapeLODs, STATGROUP_Game );
DECLARE_CYCLE_STAT( TEXT("Edit Chunk Landscape Mesh Component"), STAT_EditChunkLandscapeMeshComponent, STATGROUP_Game );

FChunkLandscapeMeshManager::FChunkLandscapeMeshManager( AOWGChunk* InChunk ) : OwnerChunk( InChunk )
{
}

void FChunkLandscapeMeshManager::OnChunkLODLevelChanged()
{
	const int32 NewChunkLODIndex = OwnerChunk->GetCurrentChunkLOD();

	// Directly swap out the mesh with the new LOD variant when we already have a mesh for it generated.
	if ( LandscapeLODMeshes.IsValidIndex( NewChunkLODIndex ) && LandscapeLODMeshes[ NewChunkLODIndex ].Key.VertexCount() > 0 )
	{
		ForceUpdateLandscapeMesh( NewChunkLODIndex );
	}
	// Otherwise, we need to generate a new LOD variant before we can assign it to the mesh component. Prefer to do that async.
	else if ( NewChunkLODIndex >= 0 && NewChunkLODIndex < OwnerChunk->NumChunkLandscapeLODs )
	{
		RebuildLandscapeMesh( NewChunkLODIndex, false );
	}
}

void FChunkLandscapeMeshManager::InvalidateLandscapeMesh()
{
	CurrentLandscapeChangeNumber++;
	const int32 CurrentChunkLOD = OwnerChunk->GetCurrentChunkLOD();

	// If we have a currently active LOD, start rebuilding it in the background
	if ( CurrentLandscapeLODMesh.Key != INDEX_NONE && CurrentLandscapeLODMesh.Key >= 0 && CurrentLandscapeLODMesh.Key < OwnerChunk->NumChunkLandscapeLODs )
	{
		RebuildLandscapeMesh( CurrentLandscapeLODMesh.Key, false );
	}
	// Also do that if we do not have an active LOD mesh but have an active chunk LOD
	else if ( CurrentChunkLOD != INDEX_NONE && CurrentChunkLOD >= 0 && CurrentChunkLOD < OwnerChunk->NumChunkLandscapeLODs )
	{
		RebuildLandscapeMesh( CurrentChunkLOD, false );
	}
}

void FChunkLandscapeMeshManager::ForceUpdateLandscapeMesh( int32 NewMeshLODIndex )
{
	SCOPE_CYCLE_COUNTER( STAT_EditChunkLandscapeMeshComponent );
	LandscapeLODMeshes.SetNum( OwnerChunk->NumChunkLandscapeLODs );

	// Capture a copy of the current mesh before we swap it otu so we can return it to the pool
	const auto CurrentLandscapeMeshLODAndChangelist = CurrentLandscapeLODMesh;
	
	if ( OwnerChunk->LandscapeMeshComponent )
	{
		OwnerChunk->LandscapeMeshComponent->EditMesh( [NewMeshLODIndex, CurrentLandscapeMeshLODAndChangelist, this]( UE::Geometry::FDynamicMesh3& DynamicMesh )
		{
			// Move the old LOD mesh data back into the LOD mesh cache array (unless there is a newer mesh there)
			if ( LandscapeLODMeshes.IsValidIndex( CurrentLandscapeMeshLODAndChangelist.Key ) &&
				LandscapeLODMeshes[ CurrentLandscapeMeshLODAndChangelist.Key ].Value <= CurrentLandscapeMeshLODAndChangelist.Value )
			{
				LandscapeLODMeshes[ CurrentLandscapeMeshLODAndChangelist.Key ].Key = MoveTemp( DynamicMesh );
				LandscapeLODMeshes[ CurrentLandscapeMeshLODAndChangelist.Key ].Value = CurrentLandscapeMeshLODAndChangelist.Value;
			}
			
			// Move the new mesh into the mesh component
			if ( LandscapeLODMeshes.IsValidIndex( NewMeshLODIndex ) )
			{
				DynamicMesh = MoveTemp( LandscapeLODMeshes[ NewMeshLODIndex ].Key );
			}
		}, EDynamicMeshComponentRenderUpdateMode::FastUpdate );
	}

	// Swap out the current mesh index and changelist with the new ones
	CurrentLandscapeLODMesh.Key = NewMeshLODIndex;
	CurrentLandscapeLODMesh.Value = LandscapeLODMeshes[ NewMeshLODIndex ].Value;
}

void FChunkLandscapeMeshManager::AddReferencedObjects( FReferenceCollector& ReferenceCollector )
{
	ReferenceCollector.AddReferencedObject( OwnerChunk );
}

void FChunkLandscapeMeshManager::OnLandscapeMeshLODRebuilt( int32 LODIndex, int32 ChangelistNumber, UE::Geometry::FDynamicMesh3& GeneratedMesh )
{
	// By the time this happens, we might already have a mesh that is more up to date than this task's generated one
	// So we need to make sure that the mesh we are trying to swap to is even a valid mesh
	LandscapeLODMeshes.SetNum( OwnerChunk->NumChunkLandscapeLODs );

	auto& LandscapeMeshPair = LandscapeLODMeshes[ LODIndex ];
	if ( ChangelistNumber < LandscapeMeshPair.Value )
	{
		return;
	}

	// Update the current mesh and the changelist number
	LandscapeMeshPair.Key = MoveTemp( GeneratedMesh );
	LandscapeMeshPair.Value = ChangelistNumber;

	// If the LOD mesh we have currently generated is now active, swap the current one with the freshly generated version
	if ( CurrentLandscapeLODMesh.Key == LODIndex && CurrentLandscapeLODMesh.Value <= ChangelistNumber )
	{
		ForceUpdateLandscapeMesh( LODIndex );
	}
	// Also forcefully update the mesh if this is the mesh we want to be active, but it wasn't available in time to activate it
	else if ( OwnerChunk->GetCurrentChunkLOD() == LODIndex )
	{
		ForceUpdateLandscapeMesh( LODIndex );
	}
}

void FChunkLandscapeMeshManager::GenerateLandscapeLODInternal( UE::Geometry::FDynamicMesh3& OutLandscapeMesh, int32 LODIndex, const FChunkData2D& HeightData, const FChunkData2D& NormalData, const FChunkData2D& BiomeMap )
{
	// Only generate one particular LOD
	SurfaceMeshGenerator::GenerateChunkSurfaceMesh( OutLandscapeMesh, FChunkCoord::ChunkSizeWorldUnits, HeightData, NormalData, BiomeMap, LODIndex );
}

/** Data for the async chunk landscape mesh generation task */
class FAsyncLODGenerationTask : public FCustomStatIDGraphTaskBase
{
	TWeakObjectPtr<AOWGChunk> Chunk;
	int32 LODIndex{INDEX_NONE};
	FChunkData2D HeightmapData;
	FChunkData2D NormalData;
	FChunkData2D BiomeData;
	int32 ChangelistNumber{INDEX_NONE};
public:
	FAsyncLODGenerationTask( FChunkLandscapeMeshManager* MeshManager, const FChunkData2D& InHeightMapData, const FChunkData2D& InNormalData, const FChunkData2D& InBiomeData, int32 InLODIndex ) : FCustomStatIDGraphTaskBase( GET_STATID( STAT_AsyncChunkLandscapeLODs ) ), Chunk( MeshManager->OwnerChunk ), LODIndex( InLODIndex )
	{
		HeightmapData = InHeightMapData;
		NormalData = InNormalData;
		BiomeData = InBiomeData;
		ChangelistNumber = MeshManager->CurrentLandscapeChangeNumber;
	}

	static ESubsequentsMode::Type GetSubsequentsMode()
	{
		// We might end up waiting for this task, so we need to track it
		return ESubsequentsMode::TrackSubsequents;
	}

	ENamedThreads::Type GetDesiredThread()
	{
		return ENamedThreads::AnyThread;
	}

	void DoTask( ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent )
	{
		TSharedPtr<UE::Geometry::FDynamicMesh3> LODMesh = MakeShared< UE::Geometry::FDynamicMesh3>();
		FChunkLandscapeMeshManager::GenerateLandscapeLODInternal( *LODMesh, LODIndex, HeightmapData, NormalData, BiomeData );

		const TWeakObjectPtr<AOWGChunk> WeakChunk = Chunk;
		const int32 LocalLODIndex = LODIndex;
		const int32 LocalChangelistNumber = ChangelistNumber;
		
		AsyncTask( ENamedThreads::GameThread, [LODMesh, LocalLODIndex, LocalChangelistNumber, WeakChunk]
		{
			if ( const AOWGChunk* LoadedChunk = WeakChunk.Get() )
			{
				LoadedChunk->GetLandscapeMeshManager()->OnLandscapeMeshLODRebuilt( LocalLODIndex, LocalChangelistNumber, *LODMesh );
			}
		} );
	}
};

void FChunkLandscapeMeshManager::RebuildLandscapeMeshLODAsync( int32 LODIndex )
{
	const FChunkData2D& HeightmapData = OwnerChunk->ChunkData2D.FindChecked( ChunkDataID::SurfaceHeightmap );
	const FChunkData2D& NormalData = OwnerChunk->ChunkData2D.FindChecked( ChunkDataID::SurfaceNormal );
	const FChunkData2D& BiomeData = OwnerChunk->ChunkData2D.FindChecked( ChunkDataID::BiomeMap );

	FGraphEventRef GraphEvent = TGraphTask<FAsyncLODGenerationTask>::CreateTask().ConstructAndDispatchWhenReady( this, HeightmapData, NormalData, BiomeData, LODIndex );

	AsyncLandscapeLODGenerationTasks.SetNum( OwnerChunk->NumChunkLandscapeLODs );
	AsyncLandscapeLODGenerationTasks[ LODIndex ] = { GraphEvent, CurrentLandscapeChangeNumber };
}

void FChunkLandscapeMeshManager::RebuildLandscapeMeshLODBlocking( int32 LODIndex )
{
	SCOPE_CYCLE_COUNTER( STAT_BlockingChunkLandscapeLODs );
	
	const FChunkData2D& HeightmapData = OwnerChunk->ChunkData2D.FindChecked( ChunkDataID::SurfaceHeightmap );
	const FChunkData2D& NormalData = OwnerChunk->ChunkData2D.FindChecked( ChunkDataID::SurfaceNormal );
	const FChunkData2D& BiomeData = OwnerChunk->ChunkData2D.FindChecked( ChunkDataID::BiomeMap );

	UE::Geometry::FDynamicMesh3 LODMesh;
	GenerateLandscapeLODInternal( LODMesh, LODIndex, HeightmapData, NormalData, BiomeData );
	OnLandscapeMeshLODRebuilt( LODIndex, CurrentLandscapeChangeNumber, LODMesh );
}

void FChunkLandscapeMeshManager::RebuildLandscapeMesh( int32 LODIndex, bool bBlocking )
{
	// Do not attempt to generate meshes until we have valid surface data
	if ( !OwnerChunk->ChunkData2D.Contains( ChunkDataID::SurfaceHeightmap ) )
	{
		return;
	}

	// Check if the current mesh is already up to date, and exit immediately if it is
	if ( LandscapeLODMeshes.IsValidIndex( LODIndex ) )
	{
		const auto& LandscapeLODMesh = LandscapeLODMeshes[ LODIndex ];
		if ( LandscapeLODMesh.Key.VertexCount() > 0 && LandscapeLODMesh.Value == CurrentLandscapeChangeNumber )
		{
			return;
		}
	}

	// Check if we already have an async task for that LOD with a matching changelist number
	if ( AsyncLandscapeLODGenerationTasks.IsValidIndex( LODIndex ) )
	{
		const auto& LandscapeLODTaskPair = AsyncLandscapeLODGenerationTasks[ LODIndex ];
		if ( LandscapeLODTaskPair.Key.IsValid() && LandscapeLODTaskPair.Value == CurrentLandscapeChangeNumber )
		{
			// If we want to wait for the event now (e.g. we want a blocking LOD), wait for it, otherwise just exit since the task is already processing
			if ( bBlocking )
			{
				LandscapeLODTaskPair.Key->Wait();
			}
			return;
		}
	}

	// Generate the mesh now, either by starting an async task or doing it sync
	if ( bBlocking )
	{
		RebuildLandscapeMeshLODBlocking( LODIndex );
	}
	else
	{
		RebuildLandscapeMeshLODAsync( LODIndex );
	}
}
