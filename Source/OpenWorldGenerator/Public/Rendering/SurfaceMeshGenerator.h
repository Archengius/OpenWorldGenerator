// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"

class FChunkData2D;

namespace SurfaceMeshGenerator
{
	OPENWORLDGENERATOR_API void GenerateChunkSurfaceMesh( UE::Geometry::FDynamicMesh3& DynamicMesh, float SurfaceSizeWorldUnits, const FChunkData2D& LandscapeHeightMap, const FChunkData2D& NormalMap, const FChunkData2D& BiomeMap, int32 LODIndex = 0 );
}
