// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OWGChunkGenerator.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGChunkGenerator.generated.h"

class UPCGGraphInstance;

/** Chunk generator that executes the PCG graph specified */
UCLASS( BlueprintType )
class OPENWORLDGENERATOR_API UPCGChunkGenerator : public UOWGChunkGenerator
{
	GENERATED_BODY()
public:
	static FName ChunkGeneratorPropertyName;

	// Begin UOWGChunkGenerator interface
	virtual bool AdvanceChunkGeneration_Implementation() override;
	virtual void EndChunkGeneration_Implementation() override;
	virtual bool CanPersistChunkGenerator_Implementation() const override;
	virtual void NotifyAboutToUnloadChunk_Implementation() override;
	// End UOWGChunkGenerator interface

protected:
	virtual bool CanStartPCGGeneration() const;
	/** Called to start the PCG graph generation */
	virtual bool BeginPCGGeneration();
	/** Called to remove the data from the PCG component once we are done */
	virtual void EndPCGGeneration();
	/** Called to immediately abort the PCG generation because the chunk is about to be unloaded. This will destroy the immediate generation results */
	virtual void AbortPCGGeneration();
	/** Migrate the managed resource from the PCG to the chunk actor, making it not affected by the cleanup */
	virtual void MigratePCGManagedResourceToChunk( class UPCGManagedResource* ManagedResource );
	
	/** Called before the PCG graph generation begins to give the generator a chance to apply additional configuration to the PCG component */
	UFUNCTION( BlueprintNativeEvent, Category = "Chunk Generator" )
	void ConfigurePCGGraph( UPCGGraphInstance* PCGGraphInstance );
	
	/** Called when the PCG graph generation is complete */
	UFUNCTION( BlueprintNativeEvent, Category = "Chunk Generator" )
	void OnPCGGraphGenerationComplete( UPCGComponent* PCGComponent );
	
	/** Graph to use for the content generation */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, Category = "Chunk Generator" )
	UPCGGraphInterface* Graph;

	/** True if we have started the PCG graph execution */
	UPROPERTY( VisibleInstanceOnly, Transient, BlueprintReadOnly, Category = "Chunk Generator" )
	bool bBegunPCGGeneration{false};

	/** True if we are currently waiting for the PCG graph generation to complete */
	UPROPERTY( VisibleInstanceOnly, Transient, BlueprintReadOnly, Category = "Chunk Generator" )
	bool bWaitingForPCGGraphToComplete{false};

	/** Instance of the PCG graph that was created from the Graph object. Will have this chunk generator as Outer object so that it can we can pinpoint it from the custom PCG elements */
	UPROPERTY( VisibleInstanceOnly, Transient, BlueprintReadOnly, Category = "Chunk Generator" )
	UPCGGraphInstance* GraphInstance;
};
