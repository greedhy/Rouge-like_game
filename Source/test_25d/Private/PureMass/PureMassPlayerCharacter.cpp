// 沙盒玩家生命周期：接管时绑定玩家状态能力系统，死亡后在当前地图重生并重新接管，不写入正式存档。
#include "PureMass/PureMassPlayerCharacter.h"
#include "Equipment/EquipmentComponent.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Controller.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Player/MyPlayerState.h"
#include "Player/MyPlayerController.h"
#include "UI/HUD/MyHUD.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "MyGameplayTags.h"

// 优先读取玩家状态等级，失去玩家状态时沿用最后缓存。
int32 APureMassPlayerCharacter::GetPlayerLevel_Implementation()
{
    if (auto* State = GetPlayerState<AMyPlayerState>())
        LastKnownLevel = State->GetPlayerLevel();
    return LastKnownLevel;
}

// 将持久玩家状态能力系统绑定到当前角色，并恢复眩晕和界面订阅。
void APureMassPlayerCharacter::InitAbilityActorInfo()
{
    auto* State = GetPlayerState<AMyPlayerState>();
    if (!State)
        return;
    auto* ASC = CastChecked<UMyAbilitySystemComponent>(State->GetAbilitySystemComponent());
    ASC->InitAbilityActorInfo(State, this);
    // PlayerState and HUD survive a local respawn. Bind their persistent callbacks only once.
    if (!ASC->OnGameplayEffectAppliedDelegateToSelf.IsBoundToObject(ASC))
        ASC->AbilityActorInfoSet();
    AbilitySystemComponent = ASC;
    AttributeSet = State->GetAttributeSet();
    OnASCRegistered.Broadcast(ASC);
    auto& StunEvent =
        ASC->RegisterGameplayTagEvent(FMyGameplayTags::Get().Debuff_Stun, EGameplayTagEventType::NewOrRemoved);
    StunEvent.RemoveAll(this);
    StunEvent.AddUObject(this, &ABaseCharacter::StunTagChanged);
    if (auto* PC = Cast<AMyPlayerController>(GetController()))
        if (auto* HUD = Cast<AMyHUD>(PC->GetHUD()))
        {
            if (!IsValid(HUD->OverlayWidget))
                HUD->InitOverlay(PC, State, ASC, AttributeSet);
            else
                HUD->GetOverlayWidgetController(FWidgetControllerParams(PC, State, ASC, AttributeSet))
                    ->BroadcastInitialValues();
        }
}

// 服务端只处理一次死亡，延迟清理旧能力效果并在当前地图重新生成玩家。
void APureMassPlayerCharacter::Die(const FVector& DeathImpulse)
{
    if (bDead || !HasAuthority())
        return;
    GetPlayerLevel_Implementation();
    ABaseCharacter::Die(DeathImpulse);
    const TWeakObjectPtr<AController> RespawnController(GetController());
    GetWorldTimerManager().SetTimer(DeathTimer,
                                    FTimerDelegate::CreateWeakLambda(
                                        this,
                                        [this, RespawnController]()
                                        {
                                            // Respawn this player locally; the legacy death path loads a map from the user's save.
                                            if (AController* PlayerController = RespawnController.Get())
                                            {
                                                if (auto* ASC = Cast<UMyAbilitySystemComponent>(AbilitySystemComponent))
                                                {
                                                    ASC->CancelAllAbilities();
                                                    ASC->ClearAllAbilities();
                                                    // Infinite attribute effects capture this pawn as SourceObject. Remove them while
                                                    // its PlayerState is still attached, before a new pawn changes captured attributes.
                                                    ASC->RemoveActiveEffects(FGameplayEffectQuery());
                                                }
                                                PlayerController->UnPossess();
                                                if (AGameModeBase* Mode = GetWorld()->GetAuthGameMode())
                                                    Mode->RestartPlayer(PlayerController);
                                            }
                                            Destroy();
                                        }),
                                    FMath::Max(.1f, DeathTime), false);
}

// 控制器接管后初始化属性、沙盒技能配置和装备效果。
void APureMassPlayerCharacter::PossessedBy(AController* NewController)
{
    ABaseCharacter::PossessedBy(NewController);
    InitAbilityActorInfo();
    InitlizeDefaultAttributes();
    if (auto* ASC = Cast<UMyAbilitySystemComponent>(AbilitySystemComponent))
    {
        // The sandbox owns its copied loadout. Existing save files and their classes stay untouched.
        ASC->CancelAllAbilities();
        ASC->ClearAllAbilities();
        AddCharacterAbilities();
        ASC->UpdateAbilityStatuses(GetPlayerLevel_Implementation());
    }
    if (auto* State = GetPlayerState<AMyPlayerState>())
        State->GetEquipmentComponent()->ReconcileEffects();
}
