// ASC 标签变化 -> 状态特效；眩晕按敌人体型置于头顶，结束/死亡/EndPlay 清理。

#include "AbilitySystem/Debuff/DebuffNiagaraComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Interface/CombatInterface.h"
#include "Character/EnemyCharacter.h"
#include "Components/CapsuleComponent.h"
#include "MyGameplayTags.h"

UDebuffNiagaraComponent::UDebuffNiagaraComponent()
{
    bAutoActivate = false;
}

void UDebuffNiagaraComponent::BeginPlay()
{
    Super::BeginPlay();

    ICombatInterface* CombatInterface = Cast<ICombatInterface>(GetOwner());
    if (UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner()))
    {
        BindAbilitySystem(ASC);
    }
    else if (CombatInterface)
    {
        ASCRegisteredHandle = CombatInterface->GetOnASCRegisteredDelegate().AddUObject(
            this, &UDebuffNiagaraComponent::BindAbilitySystem);
    }
    if (CombatInterface)
    {
        CombatInterface->GetOnDeathDelegate().AddDynamic(this, &UDebuffNiagaraComponent::OnOwnerDeath);
    }
}

// 晚注册 ASC/客户端初始复制也读当前状态；重复通知先解除旧绑定，避免重复激活。
void UDebuffNiagaraComponent::BindAbilitySystem(UAbilitySystemComponent* ASC)
{
    if (BoundASC.IsValid())
    {
        BoundASC->RegisterGameplayTagEvent(DebuffTag, EGameplayTagEventType::NewOrRemoved).Remove(TagChangedHandle);
    }
    BoundASC = ASC;
    if (ASC)
    {
        TagChangedHandle = ASC->RegisterGameplayTagEvent(DebuffTag, EGameplayTagEventType::NewOrRemoved)
                               .AddUObject(this, &UDebuffNiagaraComponent::DebuffTagChanged);
        DebuffTagChanged(DebuffTag, ASC->GetTagCount(DebuffTag));
    }
}

// 星环按胶囊半径缩放；小怪保持可见，Boss 限制最大尺寸，避免遮挡整个人物。
FTransform UDebuffNiagaraComponent::MakeStunTransform(float HalfHeight, float Radius)
{
    const float Scale = FMath::Clamp(Radius / 34.f, .65f, 2.f);
    // NS_Stars_EnemyStun 使用本地空间：压缩原粒子轨道，但保留可读的星形精灵大小。
    return FTransform(FQuat::Identity, FVector(0.f, 0.f, HalfHeight + 12.f * Scale), FVector(.15f * Scale));
}

// 状态计时完全交给 GE；多个眩晕重叠时，最后一个标签消失才停止特效。
void UDebuffNiagaraComponent::DebuffTagChanged(const FGameplayTag CallbackTag, int32 NewCount)
{
    AActor* Owner = GetOwner();
    const bool bOwnerAlive = IsValid(Owner) && Owner->Implements<UCombatInterface>() &&
                             !ICombatInterface::Execute_IsDead(Owner);

    if (NewCount > 0 && bOwnerAlive)
    {
        if (DebuffTag == FMyGameplayTags::Get().Debuff_Stun)
        {
            if (const auto* Enemy = Cast<AEnemyCharacter>(Owner))
            {
                const UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent();
                const FTransform Placement = MakeStunTransform(Capsule->GetScaledCapsuleHalfHeight(),
                                                              Capsule->GetScaledCapsuleRadius());
                SetWorldLocation(Capsule->GetComponentLocation() + Placement.GetLocation());
                SetWorldScale3D(Placement.GetScale3D());
            }
        }
        Activate();
    }
    else
    {
        StopDebuff();
    }
}

// NS_Stars 含循环粒子，不能仅停发射后等待尾迹；非眩晕保留原有渐退行为。
void UDebuffNiagaraComponent::StopDebuff()
{
    if (DebuffTag == FMyGameplayTags::Get().Debuff_Stun)
    {
        DeactivateImmediate();
    }
    else
    {
        Deactivate();
    }
}

void UDebuffNiagaraComponent::OnOwnerDeath(AActor* DeadActor)
{
    StopDebuff();
}

// 组件销毁、切图时释放委托与粒子，不让旧生命状态回调触达已退出的组件。
void UDebuffNiagaraComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (BoundASC.IsValid())
    {
        BoundASC->RegisterGameplayTagEvent(DebuffTag, EGameplayTagEventType::NewOrRemoved).Remove(TagChangedHandle);
    }
    if (auto* Combat = Cast<ICombatInterface>(GetOwner()))
    {
        Combat->GetOnASCRegisteredDelegate().Remove(ASCRegisteredHandle);
        Combat->GetOnDeathDelegate().RemoveDynamic(this, &UDebuffNiagaraComponent::OnOwnerDeath);
    }
    BoundASC.Reset();
    DeactivateImmediate();
    Super::EndPlay(EndPlayReason);
}
