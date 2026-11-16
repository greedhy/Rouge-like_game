// 联机对照由专用测试 GameMode 授予，正式人物没有这些 Spec。
#pragma once

#include "Abilities/GameplayAbility.h"
#include "Game/MyGameModeBase.h"
#include "StanderComboPredictionProbe.generated.h"

class UAbilitySystemComponent;

UCLASS()
class AStanderComboPredictionProbe : public AActor
{
    GENERATED_BODY()

public:
    AStanderComboPredictionProbe();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    void Configure(int32 InScenario);
    void ActivateNext(int32 Stage);
    void Record(const TCHAR* Event, int32 Stage, FPredictionKey Key, bool bCancelled = false) const;
    bool RejectOnAuthority(int32 Stage) const;

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UAbilitySystemComponent> ASC;
    UPROPERTY(Replicated)
    int32 Scenario = INDEX_NONE;
    bool bStarted = false;
    TSubclassOf<UGameplayAbility> StageClass(int32 Stage) const;
    /** 远端拥有者按下真实 ASC 输入，观察正式剑盾蒙太奇完成后再接下一段。 */
    void AdvanceProductionCombo();
    int32 Presses = 0;
    int32 SeenStages = 0;
    FName LastMontage;
    double IdleSince = 0.;
};

UCLASS()
class AStanderComboPredictionGameMode : public AMyGameModeBase
{
    GENERATED_BODY()

public:
    virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
    virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& Id,
                          FString& ErrorMessage) override;
    virtual APlayerController* Login(UPlayer* Client, ENetRole InRemoteRole, const FString& Portal, const FString& Options,
                                     const FUniqueNetIdRepl& Id, FString& ErrorMessage) override;
    virtual void PostLogin(APlayerController* NewPlayer) override;
    virtual void Logout(AController* Exiting) override;
    virtual UClass* GetDefaultPawnClassForController_Implementation(AController* Controller) override;
    virtual void InitializeHUDForPlayer_Implementation(APlayerController* Controller) override;
    virtual AActor* FindPlayerStart_Implementation(AController* Controller, const FString& IncomingName) override;
    virtual AActor* ChoosePlayerStart_Implementation(AController* Controller) override;
};

UCLASS(Abstract)
class UStanderComboProbeAbility : public UGameplayAbility
{
    GENERATED_BODY()

public:
    UStanderComboProbeAbility();
    virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                    const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags,
                                    FGameplayTagContainer* RelevantTags) const override;
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                 FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Data) override;
    virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                            FGameplayAbilityActivationInfo Activation, bool bReplicate, bool bCancelled) override;

protected:
    int32 Stage = 1;
};

UCLASS()
class UStanderComboProbeFirst : public UStanderComboProbeAbility
{
    GENERATED_BODY()

public:
    UStanderComboProbeFirst();
};

UCLASS()
class UStanderComboProbeSecond : public UStanderComboProbeAbility
{
    GENERATED_BODY()

public:
    UStanderComboProbeSecond();
};

UCLASS()
class UStanderComboProbeThird : public UStanderComboProbeAbility
{
    GENERATED_BODY()

public:
    UStanderComboProbeThird();
};

UCLASS()
class UStanderComboProbeGatedSecond : public UStanderComboProbeSecond
{
    GENERATED_BODY()

public:
    UStanderComboProbeGatedSecond();
};

UCLASS()
class UStanderComboProbeGatedThird : public UStanderComboProbeThird
{
    GENERATED_BODY()

public:
    UStanderComboProbeGatedThird();
};
