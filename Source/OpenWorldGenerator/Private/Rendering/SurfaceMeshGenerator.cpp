// Copyright Nikita Zolotukhin. All Rights Reserved.

#include "Rendering/SurfaceMeshGenerator.h"

#include "OpenWorldGeneratorModule.h"
#include "DynamicMesh/MeshNormals.h"
#include "Generation/OWGBiome.h"
#include "Generators/SphereGenerator.h"
#include "Operations/MeshBoolean.h"
#include "Operations/UniformTessellate.h"
#include "Partition/ChunkData2D.h"

THIRD_PARTY_INCLUDES_START
#include "FastNoise/Generators/Perlin.h"
#include "FastNoise/FastNoise.h"
THIRD_PARTY_INCLUDES_END

#include "Generators/MarchingCubes.h"
#include "Implicit/GridInterpolant.h"

#define SURFACE_DATA_INDEX(PointX, PointY, NumPoints, MeshScale) ( (NumPoints / 2) * (MeshScale) ) * ( (MeshScale) * (PointY) + (int32) ((PointY) >= (NumPoints / 2) / 2) * ((MeshScale) - 1) ) + ( (PointX) * (MeshScale) + (int32) ((PointX) >= (NumPoints / 2) / 2) * ((MeshScale) - 1) )
#define MESH_POINT_INDEX(PointX, PointY, NumPoints) (NumPoints) * (PointY) + (PointX)

struct FCheckedFloatArray
{
private:
	const float* Data{};
	int32 ElementCount{0};
public:
	explicit FCheckedFloatArray( const FChunkData2D& InData )
	{
		Data = InData.GetDataPtr<float>();
		ElementCount = InData.GetSurfaceElementCount();
	}

	float operator[]( int32 ElementIndex ) const
	{
		check( ElementIndex >= 0 && ElementIndex < ElementCount );
		return Data[ ElementIndex ];
	}
};

class FIndexedFloatArrayGrid
{
	const float* Data{nullptr};
	UE::Geometry::FVector3i Dimensions{};
public:
	FIndexedFloatArrayGrid( const float* InData, const UE::Geometry::FVector3i InDimensions ) : Data( InData ), Dimensions( InDimensions )
	{
	}

	FORCEINLINE float GetValue( const UE::Geometry::FVector3i& InPosition ) const
	{
		return FMath::Sign( Data[ InPosition.Z * Dimensions.X * Dimensions.Y + InPosition.Y * Dimensions.X + InPosition.X ] );
	}
};

void SurfaceMeshGenerator::GenerateChunkSurfaceMesh( UE::Geometry::FDynamicMesh3& DynamicMesh, float SurfaceSizeWorldUnits, const FChunkData2D& LandscapeHeightMap, const FChunkData2D& NormalMap, const FChunkData2D& BiomeMap, int32 LODIndex )
{
	DynamicMesh.Clear();

	// Enable Material IDs and Vertex Colors. We do not need triangle groups because we are generating a single surface.
	DynamicMesh.EnableVertexNormals( FVector3f::UpVector );
	DynamicMesh.EnableAttributes();
	DynamicMesh.Attributes()->EnableMaterialID();
	DynamicMesh.Attributes()->EnablePrimaryColors();

	UE::Geometry::FDynamicMeshMaterialAttribute* MaterialIDs = DynamicMesh.Attributes()->GetMaterialID();
	UE::Geometry::FDynamicMeshUVOverlay* UVs = DynamicMesh.Attributes()->PrimaryUV();
	UE::Geometry::FDynamicMeshColorOverlay* Colors = DynamicMesh.Attributes()->PrimaryColors();

	struct FHeightmapVertex
	{
		int32 VertexIndex;
		int32 VertexColorIndex;
		int32 UVIndex;
		FBiomePaletteIndex BiomeIndex;
		bool bMaskedOut;
	};

	const int32 NumPointsOnLOD0 = LandscapeHeightMap.GetSurfaceResolutionXY();

	// Make sure we can generate the LOD in question in first place - the mesh could be too small
	const int32 MeshScale = 1 << LODIndex;
	if ( !ensureMsgf( NumPointsOnLOD0 % MeshScale == 0 && NumPointsOnLOD0 % MeshScale == 0, TEXT("Cannot generate Surface Mesh LOD %d because points grid %dx%d does not scale %d times"), LODIndex, NumPointsOnLOD0, NumPointsOnLOD0, MeshScale ) )
	{
		UE_LOG( LogOpenWorldGenerator, Warning, TEXT("Cannot generate Surface Mesh LOD %d because Surface Resolution %dx%d does not scale %d times"), LODIndex, NumPointsOnLOD0, NumPointsOnLOD0, MeshScale );
		return;
	}

	// We super-sample the grid, so we have twice as many quads as the resolution of the grid to allow for smoother interpolation between the grid values
	const int32 NumPoints = ( NumPointsOnLOD0 / MeshScale ) * 2;
	const float QuadSize = SurfaceSizeWorldUnits / ( NumPoints - 1 );

	// Generate vertices for each heightmap point that is valid. We need one extra point for the last quad, and that would be the point with data identical to the neighbouring chunk.
	TArray<FHeightmapVertex> HeightMapVertices;
	HeightMapVertices.AddZeroed( NumPoints * NumPoints );

	const FCheckedFloatArray HeightmapData( LandscapeHeightMap ); // = LandscapeHeightMap.GetDataPtr<float>();
	const FBiomePaletteIndex* BiomeMapData = BiomeMap.GetDataPtr<FBiomePaletteIndex>();
	
	for ( int32 PointY = 0; PointY < NumPoints; PointY++ )
	{
		for ( int32 PointX = 0; PointX < NumPoints; PointX++ )
		{
			const int32 MeshVertexIndex = MESH_POINT_INDEX( PointX, PointY, NumPoints );
			FHeightmapVertex& Vertex = HeightMapVertices[ MeshVertexIndex ];

			float ResultPointHeight;
			int32 ResultPointBiomeIndex;

			// Calculate adjusted positions. Adjusted positions are usable for determining whenever the point maps directly to the grid or not with LOD levels taken into account
			const int32 AdjPointX = PointX - ( PointX >= NumPoints / 2 ? 1 : 0 );
			const int32 AdjPointY = PointY - ( PointY >= NumPoints / 2 ? 1 : 0 );

			if ( AdjPointX % 2 == 0 && AdjPointY % 2 == 0 )
			{
				// Vertex is aligned with the world grid - sample the data directly
				const int32 DataIndex = SURFACE_DATA_INDEX( AdjPointX / 2, AdjPointY / 2, NumPoints, MeshScale );
				ResultPointHeight = HeightmapData[ DataIndex ];
				ResultPointBiomeIndex = BiomeMapData[ DataIndex ];
			}
			else if ( AdjPointX % 2 == 0 )
			{
				// Vertex is aligned with the world grid on the X axis - sample 2 adjacent points on Y axis
				const int32 DataIndexY0 = SURFACE_DATA_INDEX( AdjPointX / 2, AdjPointY / 2, NumPoints, MeshScale );
				const int32 DataIndexYP = SURFACE_DATA_INDEX( AdjPointX / 2, AdjPointY / 2 + 1, NumPoints, MeshScale );

				const float PointHeightY0 = HeightmapData[ DataIndexY0 ];
				const float PointHeightYP = HeightmapData[ DataIndexYP ];

				ResultPointHeight = PointHeightY0 * 0.5f + PointHeightYP * 0.5f;
				ResultPointBiomeIndex = BiomeMapData[ DataIndexY0 ];
			}
			else if ( AdjPointY % 2 == 0 )
			{
				// Vertex is aligned with the world grid on the Y axis - sample 2 adjacent points on X axis
				const int32 DataIndexX0 = SURFACE_DATA_INDEX( AdjPointX / 2, AdjPointY / 2, NumPoints, MeshScale );
				const int32 DataIndexXP = SURFACE_DATA_INDEX( AdjPointX / 2 + 1, AdjPointY / 2, NumPoints, MeshScale ) ;

				const float PointHeightX0 = HeightmapData[ DataIndexX0 ];
				const float PointHeightXP = HeightmapData[ DataIndexXP ];

				ResultPointHeight = PointHeightX0 * 0.5f + PointHeightXP * 0.5f;
				ResultPointBiomeIndex = BiomeMapData[ DataIndexX0 ];
			}
			else
			{
				// Vertex is not aligned with world grid on either axis, average out 2 points on the diagonal that has smallest height difference to avoid jagged edges
				const int32 DataIndexX0Y0 = SURFACE_DATA_INDEX( AdjPointX / 2, AdjPointY / 2, NumPoints, MeshScale );
				const int32 DataIndexXPY0 = SURFACE_DATA_INDEX( AdjPointX / 2 + 1, AdjPointY / 2, NumPoints, MeshScale );
				const int32 DataIndexX0YP = SURFACE_DATA_INDEX( AdjPointX / 2, AdjPointY / 2 + 1, NumPoints, MeshScale );
				const int32 DataIndexXPYP = SURFACE_DATA_INDEX( AdjPointX / 2 + 1, AdjPointY / 2 + 1, NumPoints, MeshScale );
				
				const float PointHeightX0Y0 = HeightmapData[ DataIndexX0Y0 ];
				const float PointHeightXPY0 = HeightmapData[ DataIndexXPY0 ];
				const float PointHeightX0YP = HeightmapData[ DataIndexX0YP ];
				const float PointHeightXPYP = HeightmapData[ DataIndexXPYP ];

				if ( FMath::Abs( PointHeightXPYP - PointHeightX0Y0 ) > FMath::Abs( PointHeightX0YP - PointHeightXPY0 ) )
				{
					ResultPointHeight = PointHeightX0YP * 0.5f + PointHeightXPY0 * 0.5f;
					ResultPointBiomeIndex = BiomeMapData[ DataIndexX0Y0 ];
				}
				else
				{
					ResultPointHeight = PointHeightXPYP * 0.5f + PointHeightX0Y0 * 0.5f;
					ResultPointBiomeIndex = BiomeMapData[ DataIndexXPYP ];
				}
			}
			
			// Calculate vertex position
			const float ResultX = PointX * QuadSize - SurfaceSizeWorldUnits / 2.0f;
			const float ResultY = PointY * QuadSize - SurfaceSizeWorldUnits / 2.0f;

			const FVector ResultVertexPosition( ResultX, ResultY, ResultPointHeight );
			const int32 VertexIndex = DynamicMesh.AppendVertex( ResultVertexPosition );
			Vertex.VertexIndex = VertexIndex;

			// Generate UVs for the vertex
			const float VertexU = PointX / ( NumPoints * 1.0f );
			const float VertexV = PointY / ( NumPoints * 1.0f );
			Vertex.UVIndex = UVs->AppendElement( FVector2f( VertexU, VertexV ) );

			const FVector4f VertexColor = FVector4f( 1.0f );
			Vertex.VertexColorIndex = Colors->AppendElement( VertexColor );
			Vertex.BiomeIndex = ResultPointBiomeIndex;
		}
	}

	// Generate triangles for connected valid vertices
	for ( int32 PointY = 0; PointY < NumPoints; PointY++ )
	{
		for ( int32 PointX = 0; PointX < NumPoints; PointX++ )
		{
			const int32 IndexX0Y0 = MESH_POINT_INDEX( PointX, PointY, NumPoints );
			if ( HeightMapVertices[ IndexX0Y0 ].bMaskedOut ) continue;

			const int32 IndexX0YP = MESH_POINT_INDEX( PointX, PointY + 1, NumPoints );
			const int32 IndexX0YN = MESH_POINT_INDEX( PointX, PointY - 1, NumPoints );
			const int32 IndexXPY0 = MESH_POINT_INDEX( PointX + 1, PointY, NumPoints );
			const int32 IndexXNY0 = MESH_POINT_INDEX( PointX - 1, PointY, NumPoints );

			// We can generate up to 2 triangles with this vertex - +X+Y and -X-Y,
			// given the neighbour vertices are valid.
			// See the following link for the demonstration: https://imgur.com/a/eKEB2XW

			// +X+Y
			if ( PointX + 1 < NumPoints && PointY + 1 < NumPoints )
			{
				if ( !HeightMapVertices[ IndexXPY0 ].bMaskedOut && !HeightMapVertices[ IndexX0YP ].bMaskedOut )
				{
					const FHeightmapVertex& VertexX0YP = HeightMapVertices[ IndexX0YP ];
					const FHeightmapVertex& VertexXPY0 = HeightMapVertices[ IndexXPY0 ];
					const FHeightmapVertex& VertexX0Y0 = HeightMapVertices[ IndexX0Y0 ];

					const UE::Geometry::FIndex3i TriangleVertices( VertexX0YP.VertexIndex, VertexXPY0.VertexIndex, VertexX0Y0.VertexIndex );

					const int32 TriangleId = DynamicMesh.AppendTriangle( TriangleVertices );
					MaterialIDs->SetValue( TriangleId, (int32) VertexX0Y0.BiomeIndex );

					const UE::Geometry::FIndex3i TriangleUVIndices( VertexX0YP.UVIndex, VertexXPY0.UVIndex, VertexX0Y0.UVIndex );
					UVs->SetTriangle( TriangleId, TriangleUVIndices );

					const UE::Geometry::FIndex3i TriangleColors( VertexX0YP.VertexColorIndex, VertexXPY0.VertexColorIndex, VertexX0Y0.VertexColorIndex );
					Colors->SetTriangle( TriangleId, TriangleColors );
				}
			}
			// -X-Y
			if ( PointX - 1 >= 0 && PointY - 1 >= 0 )
			{
				if ( !HeightMapVertices[ IndexXNY0 ].bMaskedOut && !HeightMapVertices[ IndexX0YN ].bMaskedOut )
				{
					const FHeightmapVertex& VertexX0YN = HeightMapVertices[ IndexX0YN ];
					const FHeightmapVertex& VertexXNY0 = HeightMapVertices[ IndexXNY0 ];
					const FHeightmapVertex& VertexX0Y0 = HeightMapVertices[ IndexX0Y0 ];

					const UE::Geometry::FIndex3i TriangleVertices( VertexX0YN.VertexIndex,  VertexXNY0.VertexIndex, VertexX0Y0.VertexIndex );

					const int32 TriangleId = DynamicMesh.AppendTriangle( TriangleVertices );
					MaterialIDs->SetValue( TriangleId, (int32) VertexX0Y0.BiomeIndex );

					const UE::Geometry::FIndex3i TriangleUVIndices( VertexX0YN.UVIndex, VertexXNY0.UVIndex, VertexX0Y0.UVIndex );
					UVs->SetTriangle( TriangleId, TriangleUVIndices );

					const UE::Geometry::FIndex3i TriangleColors( VertexX0YN.VertexColorIndex, VertexXNY0.VertexColorIndex, VertexX0Y0.VertexColorIndex );
					Colors->SetTriangle( TriangleId, TriangleColors );
				}
			}
		}
	}

	UE::Geometry::FMeshNormals::QuickComputeVertexNormals( DynamicMesh );
}

// TODO @open-world-generator: Clean this up. This is some prototyping code for later. First segment is Constructive Solid Geometry, second one is Marching Cubes with inline perlin noise.

/*UE::Geometry::FSphereGenerator SphereGenerator;
SphereGenerator.Radius = 2500.0f;
SphereGenerator.NumPhi = SphereGenerator.NumTheta = 16;
SphereGenerator.Generate();

UE::Geometry::FDynamicMesh3 SphereMesh;
SphereMesh.Copy( &SphereGenerator );

UE::Geometry::FDynamicMesh3 ResultMesh;
UE::Geometry::FMeshBoolean MeshBoolean( &DynamicMesh, &SphereMesh, &ResultMesh, UE::Geometry::FMeshBoolean::EBooleanOp::Union );
MeshBoolean.Compute();
DynamicMesh = MoveTemp( ResultMesh );*/

/*if ( LODIndex == 0 )
{
FastNoise::SmartNode<FastNoise::Generator> PerlinNoise = FastNoise::New<FastNoise::Simplex>();

constexpr int32 NoiseDimensionXYZ = 64;
constexpr UE::Geometry::FVector3i GridDimensions( NoiseDimensionXYZ, NoiseDimensionXYZ, NoiseDimensionXYZ );
constexpr float CellSize = 100.0f;

float NoiseValueBuffer[GridDimensions.X * GridDimensions.Y * GridDimensions.Z];
PerlinNoise->GenUniformGrid3D( NoiseValueBuffer, 0, 0, 0, GridDimensions.X, GridDimensions.Y, GridDimensions.Z, 0.05f, 1337 );

FIndexedFloatArrayGrid FloatArrayGrid( NoiseValueBuffer, GridDimensions );
UE::Geometry::TTriLinearGridInterpolant GridInterpolationFunction( &FloatArrayGrid, FVector3d::ZeroVector, CellSize, GridDimensions );

UE::Geometry::FMarchingCubes MarchingCubes;
MarchingCubes.Implicit = [&]( const FVector3d& InPosition )
{
	return GridInterpolationFunction.Value( InPosition );
};
MarchingCubes.CubeSize = CellSize;
MarchingCubes.Bounds = UE::Geometry::FAxisAlignedBox3d( FVector::ZeroVector, FVector( GridDimensions.X * CellSize, GridDimensions.Y * CellSize, GridDimensions.Z * CellSize ) );
MarchingCubes.bEnableValueCaching = false;

MarchingCubes.Generate();
DynamicMesh = UE::Geometry::FDynamicMesh3( &MarchingCubes );
}*/

#undef SURFACE_DATA_INDEX
#undef MESH_POINT_INDEX
