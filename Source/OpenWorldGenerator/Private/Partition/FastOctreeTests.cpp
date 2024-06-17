// Copyright Nikita Zolotukhin. All Rights Reserved.

#include "Partition/FastOctree.h"
#include "Misc/CoreMisc.h"

struct FFastOctreeTestsExec : FSelfRegisteringExec
{
protected:
	// Begin FExec Interface
	virtual bool Exec_Dev(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override;
	// End FExec Interface
};

static FFastOctreeTestsExec GFastOctreeTestsExec;

using FTestFastOctree = TFastOctree<int32, int32, 4, 4>;
static TUniquePtr<FTestFastOctree> GTestOctree = MakeUnique<FTestFastOctree>( 0 );

bool FFastOctreeTestsExec::Exec_Dev( UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar )
{
	if ( FParse::Command( &Cmd, TEXT("FastOctreeTests") ) )
	{
		if ( FParse::Command( &Cmd, TEXT("Reset") ) )
		{
			GTestOctree = MakeUnique<FTestFastOctree>( 0 );
			Ar.Logf( TEXT("Test Octree reset to initial state (all 0)") );
			return true;
		}
		if ( FParse::Command( &Cmd, TEXT("Get") ) )
		{
			FString StrX, StrY, StrZ;
			if ( FParse::AlnumToken( Cmd, StrX ) && FParse::AlnumToken( Cmd, StrY ) && FParse::AlnumToken( Cmd, StrZ ) )
			{
				const UE::Math::TIntVector3<uint32> ElementIndex( FCString::Atoi( *StrX ), FCString::Atoi( *StrY ), FCString::Atoi( *StrZ ) );
				const int32 ElementValue = GTestOctree->GetNodeAt( ElementIndex );
				Ar.Logf( TEXT("Element at (%ld,%ld,%ld) = %d"), ElementIndex.X, ElementIndex.Y, ElementIndex.Z, ElementValue );
				return true;
			}
			Ar.Logf( TEXT("Usage: FastOctreeTests Get <X> <Y> <Z>") );
			return true;
		}
		if ( FParse::Command( &Cmd, TEXT("Set") ) )
		{
			FString StrX1, StrY1, StrZ1, StrX2, StrY2, StrZ2, StrVal;
			if ( FParse::AlnumToken( Cmd, StrX1 ) && FParse::AlnumToken( Cmd, StrY1 ) && FParse::AlnumToken( Cmd, StrZ1 ) &&
				FParse::AlnumToken( Cmd, StrX2 ) && FParse::AlnumToken( Cmd, StrY2 ) && FParse::AlnumToken( Cmd, StrZ2 ) &&
				FParse::AlnumToken( Cmd, StrVal ) )
			{
				const UE::Math::TIntVector3<uint32> StartIndex( FCString::Atoi( *StrX1 ), FCString::Atoi( *StrY1 ), FCString::Atoi( *StrZ1 ) );
				const UE::Math::TIntVector3<uint32> EndIndex( FCString::Atoi( *StrX2 ), FCString::Atoi( *StrY2 ), FCString::Atoi( *StrZ2 ) );
				const int32 ElementValue = FCString::Atoi( *StrVal );
				GTestOctree->SetNodeRangeAt( StartIndex, EndIndex, ElementValue );
				Ar.Logf( TEXT("Set elements (%ld,%ld,%ld) - (%ld,%ld,%ld) = %d"), StartIndex.X, StartIndex.Y, StartIndex.Z, EndIndex.X, EndIndex.Y, EndIndex.Z, ElementValue );
				return true;
			}
			Ar.Logf( TEXT("Usage: FastOctreeTests Set <X1> <Y1> <Z1> <X2> <Y2> <Z2> <Value>") );
			return true;
		}
		return true;
	}
	return false;
}
