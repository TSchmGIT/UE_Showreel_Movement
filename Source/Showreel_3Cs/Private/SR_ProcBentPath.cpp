#include "SR_ProcBentPath.h"
#include "Kismet/KismetMathLibrary.h"

ASR_ProcBentPath::ASR_ProcBentPath()
{
	PrimaryActorTick.bCanEverTick = false;

	ProcMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ProcMesh"));
	SetRootComponent(ProcMesh);
	ProcMesh->bUseAsyncCooking = true;
}

void ASR_ProcBentPath::OnConstruction(const FTransform& Transform)
{
	BuildMesh();
}

#if WITH_EDITOR
void ASR_ProcBentPath::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	BuildMesh();
}
#endif

// Parametric centerline: arc in XY with total arc length = LengthMeters and total yaw = CurveDegrees.
// Z changes linearly by DropDepthMeters over t in [0,1].
void ASR_ProcBentPath::SampleCenterline(float T, FVector& OutPos, FVector& OutTangent) const
{
	const float Theta = FMath::DegreesToRadians(CurveDegrees);

	// Straight line fallback if near 0°
	if (FMath::IsNearlyZero(Theta, 1e-6f))
	{
		// Along +X
		const float X = LengthMeters * T;
		const float Z = DropDepthMeters * T;
		OutPos = FVector(X, 0.f, Z);

		// Tangent: derivative wrt arc parameter (constant in X, linear Z)
		OutTangent = FVector(LengthMeters, 0.f, DropDepthMeters);
		OutTangent.Normalize();
		return;
	}

	// Circular arc in XY with arc length L = R * Theta  =>  R = L / Theta
	const float R = LengthMeters / Theta;

	// Center the arc so t=0 starts at angle 0, t=1 ends at Theta
	// We’ll place the circle center at (0, R) so the start point is at (0,0) facing +X when Theta > 0.
	const float Angle = Theta * T;
	const float CosA = FMath::Cos(Angle);
	const float SinA = FMath::Sin(Angle);

	// Circle param: start at (0,0) when Angle=0
	// Point on circle around center (0, R): P = ( R*sin(A), R*(1 - cos(A)) )
	const float X = R * SinA;
	const float Y = R * (1.f - CosA);
	const float Z = DropDepthMeters * T;

	OutPos = FVector(X, (CurveDegrees >= 0.f ? +Y : -Y), Z); // flip side for negative curves

	// Tangent is derivative wrt A, scaled by dA/dt = Theta
	// d/dA of circle point: ( R*cos(A), R*sin(A) ) then * Theta
	FVector2D dP_dA(R * CosA, R * SinA);
	FVector dXY(dP_dA.X * Theta, dP_dA.Y * Theta, DropDepthMeters); // include Z slope (linear)
	if (CurveDegrees < 0.f) dXY.Y *= -1.f;

	OutTangent = dXY.GetSafeNormal();
}

void ASR_ProcBentPath::BuildMesh()
{
	if (!ProcMesh) return;

	const int32 NumRings = Segments + 1;
	const float H = CeilingHeight * 100.f; // cm
	const float W = HalfWidth;             // you already use cm
	const float Tn = WallThickness;        // cm

	// Per-face arrays (inner & outer)
	struct Strip
	{
		TArray<FVector> V;
		TArray<int32>   I;
		TArray<FVector> N;
		TArray<FVector2D> UV;
		TArray<FProcMeshTangent> Tan;
		void Reserve(int32 rings) {
			V.Reserve(rings*2); N.Reserve(rings*2); UV.Reserve(rings*2); Tan.Reserve(rings*2);
			I.Reserve((rings-1)*6);
		}
	};
	Strip FloorIn, FloorOut, LeftIn, LeftOut, RightIn, RightOut, CeilIn, CeilOut;

	FloorIn .Reserve(NumRings); FloorOut .Reserve(NumRings);
	LeftIn  .Reserve(NumRings); LeftOut  .Reserve(NumRings);
	RightIn .Reserve(NumRings); RightOut .Reserve(NumRings);
	CeilIn  .Reserve(NumRings); CeilOut  .Reserve(NumRings);

	// Build ring vertex pairs for each strip
	float AccumLen = 0.f;
	FVector PrevCenter = FVector::ZeroVector;

	auto PushPair = [](Strip& S, const FVector& A, const FVector& B, const FVector& Normal, const FVector& Tangent, float U, float V0, float V1)
	{
		S.V.Add(A);   S.V.Add(B);
		S.N.Add(Normal); S.N.Add(Normal);
		S.Tan.Add(FProcMeshTangent(Tangent.X, Tangent.Y, Tangent.Z));
		S.Tan.Add(FProcMeshTangent(Tangent.X, Tangent.Y, Tangent.Z));
		S.UV.Add(FVector2D(U, V0));
		S.UV.Add(FVector2D(U, V1));
	};

	for (int32 i = 0; i < NumRings; ++i)
	{
		const float t = Segments == 0 ? 0.f : float(i)/float(Segments);

		// Centerline + local frame
		FVector C, T;  // center, tangent
		SampleCenterline(t, C, T);

		if (i > 0) AccumLen += (C - PrevCenter).Size();
		PrevCenter = C;

		const FVector Up   = FVector::UpVector;
		FVector Side       = FVector::CrossProduct(Up, T).GetSafeNormal();
		if (Side.IsNearlyZero()) Side = FVector::CrossProduct(FVector::ForwardVector, T).GetSafeNormal();

		// Face inward normals (into tunnel)
		const FVector N_Floor   =  FVector::CrossProduct(T, Side).GetSafeNormal(); // up-ish
		const FVector N_Ceil    = -N_Floor;
		const FVector N_Left    =  Side;
		const FVector N_Right   = -Side;

		// Inner corners (rectangle, CCW around interior): Lb, Rb, Rt, Lt
		const FVector Lb = C - Side*W;       // floor-left (bottom)
		const FVector Rb = C + Side*W;       // floor-right (bottom)
		const FVector Rt = Rb + Up*H;        // ceiling-right (top)
		const FVector Lt = Lb + Up*H;        // ceiling-left (top)

		// Mitered outward offsets for corners (sum adjacent outward normals)
		// Outward per face is opposite of inward:
		const FVector NO_Floor  = -N_Floor;
		const FVector NO_Ceil   = -N_Ceil;
		const FVector NO_Left   = -N_Left;
		const FVector NO_Right  = -N_Right;

		const FVector LbOut = Lb + (NO_Floor + NO_Left ) * Tn;
		const FVector RbOut = Rb + (NO_Floor + NO_Right) * Tn;
		const FVector RtOut = Rt + (NO_Ceil  + NO_Right) * Tn;
		const FVector LtOut = Lt + (NO_Ceil  + NO_Left ) * Tn;

		// U in meters along path (like your original)
		const float U = (AccumLen > 0.f) ? (AccumLen/100.f) : 0.f;

		// -------- Inner faces (front faces toward player inside) --------
		// Floor inner: Lb -> Rb (V across width 0..1)
		PushPair(FloorIn, Lb, Rb, N_Floor, T, U, 0.f, 1.f);

		// Left wall inner: Lb -> Lt (V up height 0..1)
		PushPair(LeftIn,  Lb, Lt, N_Left, T, U, 0.f, 1.f);

		// Right wall inner: Rb -> Rt
		PushPair(RightIn, Rb, Rt, N_Right, T, U, 0.f, 1.f);

		// Ceiling inner: Lt -> Rt (V across width 0..1)
		PushPair(CeilIn,  Lt, Rt, N_Ceil, T, U, 0.f, 1.f);

		// -------- Outer faces (front faces outward) --------
		// Floor outer: LbOut -> RbOut  (normal opposite)
		PushPair(FloorOut, LbOut, RbOut, -N_Floor, T, U, 0.f, 1.f);

		// Left wall outer: LbOut -> LtOut
		PushPair(LeftOut,  LbOut, LtOut, -N_Left, T, U, 0.f, 1.f);

		// Right wall outer: RbOut -> RtOut
		PushPair(RightOut, RbOut, RtOut, -N_Right, T, U, 0.f, 1.f);

		// Ceiling outer: LtOut -> RtOut
		PushPair(CeilOut,  LtOut, RtOut, -N_Ceil, T, U, 0.f, 1.f);
	}

	// Build indices for a standard two-verts-per-ring strip,
	// keeping **CCW** from the side the normals point to.
	auto BuildStrip = [](int32 Segs, TArray<int32>& Indices, bool bPairsAreAcrossNotVertical)
	{
		Indices.Reset();
		Indices.Reserve(Segs*6);
		for (int32 s=0; s<Segs; ++s)
		{
			const int32 i0 = s*2, i1 = i0+1, i2 = i0+2, i3 = i0+3;
			if (bPairsAreAcrossNotVertical)
			{
				// Rings along U; per ring vertices are "across" the face
				// CCW from the face's normal side:
				Indices.Add(i0); Indices.Add(i1); Indices.Add(i2);
				Indices.Add(i2); Indices.Add(i1); Indices.Add(i3);
			}
			else
			{
				// For vertical walls where we pushed (bottom, top), use wall-style:
				Indices.Add(i0); Indices.Add(i2); Indices.Add(i1);
				Indices.Add(i2); Indices.Add(i3); Indices.Add(i1);
			}
		}
	};

	// Floor/Ceiling strips: pairs are across width → use 'true'
	BuildStrip(Segments, FloorIn.I , /*bPairsAreAcrossNotVertical=*/true);
	BuildStrip(Segments, FloorOut.I, /*bPairsAreAcrossNotVertical=*/false);
	BuildStrip(Segments, CeilIn.I  , /*bPairsAreAcrossNotVertical=*/false);
	BuildStrip(Segments, CeilOut.I , /*bPairsAreAcrossNotVertical=*/true);

	// Walls: pairs are vertical (bottom->top) → use 'false'
	BuildStrip(Segments, LeftIn.I  , false);
	BuildStrip(Segments, LeftOut.I , true);
	BuildStrip(Segments, RightIn.I , true);
	BuildStrip(Segments, RightOut.I, false);

	// Optional: smooth normals again using these indices (your helper already does that).
	if (bSmoothNormals)
	{
		auto Smooth = [](const TArray<FVector>& V, const TArray<int32>& I, TArray<FVector>& N)
		{
			N.SetNumZeroed(V.Num());
			for (int32 t=0; t<I.Num(); t+=3)
			{
				const int32 a=I[t], b=I[t+1], c=I[t+2];
				const FVector& A=V[a], &B=V[b], &C=V[c];
				const FVector Fn = FVector::CrossProduct(B-A, C-A).GetSafeNormal();
				N[a]+=Fn; N[b]+=Fn; N[c]+=Fn;
			}
			for (int32 v=0; v<V.Num(); ++v) N[v] = N[v].GetSafeNormal();
		};
		Smooth(FloorIn.V , FloorIn.I , FloorIn.N );
		Smooth(FloorOut.V, FloorOut.I, FloorOut.N);
		Smooth(LeftIn.V  , LeftIn.I  , LeftIn.N  );
		Smooth(LeftOut.V , LeftOut.I , LeftOut.N );
		Smooth(RightIn.V , RightIn.I , RightIn.N );
		Smooth(RightOut.V, RightOut.I, RightOut.N);
		Smooth(CeilIn.V  , CeilIn.I  , CeilIn.N  );
		Smooth(CeilOut.V , CeilOut.I , CeilOut.N );
	}

	// Normalize U (meters along) for floor/ceiling only if you want exactly [0..1].
	auto NormalizeU = [](TArray<FVector2D>& UVs)
	{
		if (UVs.Num()<2) return;
		const float TotalU = UVs.Last(1).X;
		if (!FMath::IsNearlyZero(TotalU))
		{
			const float S = 1.f / TotalU;
			for (auto& uv : UVs) uv.X *= S;
		}
	};
	NormalizeU(FloorIn.UV); NormalizeU(FloorOut.UV);
	NormalizeU(CeilIn.UV ); NormalizeU(CeilOut.UV );

	// Commit sections: 0..7
	ProcMesh->ClearAllMeshSections();

	// Inner faces (toward player)
	ProcMesh->CreateMeshSection_LinearColor(0, FloorIn.V , FloorIn.I , FloorIn.N , FloorIn.UV , {}, FloorIn.Tan , bCreateCollision);
	ProcMesh->CreateMeshSection_LinearColor(1, LeftIn.V  , LeftIn.I  , LeftIn.N  , LeftIn.UV  , {}, LeftIn.Tan  , bCreateCollision);
	ProcMesh->CreateMeshSection_LinearColor(2, RightIn.V , RightIn.I , RightIn.N , RightIn.UV , {}, RightIn.Tan , bCreateCollision);
	ProcMesh->CreateMeshSection_LinearColor(3, CeilIn.V  , CeilIn.I  , CeilIn.N  , CeilIn.UV  , {}, CeilIn.Tan  , bCreateCollision);

	// Outer faces (outward)
	ProcMesh->CreateMeshSection_LinearColor(4, FloorOut.V, FloorOut.I, FloorOut.N, FloorOut.UV, {}, FloorOut.Tan, bCreateCollision);
	ProcMesh->CreateMeshSection_LinearColor(5, LeftOut.V , LeftOut.I , LeftOut.N , LeftOut.UV , {}, LeftOut.Tan , bCreateCollision);
	ProcMesh->CreateMeshSection_LinearColor(6, RightOut.V, RightOut.I, RightOut.N, RightOut.UV, {}, RightOut.Tan, bCreateCollision);
	ProcMesh->CreateMeshSection_LinearColor(7, CeilOut.V , CeilOut.I , CeilOut.N , CeilOut.UV , {}, CeilOut.Tan , bCreateCollision);

	// Materials (optional)
	if (FloorMat)   ProcMesh->SetMaterial(0, FloorMat),   ProcMesh->SetMaterial(4, FloorMat);
	if (WallMat)    ProcMesh->SetMaterial(1, WallMat),    ProcMesh->SetMaterial(2, WallMat),
	                ProcMesh->SetMaterial(5, WallMat),    ProcMesh->SetMaterial(6, WallMat);
	if (CeilingMat) ProcMesh->SetMaterial(3, CeilingMat), ProcMesh->SetMaterial(7, CeilingMat);

	// Shadow settings that help with light leaks (outward faces + thickness should already fix most):
	ProcMesh->SetCastShadow(true);
	ProcMesh->bCastDynamicShadow    = true;
	ProcMesh->bCastShadowAsTwoSided = false; // no longer needed when we have outer faces
}