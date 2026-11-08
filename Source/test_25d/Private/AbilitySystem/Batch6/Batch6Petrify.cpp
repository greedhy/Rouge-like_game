/**
 * 石化链：服务器 GA ActivateAbility/Commit -> 混合范围查询 -> Mass ApplyPetrify 或 Enemy 组件 Apply。Actor 用 Infinite Stun GE 控制，组件复制 bPetrified/材质/Lease，OnRep_State 管理本地动画暂停与材质。Tick/GE 移除回调/EndPlay -> Clear；缓存原材质只在匹配当前覆盖时恢复，避免覆盖其他系统修改。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
#include "AbilitySystem/Batch6/Batch6Petrify.h"
#include "PureMass/PureMassAbility.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Character/EnemyCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayEffect.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "Interface/CombatInterface.h"
#include "MyGameplayTags.h"
#include "Net/UnrealNetwork.h"
#include "PureMass/PureMassCombatTarget.h"
#include "PureMass/PureMassSubsystem.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
/** 构造 Infinite 控制 GE，实际 Stun Tag 在 Apply 的独立 Spec 中加入。 */
UBatch6PetrifyEffect::UBatch6PetrifyEffect()
{
    DurationPolicy = EGameplayEffectDurationType::Infinite;
}
/** 构造默认复制组件；初始不 Tick，应用状态后由 OnRep_State 开启。 */
UBatch6PetrifyComponent::UBatch6PetrifyComponent()
{
    SetIsReplicatedByDefault(true);
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
}
/** 服务器验证 Enemy/ASC/生命代次；首次施加控制 GE 并监听移除，取消能力和移动；延长到期时间，更新材质与复制。 */
bool UBatch6PetrifyComponent::Apply(float Duration, UMaterialInterface* Material)
{
    AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(GetOwner());
    UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Enemy);
    if (!Enemy || !Enemy->HasAuthority() || !ASC || Duration <= 0 || !FMath::IsFinite(Duration) ||
        Enemy->IsDungeonPoolSleeping() || ICombatInterface::Execute_IsDead(Enemy))
        return false;
    if (bPetrified && !IsLeaseValid(Lease, Enemy->GetDungeonPoolLease()))
        Clear();
    if (!bPetrified)
    {
        FGameplayEffectSpec Spec(GetDefault<UBatch6PetrifyEffect>(), ASC->MakeEffectContext(), 1.f);
        Spec.DynamicGrantedTags.AddTag(FMyGameplayTags::Get().Debuff_Stun);
        ControlHandle = ASC->ApplyGameplayEffectSpecToSelf(Spec);
        if (!ControlHandle.IsValid())
            return false;
        ASC->OnAnyGameplayEffectRemovedDelegate().RemoveAll(this);
        ASC->OnAnyGameplayEffectRemovedDelegate().AddUObject(this, &UBatch6PetrifyComponent::OnControlRemoved);
        ASC->CancelAllAbilities();
        Enemy->GetCharacterMovement()->StopMovementImmediately();
    }
    Lease = Enemy->GetDungeonPoolLease();
    Expires = FMath::Max(Expires, GetWorld()->GetTimeSeconds() + Duration);
    StoneMaterial = Material;
    bPetrified = true;
    OnRep_State();
    Enemy->ForceNetUpdate();
    return true;
}
/** 服务器主动调用与客户端复制回调：验证 Lease 后缓存/替换材质并暂停动画；解除时恢复本组件仍持有的覆盖。 */
void UBatch6PetrifyComponent::OnRep_State()
{
    AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(GetOwner());
    if (!Enemy || !Enemy->GetMesh())
        return;
    auto* Mesh = Enemy->GetMesh();
    SetComponentTickEnabled(bPetrified);
    const bool bValidLife =
        IsLeaseValid(Lease, Enemy->GetDungeonPoolLease()) && !ICombatInterface::Execute_IsDead(Enemy);
    if (bPetrified && bValidLife && !bVisualApplied)
    {
        bWasPaused = Mesh->bPauseAnims;
        OriginalMaterials.Reset();
        for (int32 I = 0; I < Mesh->GetNumMaterials(); ++I)
            OriginalMaterials.Add(Mesh->GetMaterial(I));
        Mesh->bPauseAnims = true;
        bVisualApplied = true;
    }
    if (bPetrified && bValidLife && StoneMaterial)
    {
        for (int32 I = 0; I < Mesh->GetNumMaterials(); ++I)
            Mesh->SetMaterial(I, StoneMaterial);
        AppliedMaterial = StoneMaterial;
    }
    if ((!bPetrified || !bValidLife) && bVisualApplied)
    {
        for (int32 I = 0; I < OriginalMaterials.Num(); ++I)
            if (AppliedMaterial && Mesh->GetMaterial(I) == AppliedMaterial)
                Mesh->SetMaterial(I, OriginalMaterials[I]);
        if (Enemy->GetDungeonPoolLease() == Lease || Enemy->GetDungeonPoolLease() < 0)
            Mesh->bPauseAnims = bWasPaused;
        OriginalMaterials.Reset();
        AppliedMaterial = nullptr;
        bVisualApplied = false;
    }
}
/** ASC GE 移除委托：只处理 ControlHandle，先作废句柄再 Clear，避免重复移除递归。 */
void UBatch6PetrifyComponent::OnControlRemoved(const FActiveGameplayEffect& Effect)
{
    if (Effect.Handle != ControlHandle)
        return;
    ControlHandle.Invalidate();
    Clear();
}
/** 先作废句柄并解绑委托，再移除权威 GE；清到期/石化标记，恢复本地表现并请求网络更新。 */
void UBatch6PetrifyComponent::Clear()
{
    const auto RemovingHandle = ControlHandle;
    ControlHandle.Invalidate();
    if (GetOwner() && GetOwner()->HasAuthority())
        if (auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner()))
        {
            ASC->OnAnyGameplayEffectRemovedDelegate().RemoveAll(this);
            if (RemovingHandle.IsValid())
                ASC->RemoveActiveGameplayEffect(RemovingHandle);
        }
    ControlHandle.Invalidate();
    Expires = 0;
    bPetrified = false;
    OnRep_State();
    if (GetOwner())
        GetOwner()->ForceNetUpdate();
}
/** 检查死亡/Lease 和服务器到期；服务器 Clear，客户端只重新协调表现，不能自行结算控制。 */
void UBatch6PetrifyComponent::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta, Type, Tick);
    auto* Enemy = Cast<AEnemyCharacter>(GetOwner());
    if (bPetrified && Enemy &&
        (ICombatInterface::Execute_IsDead(Enemy) || !IsLeaseValid(Lease, Enemy->GetDungeonPoolLease()) ||
         (Enemy->HasAuthority() && GetWorld()->GetTimeSeconds() >= Expires)))
    {
        if (Enemy->HasAuthority())
            Clear();
        else
            OnRep_State();
    }
}
/** 组件退出时 Clear 控制与视觉覆盖，随后交父类清理。 */
void UBatch6PetrifyComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    Clear();
    Super::EndPlay(Reason);
}
/** 复制材质、Lease 和 bPetrified；原材质缓存/到期时间/GE 句柄不作为组件字段复制。 */
void UBatch6PetrifyComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UBatch6PetrifyComponent, StoneMaterial);
    DOREPLIFETIME(UBatch6PetrifyComponent, Lease);
    DOREPLIFETIME(UBatch6PetrifyComponent, bPetrified);
}
/** 构造 ServerOnly、每 Actor 实例化 GA，控制效果由服务器执行。 */
UBatch6PetrifyAbility::UBatch6PetrifyAbility()
{
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}
/** CommitAbility 回调，经稳定原生扣蓝 GE 扣除 Prototype.ManaCost。 */
void UBatch6PetrifyAbility::ApplyCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                      FGameplayAbilityActivationInfo Activation) const
{
    if (Info && Info->IsNetAuthority() && Prototype)
        UPureMassAbility::SpendMana(Info->AvatarActor.Get(), Prototype->ManaCost);
}
/** 服务器提交施法后查询可见范围目标：Mass 调 ApplyPetrify，Actor 按需注册组件并 Apply；完成后结束 GA，组件继续计时。 */
void UBatch6PetrifyAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                            const FGameplayAbilityActorInfo* Info,
                                            const FGameplayAbilityActivationInfo Activation,
                                            const FGameplayEventData* Data)
{
    AActor* Avatar = Info ? Info->AvatarActor.Get() : nullptr;
    if (!Avatar || !Avatar->HasAuthority() || !CommitAbility(Handle, Info, Activation))
    {
        EndAbility(Handle, Info, Activation, true, true);
        return;
    }
    for (const auto& Target : PureMassCombat::Gather(GetWorld(), Avatar, Avatar->GetActorLocation(), Radius))
    {
        if (!Target.IsActor())
        {
            if (PureMassCombat::Visible(GetWorld(), Avatar, Avatar->GetActorLocation(), Target))
                if (auto* Mass = GetWorld()->GetSubsystem<UPureMassSubsystem>())
                    Mass->ApplyPetrify(Target.EntityId, Duration, Avatar);
            continue;
        }
        auto* Enemy = Cast<AEnemyCharacter>(Target.Actor.Get());
        if (!Enemy || !PureMassCombat::Visible(GetWorld(), Avatar, Avatar->GetActorLocation(), Target))
            continue;
        auto* Component = Enemy->FindComponentByClass<UBatch6PetrifyComponent>();
        if (!Component)
        {
            Component = NewObject<UBatch6PetrifyComponent>(Enemy);
            Enemy->AddInstanceComponent(Component);
            Component->RegisterComponent();
        }
        Component->Apply(Duration, StoneMaterial);
    }
    EndAbility(Handle, Info, Activation, true, false);
}

/** UI 使用当前半径、持续时间与 Prototype 的费用/冷却生成说明。 */
FString UBatch6PetrifyAbility::GetDescription(int32 Level)
{
    return FString::Printf(
        TEXT(
            "<Title>%s</> <Level>%d</>\n<Default>Petrifies visible enemies in radius %.0f for %.1fs. Stops movement and attacks.</>\n<Default>Mana %.0f | Cooldown %.1fs</>"),
        Prototype ? *Prototype->DisplayName.ToString() : TEXT("Petrify"), Level, Radius, Duration,
        Prototype ? Prototype->ManaCost : 0.f, Prototype ? Prototype->Cooldown : 0.f);
}
