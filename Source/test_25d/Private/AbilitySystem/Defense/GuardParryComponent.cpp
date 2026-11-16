// 调用链：GuardWindow -> GE/动作时钟 -> 接触裁决 -> Actor/Mass眩晕或反射、独立时间租约 -> 原格挡前端。
#include "AbilitySystem/Defense/GuardParryComponent.h"
#include "AbilitySystem/Time/AbilityWorldTimeSubsystem.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystemComponent.h"
#include "Character/EnemyCharacter.h"
#include "Interface/CombatInterface.h"
#include "Roguelike/DungeonRunDirector.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassCombatTarget.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "GameplayEffectComponents/BlockAbilityTagsGameplayEffectComponent.h"
#include "NativeGameplayTags.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ParryStun, "Debuff.Stun");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ParryAbilities, "Abilities");

// 在Commit/打开窗口前拒绝无效时间与无界配置。
bool FGuardParrySettings::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(Window) || Window <= 0 || Window > 1.f || !FMath::IsFinite(MeleeStunDuration) ||
        MeleeStunDuration <= 0 || MeleeStunDuration > 10.f)
    {
        Error = TEXT("Parry requires window (0,1] action seconds and stun (0,10] world seconds.");
        return false;
    }
    if (bRewardWorldSlow && (!FMath::IsFinite(RewardLock) || RewardLock < 0.f || RewardLock > 60.f))
    {
        Error = TEXT("Parry reward lock must be in [0,60] action seconds.");
        return false;
    }
    return !bRewardWorldSlow || Reward.Validate(Error);
}

// 标准人物与武器格挡提示共用当前配置，避免显示值偏离实际窗口和奖励。
FString FGuardParrySettings::Describe() const
{
    if (!bEnabled)
    {
        return FString();
    }
    FString Text = FString::Printf(TEXT("\n格挡开始 %.2f 秒内可弹反：反弹来袭弹道，使近战攻击者眩晕 %.1f 秒。"), Window,
                                   MeleeStunDuration);
    if (bRewardWorldSlow)
    {
        Text +=
            FString::Printf(TEXT("\n弹反成功使世界速度降至 %.0f%%，维持 %.1f 秒。"), Reward.Scale * 100.f, Reward.Hold);
        if (Reward.bCompensateSource)
        {
            Text += TEXT("自身速度保持正常。");
        }
    }
    return Text;
}

// 使用既有Debuff.Stun驱动AI/移动；独立GE到期与池清理不会移除其他控制。
UGuardParryStunEffect::UGuardParryStunEffect()
{
    DurationPolicy = EGameplayEffectDurationType::HasDuration;
    DurationMagnitude = FScalableFloat(1.f);
    auto* TagsComponent = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("StunTags"));
    GEComponents.Add(TagsComponent);
    FInheritedTagContainer Tags;
    Tags.AddTag(TAG_ParryStun);
    TagsComponent->SetAndApplyTargetTagChanges(Tags);
    auto* Block = CreateDefaultSubobject<UBlockAbilityTagsGameplayEffectComponent>(TEXT("BlockAttacks"));
    GEComponents.Add(Block);
    FInheritedTagContainer Blocked;
    Blocked.AddTag(TAG_ParryAbilities);
    Block->SetAndApplyBlockedAbilityTagChanges(Blocked);
}

// Tick回收失效记录；每次接触同步复核，不依赖清理帧先后。
UGuardParryComponent::UGuardParryComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

// 权威角色按需拥有共享组件，死亡委托立即关闭本生命的窗口。
UGuardParryComponent* UGuardParryComponent::FindOrCreate(AActor* Target)
{
    if (!IsValid(Target) || !Target->HasAuthority())
    {
        return nullptr;
    }
    if (auto* Existing = Target->FindComponentByClass<UGuardParryComponent>())
    {
        return Existing;
    }
    auto* Created = NewObject<UGuardParryComponent>(Target);
    Target->AddInstanceComponent(Created);
    Created->RegisterComponent();
    if (auto* Combat = Cast<ICombatInterface>(Target))
    {
        Combat->GetOnDeathDelegate().AddUniqueDynamic(Created, &ThisClass::OwnerDied);
    }
    return Created;
}

// 共用暂停感知时钟，世界慢速不会延长操作判定窗口。
double UGuardParryComponent::Now() const
{
    const auto* Time = GetWorld() ? GetWorld()->GetSubsystem<UAbilityWorldTimeSubsystem>() : nullptr;
    return Time ? Time->GetGameplayRealTime() : 0.;
}

// 时间、来源生命、Avatar、精确GE与眩晕状态必须同时有效。
bool UGuardParryComponent::Current(const FGuardParryWindow& Window) const
{
    AActor* Avatar = nullptr;
    const double Age = Now() - Window.Started;
    return Age >= 0 && Age < Window.Settings.Window && AbilityExecutionIdentity::ResolveSource(Window.Source, Avatar) &&
           Avatar == GetOwner() && Window.Source.ASC->GetActiveGameplayEffect(Window.Guard) &&
           !Window.Source.ASC->HasMatchingGameplayTag(TAG_ParryStun);
}

// 绑定已生效GE的固定窗口；保留过期记录防止同一句柄刷新。
void UGuardParryComponent::Open(const FAbilitySourceToken& Source, FActiveGameplayEffectHandle Guard,
                                const FGuardParrySettings& Settings)
{
    FString Error;
    AActor* Avatar = nullptr;
    if (!Settings.bEnabled || !Settings.Validate(Error) || !Guard.IsValid() ||
        !AbilityExecutionIdentity::ResolveSource(Source, Avatar) || Avatar != GetOwner() ||
        !Source.ASC->GetActiveGameplayEffect(Guard))
    {
        return;
    }
    // 已结束的同一GE也不重开窗口；直到GE移除才丢弃记录。
    if (Windows.ContainsByPredicate(
            [&](const FGuardParryWindow& Item)
            {
                return Item.Guard == Guard;
            }))
    {
        return;
    }
    if (Windows.Num() < 16)
    {
        FGuardParryWindow Entry;
        Entry.Source = Source;
        Entry.Guard = Guard;
        Entry.Settings = Settings;
        Entry.Started = Now();
        Windows.Add(MoveTemp(Entry));
    }
}

// 按打开顺序选取首个仍有效的独立格挡窗口。
FGuardParryWindow* UGuardParryComponent::FindWindow()
{
    return Windows.FindByPredicate(
        [&](const FGuardParryWindow& Entry)
        {
            return Current(Entry);
        });
}

// 弹体与近战共用来源生命、敌对关系、世界/房间和窗口裁决。
UGuardParryComponent* UGuardParryComponent::Eligible(AActor* Target, const FAbilitySourceToken& AttackSource)
{
    AActor* Attacker = nullptr;
    auto* Component = IsValid(Target) ? Target->FindComponentByClass<UGuardParryComponent>() : nullptr;
    if (!Component || !Target->HasAuthority() || !AbilityExecutionIdentity::ResolveSource(AttackSource, Attacker) ||
        AttackSource.World != Target->GetWorld() || !UMyAbilitySystemLibrary::IsNotFriend(Target, Attacker) ||
        !ADungeonRunDirector::AllowsCombat(Target, Attacker) || !Component->FindWindow())
    {
        return nullptr;
    }
    return Component;
}

// 只读检查，允许弹体先完成自身反射策略校验。
bool UGuardParryComponent::CanParryProjectile(AActor* Target, const FAbilitySourceToken& AttackSource)
{
    return Eligible(Target, AttackSource) != nullptr;
}

// 回调前记录攻击ID并复制状态，避免同步GE/取消回调使数组引用失效。
bool UGuardParryComponent::Confirm(FGuid AttackId, FActiveGameplayEffectHandle& Guard, FAbilitySourceToken& Defender,
                                   float& StunDuration)
{
    auto* Entry = FindWindow();
    if (!Entry || !AttackId.IsValid() || Entry->Attacks.Contains(AttackId) || Entry->Attacks.Num() >= 64)
    {
        return false;
    }
    Entry->Attacks.Add(AttackId);
    Guard = Entry->Guard;
    Defender = Entry->Source;
    StunDuration = Entry->Settings.MeleeStunDuration;
    RewardSuccess(*Entry);
    return true;
}

// 已确认弹反才尝试独立时间奖励；同一格挡只尝试一次，不排队、不叠加，也不刷新现有慢速。
void UGuardParryComponent::RewardSuccess(FGuardParryWindow& Window)
{
    if (Window.bRewardAttempted)
    {
        return;
    }
    Window.bRewardAttempted = true;
    auto* Time = GetWorld() ? GetWorld()->GetSubsystem<UAbilityWorldTimeSubsystem>() : nullptr;
    if (!Window.Settings.bRewardWorldSlow || Now() < NextRewardTime || !Time || Time->HasActiveRequests())
    {
        return;
    }
    const auto Source = Window.Source;
    const auto Settings = Window.Settings.Reward;
    NextRewardTime = Now() + Window.Settings.RewardLock;
    FString Error;
    RewardSource = Source;
    RewardLease = Time->Acquire(Source, Settings, this, Error);
    // Acquire失败只放弃奖励；已经确认的伤害拦截、反射/眩晕保持成功。
}

// 仅实际反射成功才确认一次事件；重复回调不重发反击。
void UGuardParryComponent::NotifyProjectileParried(AActor* Target, const FAbilitySourceToken& AttackSource,
                                                   FGuid AttackId)
{
    if (auto* Component = Eligible(Target, AttackSource))
    {
        FActiveGameplayEffectHandle Guard;
        FAbilitySourceToken Defender;
        float Duration = 0;
        if (Component->Confirm(AttackId, Guard, Defender, Duration))
        {
            Component->OnParried.Broadcast(Guard);
        }
    }
}

// 伤害前拦截整次近战，眩晕原来源生命并取消其当前攻击。
bool UGuardParryComponent::TryParryMelee(AActor* Target, const FAbilitySourceToken& AttackSource, FGuid AttackId)
{
    auto* Component = Eligible(Target, AttackSource);
    if (!Component || !AttackId.IsValid())
    {
        return false;
    }
    FActiveGameplayEffectHandle Guard;
    FAbilitySourceToken Defender;
    float Duration = 0;
    // 重复命中仍被抑制，但不再次眩晕或广播。
    if (!Component->Confirm(AttackId, Guard, Defender, Duration))
    {
        return true;
    }
    auto* AttackerASC = AttackSource.ASC.Get();
    auto Spec =
        Defender.ASC->MakeOutgoingSpec(UGuardParryStunEffect::StaticClass(), 1, Defender.ASC->MakeEffectContext());
    if (Spec.IsValid())
    {
        Spec.Data->SetDuration(Duration, true);
        Defender.ASC->ApplyGameplayEffectSpecToTarget(*Spec.Data, AttackerASC);
    }
    AActor* StillAttacking = nullptr;
    if (AbilityExecutionIdentity::ResolveSource(AttackSource, StillAttacking))
    {
        // GE同步委托可能已结束攻击/复用生命，复核后才取消原来源的余下攻击任务。
        if (auto* Enemy = Cast<AEnemyCharacter>(StillAttacking))
        {
            Enemy->GetCharacterMovement()->StopMovementImmediately();
        }
    }
    if (AbilityExecutionIdentity::ResolveSource(AttackSource, StillAttacking))
    {
        AttackerASC->CancelAllAbilities();
    }
    Component->OnParried.Broadcast(Guard);
    return true;
}

// Mass没有敌方ASC，使用精确实体句柄取消攻击并保存独立眩晕计时。
bool UGuardParryComponent::TryParryMass(AActor* Target, int32 EntityId, FMassEntityHandle Entity, const FVector& Origin,
                                        FGuid AttackId)
{
    auto* Component = IsValid(Target) ? Target->FindComponentByClass<UGuardParryComponent>() : nullptr;
    auto* Mass = Component ? Target->GetWorld()->GetSubsystem<UPureMassSubsystem>() : nullptr;
    FVector Location;
    if (!Component || !Target->HasAuthority() || !Component->FindWindow() || !Mass || !Entity.IsSet() ||
        Mass->GetEntity(EntityId) != Entity || Origin.ContainsNaN() || !AttackId.IsValid() ||
        !PureMassCombat::Resolve(Target->GetWorld(), Target, FPureMassCombatTarget(EntityId), Location))
    {
        return false;
    }
    FActiveGameplayEffectHandle Guard;
    FAbilitySourceToken Defender;
    float Duration = 0;
    if (Component->Confirm(AttackId, Guard, Defender, Duration))
    {
        Mass->ApplyStun(EntityId, Entity, Duration, Target);
        Component->OnParried.Broadcast(Guard);
    }
    return true;
}

// 生命失效只清除本组件窗口和奖励；不移除其他调用者的GE或时间租约。
void UGuardParryComponent::Invalidate()
{
    Windows.Reset();
    const FGuid Previous = RewardLease;
    RewardLease.Invalidate();
    RewardSource = {};
    NextRewardTime = 0.;
    if (auto* Time = GetWorld() ? GetWorld()->GetSubsystem<UAbilityWorldTimeSubsystem>() : nullptr)
    {
        Time->Release(Previous);
    }
}

// 常规死亡广播路径；地下城可逆倒地显式调用相同清理入口。
void UGuardParryComponent::OwnerDied(AActor*)
{
    Invalidate();
}

// 来源失效或所属GE移除时回收记录；时间过期的有效GE记录保留以阻止刷新。
void UGuardParryComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Tick)
{
    Super::TickComponent(DeltaTime, TickType, Tick);
    AActor* RewardAvatar = nullptr;
    if (RewardLease.IsValid() && !AbilityExecutionIdentity::ResolveSource(RewardSource, RewardAvatar))
    {
        Invalidate();
        return;
    }
    Windows.RemoveAll(
        [](const FGuardParryWindow& Entry)
        {
            AActor* Avatar = nullptr;
            return !AbilityExecutionIdentity::ResolveSource(Entry.Source, Avatar) ||
                   !Entry.Source.ASC->GetActiveGameplayEffect(Entry.Guard);
        });
}

// 世界/Actor退出时先关闭窗口并解绑死亡监听。
void UGuardParryComponent::EndPlay(EEndPlayReason::Type Reason)
{
    Invalidate();
    if (auto* Combat = Cast<ICombatInterface>(GetOwner()))
    {
        Combat->GetOnDeathDelegate().RemoveDynamic(this, &ThisClass::OwnerDied);
    }
    Super::EndPlay(Reason);
}
