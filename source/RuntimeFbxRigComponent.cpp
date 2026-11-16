#include "Carla.h"
#include "Carla/Vehicle/RuntimeFbxRigComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "ProceduralMeshComponent.h"

URuntimeFbxRigComponent::URuntimeFbxRigComponent()
{
  PrimaryComponentTick.bCanEverTick = true;
  PrimaryComponentTick.bStartWithTickEnabled = true;
  BoneNames[0] = TEXT("Wheel_Front_Left");
  BoneNames[1] = TEXT("Wheel_Front_Right");
  BoneNames[2] = TEXT("Wheel_Rear_Left");
  BoneNames[3] = TEXT("Wheel_Rear_Right");
}

void URuntimeFbxRigComponent::InitializeRig(USkeletalMeshComponent *InDonor,
    USceneComponent *InVisualRoot, UProceduralMeshComponent *const InWheels[4],
    const FVector InWheelCenters[4])
{
  Donor = InDonor;
  VisualRoot = InVisualRoot;
  for (int32 I = 0; I < 4; ++I) { Wheels[I] = InWheels[I]; Centers[I] = InWheelCenters[I]; }
  AddTickPrerequisiteComponent(InDonor);
}

void URuntimeFbxRigComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction *ThisTickFunction)
{
  Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
  USkeletalMeshComponent *Mesh = Donor.Get();
  USceneComponent *Root = VisualRoot.Get();
  if (!Mesh || !Root) return;
  if (!bHaveReference)
  {
    for (int32 I = 0; I < 4; ++I)
      ReferenceBones[I] = Mesh->GetSocketTransform(BoneNames[I], RTS_Actor);
    bHaveReference = true;
    return;
  }
  const FQuat VisualRotation = Root->GetRelativeTransform().GetRotation();
  for (int32 I = 0; I < 4; ++I)
  {
    UProceduralMeshComponent *Wheel = Wheels[I].Get();
    if (!Wheel) continue;
    const FTransform Current = Mesh->GetSocketTransform(BoneNames[I], RTS_Actor);
    const FQuat Delta = Current.GetRotation() * ReferenceBones[I].GetRotation().Inverse();
    const FQuat LocalDelta = VisualRotation.Inverse() * Delta * VisualRotation;
    const FVector Travel = VisualRotation.UnrotateVector(
        Current.GetLocation() - ReferenceBones[I].GetLocation());
    Wheel->SetRelativeLocation(Centers[I] + Travel);
    Wheel->SetRelativeRotation(LocalDelta);
  }
}
