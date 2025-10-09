#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "SR_ProcBentPath.generated.h"

UCLASS(Blueprintable)
class ASR_ProcBentPath : public AActor
{
	GENERATED_BODY()

public:
	ASR_ProcBentPath();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mesh")
	TObjectPtr<UProceduralMeshComponent> ProcMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Attachments")
	TObjectPtr<USceneComponent> EndTransform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attachments")
	TObjectPtr<AActor> EndActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attachments")
	bool PreserveUp = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Transform")
	FTransform OffsetTransform; 

	// --- Shape params (editable in Details) ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Path", meta=(ClampMin="0.01", UIMin="0.01"))
	float LengthMeters = 10.f;

	/** Positive bends left (yaw CCW looking down +Z). 0 = straight. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Path", meta=(UIMin="-180", UIMax="180"))
	float CurveDegrees = 27.f;

	/** Final Z delta after Length; negative means going down. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Path")
	float DropDepthMeters = -2.4f;

	/** Path half-width on either side of centerline (overall width = 2*HalfWidth). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Path", meta=(ClampMin="0.01", UIMin="0.01"))
	float HalfWidth = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Path", meta=(ClampMin="0.0", UIMin="0.0"))
	float CeilingHeight = 2.5f; // meters

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mesh", meta=(ClampMin="0.0", UIMin="0.0"))
	float WallThickness = 5.f; // cm (physical shell thickness)
	
	/** More segments = smoother curve. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tessellation", meta=(ClampMin="1", UIMin="1"))
	int32 Segments = 32;

	/** Generate normals from geometry or use face normals only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mesh")
	bool bSmoothNormals = true;

	/** Build simple collision from triangles. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mesh")
	bool bCreateCollision = true;

	// Optional: different materials per section
	UPROPERTY(EditAnywhere, Category="Mesh")
	UMaterialInterface* FloorMat = nullptr;
	UPROPERTY(EditAnywhere, Category="Mesh")
	UMaterialInterface* WallMat = nullptr;
	UPROPERTY(EditAnywhere, Category="Mesh")
	UMaterialInterface* CeilingMat = nullptr;
	
public:
	/** Rebuild when actor is moved/edited in editor, and at BeginPlay. */
	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void BuildMesh();

	// Helper to sample centerline point & tangent for 0..1
	void SampleCenterline(float T, FVector& OutPos, FVector& OutTangent) const;
};
