// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "Data/PCGSurfaceData.h"
#include "PCGChunkLandscapeData.generated.h"

class UPCGSpatialData;
struct FPCGProjectionParams;
struct FChunkLandscapePoint;
struct FCachedChunkLandscapeData;
struct FCachedChunkBiomeData;

UCLASS()
class OPENWORLDGENERATOR_API UPCGChunkLandscapeData : public UPCGSurfaceData
{
	GENERATED_BODY()
public:
	void Initialize( const TSharedPtr<FCachedChunkLandscapeData>& InLandscapeData, const TSharedPtr<FCachedChunkBiomeData>& InBiomeData, bool InUseMetadata = true );

	// ~Begin UPCGData interface
	virtual EPCGDataType GetDataType() const override { return EPCGDataType::Surface; }
	// ~End UPCGData interface

	// ~Begin UPGCSpatialData interface
	virtual FBox GetBounds() const override;
	virtual FBox GetStrictBounds() const override;
	virtual bool SamplePoint(const FTransform& Transform, const FBox& Bounds, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const override;
	virtual bool ProjectPoint(const FTransform& InTransform, const FBox& InBounds, const FPCGProjectionParams& InParams, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const override;
	virtual bool HasNonTrivialTransform() const override { return true; }

	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
protected:
	virtual UPCGSpatialData* CopyInternal() const override;
	//~End UPCGSpatialData interface

public:
	// ~Begin UPCGSpatialDataWithPointCache interface
	virtual bool SupportsBoundedPointData() const { return true; }
	virtual const UPCGPointData* CreatePointData(FPCGContext* Context) const override { return CreatePointData(Context, FBox(EForceInit::ForceInit)); }
	virtual const UPCGPointData* CreatePointData(FPCGContext* Context, const FBox& InBounds) const override;
	// ~End UPCGSpatialDataWithPointCache interface

	bool IsUsingMetadata() const { return bUseMetadata; }
private:
	static void PopulatePointMetadata( FPCGPoint& OutPoint, const FChunkLandscapePoint& ChunkPoint, UPCGMetadata* OutMetadata );
protected:
	TSharedPtr<FCachedChunkLandscapeData> LandscapeData;
	TSharedPtr<FCachedChunkBiomeData> BiomeData;
	bool bUseMetadata = true;
};
