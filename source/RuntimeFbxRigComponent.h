#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RuntimeFbxRigComponent.generated.h"

class UProceduralMeshComponent;
class USceneComponent;
class USkeletalMeshComponent;

// Follows the donor vehicle's animated wheel bones; no imported skeletal asset.
UCLASS(ClassGroup=(Custom))
class CARLA_API URuntimeFbxRigComponent : public UActorComponent
{
  GENERATED_BODY()
public:
  URuntimeFbxRigComponent();
  void InitializeRig(USkeletalMeshComponent *InDonor, USceneComponent *InVisualRoot,
                     UProceduralMeshComponent *const InWheels[4],
                     const FVector InWheelCenters[4]);
  virtual void TickComponent(float DeltaTime, ELevelTick TickType,
                             FActorComponentTickFunction *ThisTickFunction) override;
private:
  TWeakObjectPtr<USkeletalMeshComponent> Donor;
  TWeakObjectPtr<USceneComponent> VisualRoot;
  TWeakObjectPtr<UProceduralMeshComponent> Wheels[4];
  FName BoneNames[4];
  FTransform ReferenceBones[4];
  FVector Centers[4];
  bool bHaveReference = false;
};
