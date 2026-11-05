#pragma once
#include "CoreMinimal.h"
#include "Roguelike/DungeonPlayerCharacter.h"
#include "SwordShieldPlayer.generated.h"
class UStaticMeshComponent;
class USwordShieldConfig;
class UNiagaraSystem;
UCLASS()
class TEST_25D_API ASwordShieldPlayer : public ADungeonPlayerCharacter
{
 GENERATED_BODY()
public:
 ASwordShieldPlayer();
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly) TObjectPtr<USwordShieldConfig> SwordConfig;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> SwordVisual;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> ShieldVisual;
 UFUNCTION(NetMulticast,Unreliable) void ShowSwordImpact(UNiagaraSystem* Effect,FVector Location);
};
