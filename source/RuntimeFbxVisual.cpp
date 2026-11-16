// Runtime visual loader for the supplied rigidly weighted Lincoln FBX, UE 4.26.
#include "Carla.h"
#include "Carla/Vehicle/RuntimeFbxVisual.h"
#include "Carla/Vehicle/RuntimeFbxRigComponent.h"
#include "Carla/Vehicle/CarlaWheeledVehicle.h"
#include "Carla/Actor/ActorDescription.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/Material.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

THIRD_PARTY_INCLUDES_START
#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
THIRD_PARTY_INCLUDES_END

namespace
{
// The supplied FBX's Assimp coordinates are X forward, Y up, Z right.
FVector Convert(const aiVector3D &V) { return FVector(V.x, V.z, V.y); }

struct FRuntimeAssimp
{
  using ImportType = const aiScene *(*)(const char *, unsigned int);
  using ReleaseType = void (*)(const aiScene *);
  using ErrorType = const char *(*)();
  void *Handle = nullptr;
  ImportType Import = nullptr;
  ReleaseType Release = nullptr;
  ErrorType Error = nullptr;
  bool Open()
  {
    Handle = FPlatformProcess::GetDllHandle(TEXT("libassimp.so.5"));
    if (!Handle) return false;
    Import = reinterpret_cast<ImportType>(FPlatformProcess::GetDllExport(Handle, TEXT("aiImportFile")));
    Release = reinterpret_cast<ReleaseType>(FPlatformProcess::GetDllExport(Handle, TEXT("aiReleaseImport")));
    Error = reinterpret_cast<ErrorType>(FPlatformProcess::GetDllExport(Handle, TEXT("aiGetErrorString")));
    return Import && Release && Error;
  }
  ~FRuntimeAssimp() { if (Handle) FPlatformProcess::FreeDllHandle(Handle); }
};

struct FPart
{
  TArray<FVector> Vertices;
  TArray<int32> Triangles;
  TArray<FVector> Normals;
  TArray<FVector2D> UVs;
  FBox Bounds = FBox(ForceInit);
};

// Parts are 0 = body, 1..4 = FL, FR, RL, RR. Refuse mixed-bone
// meshes rather than silently producing a malformed or incomplete rig.
int32 PartForMesh(const aiMesh *Mesh)
{
  int32 Part = -1;
  for (unsigned int B = 0; B < Mesh->mNumBones; ++B)
  {
    const aiBone *Bone = Mesh->mBones[B];
    if (!Bone || !Bone->mNumWeights) continue;
    const FString Name = UTF8_TO_TCHAR(Bone->mName.C_Str());
    int32 ThisPart = 0;
    if (Name.Equals(TEXT("FL"), ESearchCase::IgnoreCase)) ThisPart = 1;
    else if (Name.Equals(TEXT("FR"), ESearchCase::IgnoreCase)) ThisPart = 2;
    else if (Name.Equals(TEXT("RL"), ESearchCase::IgnoreCase)) ThisPart = 3;
    else if (Name.Equals(TEXT("RR"), ESearchCase::IgnoreCase)) ThisPart = 4;
    else if (!Name.Equals(TEXT("Root"), ESearchCase::IgnoreCase)) return -1;
    if (Part >= 0 && Part != ThisPart) return -1;
    Part = ThisPart;
  }
  return Part < 0 ? 0 : Part;
}

bool AppendNode(const aiScene *Scene, const aiNode *Node, const aiMatrix4x4 &Parent,
                FPart Parts[5], int64 &TotalVertices, int64 &TotalIndices)
{
  if (!Node) return false;
  const aiMatrix4x4 Transform = Parent * Node->mTransformation;
  aiMatrix3x3 NormalTransform(Transform);
  const float Det = NormalTransform.Determinant();
  if (!FMath::IsFinite(Det) || FMath::Abs(Det) < 1.e-12f) return false;
  NormalTransform.Inverse().Transpose();
  for (unsigned int M = 0; M < Node->mNumMeshes; ++M)
  {
    if (Node->mMeshes[M] >= Scene->mNumMeshes) return false;
    const aiMesh *Mesh = Scene->mMeshes[Node->mMeshes[M]];
    if (!Mesh || !Mesh->mVertices) return false;
    const int32 PartIndex = PartForMesh(Mesh);
    if (PartIndex < 0) return false;
    TotalVertices += Mesh->mNumVertices;
    TotalIndices += int64(Mesh->mNumFaces) * 3;
    if (TotalVertices > 2000000 || TotalIndices > 6000000) return false;
    FPart &Part = Parts[PartIndex];
    const int32 Base = Part.Vertices.Num();
    for (unsigned int I = 0; I < Mesh->mNumVertices; ++I)
    {
      const FVector V = Convert(Transform * Mesh->mVertices[I]);
      if (V.ContainsNaN()) return false;
      Part.Vertices.Add(V);
      Part.Bounds += V;
      Part.Normals.Add(Mesh->HasNormals()
          ? Convert(NormalTransform * Mesh->mNormals[I]).GetSafeNormal() : FVector::UpVector);
      Part.UVs.Add(Mesh->HasTextureCoords(0)
          ? FVector2D(Mesh->mTextureCoords[0][I].x, Mesh->mTextureCoords[0][I].y)
          : FVector2D::ZeroVector);
    }
    for (unsigned int I = 0; I < Mesh->mNumFaces; ++I)
    {
      const aiFace &Face = Mesh->mFaces[I];
      if (Face.mNumIndices != 3) continue;
      if (Face.mIndices[0] >= Mesh->mNumVertices || Face.mIndices[1] >= Mesh->mNumVertices ||
          Face.mIndices[2] >= Mesh->mNumVertices) return false;
      Part.Triangles.Add(Base + Face.mIndices[0]);
      // X/Y/Z -> X/Z/Y reflects space; compensate for the resulting winding.
      Part.Triangles.Add(Base + Face.mIndices[Det > 0.f ? 2 : 1]);
      Part.Triangles.Add(Base + Face.mIndices[Det > 0.f ? 1 : 2]);
    }
  }
  for (unsigned int I = 0; I < Node->mNumChildren; ++I)
    if (!AppendNode(Scene, Node->mChildren[I], Transform, Parts, TotalVertices, TotalIndices)) return false;
  return true;
}

UProceduralMeshComponent *MakePart(AActor *Owner, USceneComponent *Parent, FPart &Part,
    const FVector &OverallCenter, float Scale, const FVector &Pivot)
{
  UProceduralMeshComponent *Mesh = NewObject<UProceduralMeshComponent>(Owner);
  if (!Mesh) return nullptr;
  for (FVector &V : Part.Vertices) V = (V - OverallCenter) * Scale - Pivot;
  Mesh->SetupAttachment(Parent);
  Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  Mesh->bUseAsyncCooking = false;
  Owner->AddInstanceComponent(Mesh);
  Mesh->RegisterComponent();
  Mesh->SetRelativeLocation(Pivot);
  Mesh->CreateMeshSection(0, Part.Vertices, Part.Triangles, Part.Normals, Part.UVs,
                          TArray<FColor>(), TArray<FProcMeshTangent>(), false);
  Mesh->SetMaterial(0, UMaterial::GetDefaultMaterial(MD_Surface));
  return Mesh;
}
}

void TryAttachRuntimeFbxVisual(ACarlaWheeledVehicle *Vehicle, const FActorDescription &Description)
{
#if PLATFORM_LINUX
  if (!Vehicle || !IsInGameThread()) return;
  FString Path;
  if (!FParse::Value(FCommandLine::Get(), TEXT("RuntimeFbx="), Path) || Path.IsEmpty()) return;
  const FActorAttribute *Role = Description.Variations.Find(TEXT("role_name"));
  FString WantedRole = TEXT("runtime_fbx");
  FParse::Value(FCommandLine::Get(), TEXT("RuntimeFbxRole="), WantedRole);
  if (!Role || Role->Value != WantedRole || Description.Id != TEXT("vehicle.lincoln.mkz_2017")) return;

  USkeletalMeshComponent *Donor = Vehicle->GetMesh();
  if (!Donor || !Vehicle->GetRootComponent()) return;
  const FName DonorBones[4] = { TEXT("Wheel_Front_Left"), TEXT("Wheel_Front_Right"),
                               TEXT("Wheel_Rear_Left"), TEXT("Wheel_Rear_Right") };
  for (int32 I = 0; I < 4; ++I)
    if (Donor->GetBoneIndex(DonorBones[I]) == INDEX_NONE)
    {
      UE_LOG(LogCarla, Error, TEXT("RuntimeFBX: donor wheel bone %s is missing"), *DonorBones[I].ToString());
      return;
    }
  Path = FPaths::ConvertRelativePathToFull(Path);
  FRuntimeAssimp Library;
  if (!Library.Open()) { UE_LOG(LogCarla, Error, TEXT("RuntimeFBX: Assimp load failed")); return; }
  // PreTransformVertices would discard the bone identities used for classification.
  const aiScene *Scene = Library.Import(TCHAR_TO_UTF8(*Path), aiProcess_Triangulate | aiProcess_GenSmoothNormals);
  if (!Scene) { UE_LOG(LogCarla, Error, TEXT("RuntimeFBX: %s"), UTF8_TO_TCHAR(Library.Error())); return; }
  FPart Parts[5];
  int64 TotalVertices = 0, TotalIndices = 0;
  const bool Valid = AppendNode(Scene, Scene->mRootNode, aiMatrix4x4(), Parts, TotalVertices, TotalIndices);
  Library.Release(Scene);
  FBox Overall(ForceInit);
  for (int32 I = 0; I < 5; ++I)
  {
    if (!Valid || !Parts[I].Bounds.IsValid || Parts[I].Triangles.Num() < 3)
    {
      UE_LOG(LogCarla, Error, TEXT("RuntimeFBX: missing body/wheel part %d, mixed weights, or invalid geometry"), I);
      return;
    }
    Overall += Parts[I].Bounds;
  }
  const FVector Size = Overall.GetSize();
  const float Longest = FMath::Max3(Size.X, Size.Y, Size.Z);
  float Multiplier = 1.f, Yaw = 0.f, OffsetX = 0.f, OffsetY = 0.f, OffsetZ = 0.f;
  FParse::Value(FCommandLine::Get(), TEXT("RuntimeFbxScale="), Multiplier);
  FParse::Value(FCommandLine::Get(), TEXT("RuntimeFbxYaw="), Yaw);
  FParse::Value(FCommandLine::Get(), TEXT("RuntimeFbxOffsetX="), OffsetX);
  FParse::Value(FCommandLine::Get(), TEXT("RuntimeFbxOffsetY="), OffsetY);
  FParse::Value(FCommandLine::Get(), TEXT("RuntimeFbxOffsetZ="), OffsetZ);
  if (Longest < KINDA_SMALL_NUMBER || !FMath::IsFinite(Longest) || !FMath::IsFinite(Multiplier) ||
      Multiplier <= 0.f || !FMath::IsFinite(Yaw) || !FMath::IsFinite(OffsetX) ||
      !FMath::IsFinite(OffsetY) || !FMath::IsFinite(OffsetZ)) return;
  const float Scale = FMath::Max(2.f * Vehicle->GetVehicleBoundingBoxExtent().X, 1.f) / Longest * Multiplier;
  const FVector Center = Overall.GetCenter();
  USceneComponent *Root = NewObject<USceneComponent>(Vehicle);
  if (!Root) return;
  Root->SetupAttachment(Vehicle->GetRootComponent());
  Vehicle->AddInstanceComponent(Root);
  Root->RegisterComponent();
  Root->SetRelativeLocation(Vehicle->GetVehicleBoundingBoxTransform().GetLocation() +
                            FVector(OffsetX, OffsetY, OffsetZ));
  Root->SetRelativeRotation(FRotator(0.f, Yaw, 0.f));

  UProceduralMeshComponent *Body = MakePart(Vehicle, Root, Parts[0], Center, Scale, FVector::ZeroVector);
  UProceduralMeshComponent *Wheels[4] = {nullptr, nullptr, nullptr, nullptr};
  FVector WheelCenters[4];
  bool Built = Body != nullptr;
  for (int32 I = 0; I < 4 && Built; ++I)
  {
    WheelCenters[I] = (Parts[I + 1].Bounds.GetCenter() - Center) * Scale;
    Wheels[I] = MakePart(Vehicle, Root, Parts[I + 1], Center, Scale, WheelCenters[I]);
    Built = Wheels[I] != nullptr;
  }
  URuntimeFbxRigComponent *Rig = Built ? NewObject<URuntimeFbxRigComponent>(Vehicle) : nullptr;
  if (!Rig)
  {
    for (int32 I = 0; I < 4; ++I) if (Wheels[I]) Wheels[I]->DestroyComponent();
    if (Body) Body->DestroyComponent();
    Root->DestroyComponent();
    return;
  }
  Vehicle->AddInstanceComponent(Rig);
  Rig->InitializeRig(Donor, Root, Wheels, WheelCenters);
  Rig->RegisterComponent();
  Donor->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
  Donor->SetVisibility(false, true);
  Root->SetVisibility(true, true);
  UE_LOG(LogCarla, Log, TEXT("RuntimeFBX: attached animated-wheel visual from %s"), *Path);
#endif
}
