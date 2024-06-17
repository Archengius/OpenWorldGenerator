// Copyright Nikita Zolotukhin. All Rights Reserved.

#include "Generation/OWGNoiseGenerator.h"
#include "Curves/CurveFloat.h"
#include "Partition/ChunkData2D.h"
#include "Partition/OWGChunk.h"

THIRD_PARTY_INCLUDES_START
#include "FastNoise/Generators/Modifiers.h"
#include "FastNoise/Generators/Perlin.h"
#include "FastNoise/Generators/BasicGenerators.h"
#include "FastNoise/FastNoise.h"
THIRD_PARTY_INCLUDES_END

UOWGNoiseGenerator::UOWGNoiseGenerator() :
	NoiseOffsetX( 0.0f ),
	NoiseOffsetY( 0.0f ),
	NoiseScaleX( 1.0f ),
	NoiseScaleY( 1.0f ),
	GeneratorFrequency( 0.01f ),
	NumOctaves( 1 ),
	Lacunarity( 2.0f ),
	Gain( 0.5f )
{
}

void UOWGNoiseGenerator::GenerateNoise( int32 WorldSeed, const FChunkCoord& ChunkCoord, int32 HeightmapResolutionXY, float* OutNoiseData ) const
{
	const FastNoise::SmartNode<FastNoise::Generator> BaseGenerator = CreateAndConfigureGenerator();
	const FastNoise::SmartNode<FastNoise::Generator> NoiseGenerator = TransformGenerator( BaseGenerator );

	// Because of how chunks are spatially placed, the last row/column of the previous chunk is the first row/column of the next chunk. They have matching world locations.
	// That means the noise grid is actually one point smaller than the chunk noise data (e.g. the noise tiling is 63x63 while chunk noise data is 64x64, and last value is shared between 2 adjacent chunks)
	const int32 StartX = ChunkCoord.PosX * ( HeightmapResolutionXY - 1 );
	const int32 StartY = ChunkCoord.PosY * ( HeightmapResolutionXY - 1 );
	const int32 XSize = HeightmapResolutionXY;
	const int32 YSize = HeightmapResolutionXY;

 	NoiseGenerator->GenUniformGrid2D( OutNoiseData, StartX, StartY, XSize, YSize, GeneratorFrequency, WorldSeed );
}

FastNoise::SmartNode<FastNoise::Generator> UOWGNoiseGenerator::TransformGenerator( FastNoise::SmartNode<FastNoise::Generator> InGenerator ) const
{
	FastNoise::SmartNode<FastNoise::Generator> ResultGenerator = InGenerator;
	if ( NoiseScaleX != 1.0f || NoiseScaleY != 1.0f )
	{
		const FastNoise::SmartNode<FastNoise::DomainAxisScale> AxisScale = FastNoise::New<FastNoise::DomainAxisScale>();
		AxisScale->SetSource( ResultGenerator );
		AxisScale->SetScale<FastNoise::Dim::X>( NoiseScaleX );
		AxisScale->SetScale<FastNoise::Dim::Y>( NoiseScaleY );
		ResultGenerator = AxisScale;
	}

	if ( NoiseOffsetX != 0.0f || NoiseOffsetY != 0.0f )
	{
		const FastNoise::SmartNode<FastNoise::DomainOffset> DomainOffset = FastNoise::New<FastNoise::DomainOffset>();
		DomainOffset->SetSource( ResultGenerator );
		DomainOffset->SetOffset<FastNoise::Dim::X>( NoiseOffsetX );
		DomainOffset->SetOffset<FastNoise::Dim::Y>( NoiseOffsetY );
		ResultGenerator = DomainOffset;
	}

	if ( NumOctaves > 1 )
	{
		const FastNoise::SmartNode<FastNoise::FractalFBm> FractalNoise = FastNoise::New<FastNoise::FractalFBm>();
		FractalNoise->SetSource( ResultGenerator );
		FractalNoise->SetOctaveCount( NumOctaves );
		FractalNoise->SetLacunarity( Lacunarity );
		FractalNoise->SetGain( Gain );
		ResultGenerator = FractalNoise;
	}
	return ResultGenerator;
}

FastNoise::SmartNode<FastNoise::Generator> UOWGPerlinNoiseGenerator::CreateAndConfigureGenerator() const
{
	return FastNoise::New<FastNoise::Simplex>();
}

FastNoise::SmartNode<FastNoise::Generator> UOWGConstantNoiseGenerator::CreateAndConfigureGenerator() const
{
	const FastNoise::SmartNode<FastNoise::Constant> Noise = FastNoise::New<FastNoise::Constant>();
	Noise->SetValue( ConstantValue );
	return Noise;
}

void FOWGNoiseReference::GenerateNoise( const AOWGChunk* Chunk, int32 HeightmapResolutionXY, float* OutNoiseData ) const
{
	// Copy the noise data from the identifier
	if ( const FChunkData2D* NoiseData = Chunk->FindRawNoiseData( NoiseIdentifier ) )
	{
		const float* RawNoiseData = NoiseData->GetDataPtr<float>();
		check( NoiseData->GetSurfaceResolutionXY() == HeightmapResolutionXY );
		FMemory::Memcpy( OutNoiseData, RawNoiseData, HeightmapResolutionXY * HeightmapResolutionXY * sizeof(float) );
	}

	// Remap the values to the specified range if we are asked to
	if ( RemapCurve != nullptr )
	{
		for ( int32 i = 0; i < HeightmapResolutionXY * HeightmapResolutionXY; i++ )
		{
			OutNoiseData[ i ] = RemapCurve->GetFloatValue( OutNoiseData[ i ] );
		}
	}
}
