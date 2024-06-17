// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Async/TaskGraphFwd.h"
#include "DynamicMesh/DynamicMesh3.h"

class FChunkData2D;
class AOWGChunk;
class FReferenceCollector;

class OPENWORLDGENERATOR_API FChunkLandscapeMeshManager : public FNoncopyable
{
public:
	explicit FChunkLandscapeMeshManager( AOWGChunk* InChunk );
	
	/** Invalidates the currently generated landscape mesh and schedules it's regeneration in background */
	void InvalidateLandscapeMesh();

	/** Called when the chunk LOD level changes */
	void OnChunkLODLevelChanged();
	
	/** Completely regenerates the landscape mesh from the heightmap for the given LOD. If LOD is -1, it will regenerate All LODs. If bAsync is false, the meshes will be generated on the main thread. */
	void RebuildLandscapeMesh( int32 LODIndex = -1, bool bBlocking = false );

	/** Updates the landscape mesh to the new LOD level */
	void ForceUpdateLandscapeMesh( int32 NewMeshLODIndex );

	void AddReferencedObjects( FReferenceCollector& ReferenceCollector );
private:
	friend class FAsyncLODGenerationTask;

	void RebuildLandscapeMeshLODAsync( int32 LODIndex );
	void RebuildLandscapeMeshLODBlocking( int32 LODIndex );

	void OnLandscapeMeshLODRebuilt( int32 LODIndex, int32 ChangelistNumber, UE::Geometry::FDynamicMesh3& GeneratedMesh );

	/** Generates a landscape LOD mesh. Can be called off the main thread */
	static void GenerateLandscapeLODInternal( UE::Geometry::FDynamicMesh3& OutLandscapeMesh, int32 LODIndex, const FChunkData2D& HeightData, const FChunkData2D& BiomeMap, const FChunkData2D& NormalData );
protected:
	/** The chunk owning this material manager */
	AOWGChunk* OwnerChunk{};

	/** Meshes used for rendering landscape at various distances, mapped to their current changelist number */
	TArray<TPair<UE::Geometry::FDynamicMesh3, int32>> LandscapeLODMeshes;

	/** Async tasks currently generating LOD meshes. Can be waited on if needed */
	TArray<TPair<FGraphEventRef, int32>> AsyncLandscapeLODGenerationTasks;

	/** Current change number of the landscape data. If a landscape LOD mesh has a lower number, it is outdated */
	int32 CurrentLandscapeChangeNumber{0};

	/** Currently active landscape mesh LOD index and it's changelist */
	TPair<int32, int32> CurrentLandscapeLODMesh{TPair<int32, int32>( INDEX_NONE, INDEX_NONE )};
};
