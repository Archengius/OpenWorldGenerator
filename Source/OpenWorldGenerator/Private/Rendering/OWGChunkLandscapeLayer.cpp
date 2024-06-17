// Copyright Nikita Zolotukhin. All Rights Reserved.

#include "Rendering/OWGChunkLandscapeLayer.h"

FOWGLandscapeGrassVariety::FOWGLandscapeGrassVariety() :
	GrassMesh( nullptr ),
	Scaling( EOWGLandscapeGrassScaling::Uniform ),
	ScaleX( 1.0f, 1.0f ),
	ScaleY( 1.0f, 1.0f ),
	ScaleZ( 1.0f, 1.0f ),
	GrassDensity( 400 ),
	bUseGrid( true ),
	PlacementJitter( 1.0f ),
	RandomRotation( true ),
	AlignToSurface( true ),
	StartCullDistance( 10000 ),
	EndCullDistance( 10000 ),
	MinLOD( -1 ),
	InstanceWorldPositionOffsetDisableDistance( 0 )
{
}

UOWGLandscapeGrassType::UOWGLandscapeGrassType() : bEnableDensityScaling( true )
{
}
