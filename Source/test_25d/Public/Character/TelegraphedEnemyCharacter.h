#pragma once
#include "CoreMinimal.h"
#include "Character/EnemyCharacter.h"
#include "StoneGolem/StoneGolemConfig.h"
#include "TelegraphedEnemyCharacter.generated.h"
class UProceduralMeshComponent;
class UTextRenderComponent;
class UNiagaraComponent;

USTRUCT()
struct FStoneGolemPresentation
{
    GENERATED_BODY()
    UPROPERTY()
    int32 Serial = 0;
    UPROPERTY()
    int32 Lease = 0;
    UPROPERTY()
    uint8 Phase = 0;
    UPROPERTY()
    FVector Center = FVector::ZeroVector;
    UPROPERTY()
    FVector Impact = FVector::ZeroVector;
    UPROPERTY()
    float Yaw = 0;
    UPROPERTY()
    FStoneGolemAttackTuning Tuning;
};

UCLASS()
class TEST_25D_API ATelegraphedEnemyCharacter : public AEnemyCharacter
{
    GENERATED_BODY()
public:
    ATelegraphedEnemyCharacter();
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "StoneGolem")
    TObjectPtr<UMonsterCombatConfig> CombatConfig;
    virtual UMonsterCombatConfig* GetCombatConfig() const
    {
        return CombatConfig;
    }
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    virtual void StunTagChanged(FGameplayTag Tag, int32 Count) override;
    int32 BeginAttack(const FStoneGolemAttackTuning& Tuning, const FVector& Center, float Yaw);
    void UpdateWarning(int32 Serial, const FVector& Center, float Yaw, const FStoneGolemAttackTuning* Tuning = nullptr);
    void ShowImpact(int32 Serial, const FVector& Impact);
    void ShowActiveFootprint(int32 Serial, const FVector* Impact = nullptr);
    void FinishAttack(int32 Serial);
    bool IsAttackCurrent(int32 Serial, int32 Lease) const;
    bool IsAttacking() const
    {
        return bAttackActive;
    }
    bool TryChooseAttack();
    UFUNCTION(BlueprintCallable, Category = "StoneGolem")
    bool TryAttackIndex(int32 Index);

protected:
    virtual void GrantEnemyAbilities() override;
    virtual void InvalidateEnemyAttack() override;
    virtual void ReconcileEnemyPresentation() override
    {
        OnRep_Presentation();
    }
    virtual void ApplyConfig();

private:
    void ClearImpact();
    void BuildFootprint();
    UFUNCTION()
    void OnRep_Presentation();
    UPROPERTY(ReplicatedUsing = OnRep_Presentation)
    FStoneGolemPresentation Presentation;
    UPROPERTY()
    TObjectPtr<UProceduralMeshComponent> Footprint;
    UPROPERTY()
    TObjectPtr<UTextRenderComponent> Exclamation;
    UPROPERTY()
    TObjectPtr<UNiagaraComponent> ImpactComponent;
    int32 Generation = 0;
    int32 LastImpact = 0;
    bool bAttackActive = false;
    FTimerHandle ImpactExpiry;
};
