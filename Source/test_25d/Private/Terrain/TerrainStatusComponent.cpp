// 主要调用链：地块 RefreshContact → UpdateEffects → GE 与表现快照；TickComponent 淘汰过期接触；复制回调刷新本地表现；死亡/EndPlay 清理。
#include "Terrain/TerrainStatusComponent.h"
#include "Terrain/TerrainSurface.h"
#include "Terrain/TerrainStatusEffects.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Character/BaseCharacter.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NiagaraComponent.h"
#include "Net/UnrealNetwork.h"

// 启用组件复制及低频 Tick，并为四种地形状态分配表现槽位。
UTerrainStatusComponent::UTerrainStatusComponent()
{
    SetIsReplicatedByDefault(true);
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = .1f;
    Presentation.Systems.SetNum(4);
}

// 读取表现位掩码，查询指定地形状态是否处于激活状态。
bool UTerrainStatusComponent::IsTerrainStatusActive(ETerrainSurfaceType Type) const
{
    return (Presentation.ActiveMask & (1 << static_cast<uint8>(Type))) != 0;
}

// 在服务器登记或续期地块接触，立即重新汇总状态效果。
void UTerrainStatusComponent::RefreshContact(ATerrainSurface* Surface)
{
    auto* Character = Cast<ABaseCharacter>(GetOwner());
    if (!Character || !Character->HasAuthority() || !IsValid(Surface) || !Surface->CanAffectCharacter(Character))
        return;
    FContact* Contact = Contacts.FindByPredicate(
        [Surface](const FContact& Item)
        {
            return Item.Surface == Surface;
        });
    if (!Contact)
        Contact = &Contacts.AddDefaulted_GetRef();
    Contact->Surface = Surface;
    Contact->Config = Surface->Config;
    // One scan interval of grace prevents flicker at adjoining tile boundaries.
    Contact->Remaining = .3f + (Surface->Config->SurfaceType == ETerrainSurfaceType::Vines
                                    ? 0.f
                                    : FMath::Max(0.f, Surface->Config->ExitDuration));
    UpdateEffects();
}

// 死亡时清理；服务器推进接触、束缚和恢复计时并重算效果。
void UTerrainStatusComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                            FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime, TickType, TickFunction);
    auto* Character = Cast<ABaseCharacter>(GetOwner());
    if (!Character)
        return;
    if (Character->IsDead_Implementation())
    {
        ClearTerrainEffects();
        return;
    }
    if (!Character->HasAuthority())
        return;
    RootRemaining = FMath::Max(0.f, RootRemaining - DeltaTime);
    RecoveryRemaining = FMath::Max(0.f, RecoveryRemaining - DeltaTime);
    for (FContact& Contact : Contacts)
        Contact.Remaining -= DeltaTime;
    Contacts.RemoveAll(
        [](const FContact& Contact)
        {
            return Contact.Remaining <= 0.f || !Contact.Surface.IsValid() || !Contact.Config.IsValid() ||
                   !Contact.Surface->bEnabled;
        });
    UpdateEffects();
}

// 按类型选出最强接触，维护四类 GE 及束缚恢复窗口，再同步表现快照。
void UTerrainStatusComponent::UpdateEffects()
{
    auto* Character = Cast<ABaseCharacter>(GetOwner());
    UAbilitySystemComponent* ASC = Character ? Character->GetAbilitySystemComponent() : nullptr;
    if (!ASC || !Character->HasAuthority())
        return;
    if (EffectASC.IsValid() && EffectASC != ASC)
        ClearTerrainEffects();
    EffectASC = ASC;
    // 第一阶段：每种地形只保留最强接触，冰霜比较减速倍率，其余比较伤害。
    const FContact* Best[4] = {};
    for (const FContact& Contact : Contacts)
    {
        if (!Contact.Config.IsValid() || !Contact.Surface.IsValid())
            continue;
        const UTerrainSurfaceConfig* Config = Contact.Config.Get();
        const int32 Index = static_cast<uint8>(Config->SurfaceType);
        if (Index >= 4)
            continue;
        if (!Best[Index] || (Index == 1 ? Config->MovementMultiplier < Best[Index]->Config->MovementMultiplier
                                        : Config->DamagePerSecond > Best[Index]->Config->DamagePerSecond))
            Best[Index] = &Contact;
    }
    // 第二阶段：藤蔓仅在恢复窗口结束后重新触发，避免连续接触永久刷新束缚。
    if (Best[3] && RecoveryRemaining <= 0.f)
    {
        RootRemaining = FMath::Max(.1f, Best[3]->Config->EntangleDuration);
        RecoveryRemaining = RootRemaining + FMath::Max(.5f, Best[3]->Config->EntangleRecovery);
        RootSystem = Best[3]->Config->StatusVFX;
    }
    // 第三阶段：逐类型维护 GE 句柄，持续接触时保留周期计时器。
    FTerrainStatusPresentation Next;
    Next.Systems.SetNum(4);
    Next.MovementMultiplier = Best[1] ? FMath::Clamp(Best[1]->Config->MovementMultiplier, .05f, 1.f) : 1.f;
    UClass* Classes[4] = {UTerrainBurnEffect::StaticClass(), UTerrainFrostEffect::StaticClass(),
                          UTerrainPoisonEffect::StaticClass(), UTerrainEntangleEffect::StaticClass()};
    for (int32 Index = 0; Index < 4; ++Index)
    {
        const bool bActive = Index == 3 ? RootRemaining > 0.f : Best[Index] != nullptr;
        if (!bActive)
        {
            if (EffectHandles[Index].IsValid())
                ASC->RemoveActiveGameplayEffect(EffectHandles[Index]);
            EffectHandles[Index].Invalidate();
            continue;
        }
        Next.ActiveMask |= (1 << Index);
        Next.Systems[Index] = Index == 3 ? RootSystem : Best[Index]->Config->StatusVFX;
        const float Damage = Best[Index] ? FMath::Max(0.f, Best[Index]->Config->DamagePerSecond) : 0.f;
        if (Best[Index])
        {
            // A stronger/remaining tile may belong to another caster. Update attribution
            // in place so switching the winning contact does not restart the periodic clock.
            if (const FActiveGameplayEffect* Active = ASC->GetActiveGameplayEffect(EffectHandles[Index]))
            {
                ATerrainSurface* Source = Best[Index]->Surface.Get();
                FGameplayEffectContextHandle Context = Active->Spec.GetContext();
                Context.AddInstigator(Source->bOnlyAffectCasterEnemies ? Source->GetOwner() : Source, Source);
            }
        }
        if (!EffectHandles[Index].IsValid() || !ASC->GetActiveGameplayEffect(EffectHandles[Index]))
        {
            ATerrainSurface* Source = Best[Index] ? Best[Index]->Surface.Get() : nullptr;
            AActor* Instigator = Source && Source->bOnlyAffectCasterEnemies ? Source->GetOwner() : Source;
            UAbilitySystemComponent* SourceASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Instigator);
            FGameplayEffectContextHandle Context = (SourceASC ? SourceASC : ASC)->MakeEffectContext();
            Context.AddInstigator(Instigator, Source);
            Context.AddSourceObject(this);
            FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(Classes[Index], 1.f, Context);
            if (Spec.IsValid())
            {
                if (Index == 0 || Index == 2)
                    Spec.Data->SetSetByCallerMagnitude(TerrainTags::Damage, Damage);
                EffectHandles[Index] = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
            }
        }
        else if (Index == 0 || Index == 2)
        {
            // Keep the existing periodic timer: continuous contact must not reset it.
            ASC->UpdateActiveGameplayEffectSetByCallerMagnitude(EffectHandles[Index], TerrainTags::Damage, Damage);
        }
    }
    // 第四阶段：只有可见状态发生变化才刷新本地表现并要求网络更新。
    const bool bChanged = Presentation.ActiveMask != Next.ActiveMask ||
                          !FMath::IsNearlyEqual(Presentation.MovementMultiplier, Next.MovementMultiplier) ||
                          Presentation.Systems != Next.Systems;
    Presentation = Next;
    if (bChanged)
    {
        UpdatePresentation();
        Character->ForceNetUpdate();
    }
}

// 清空接触与计时，移除本组件持有的 GE 并恢复默认表现。
void UTerrainStatusComponent::ClearTerrainEffects()
{
    // 先丢弃接触与控制计时，再按持有的句柄回收效果，避免清除其他系统的 GE。
    Contacts.Reset();
    RootRemaining = RecoveryRemaining = 0.f;
    RootSystem = nullptr;
    if (GetOwner() && GetOwner()->HasAuthority())
    {
        if (UAbilitySystemComponent* ASC = EffectASC.Get())
            for (auto& Handle : EffectHandles)
                if (Handle.IsValid())
                    ASC->RemoveActiveGameplayEffect(Handle);
        for (auto& Handle : EffectHandles)
            Handle.Invalidate();
        EffectASC.Reset();
    }
    const bool bWasActive = Presentation.ActiveMask != 0;
    Presentation.ActiveMask = 0;
    Presentation.MovementMultiplier = 1.f;
    Presentation.Systems.SetNum(4);
    for (auto& System : Presentation.Systems)
        System = nullptr;
    UpdatePresentation();
    if (bWasActive && GetOwner() && GetOwner()->HasAuthority())
        GetOwner()->ForceNetUpdate();
}

// 接收服务器表现快照后刷新速度及 Niagara 状态。
void UTerrainStatusComponent::OnRep_Presentation()
{
    UpdatePresentation();
}

// 刷新角色速度，并按状态位创建、更新或停止脚底 Niagara 组件。
void UTerrainStatusComponent::UpdatePresentation()
{
    auto* Character = Cast<ABaseCharacter>(GetOwner());
    if (!Character)
        return;
    Character->RefreshMovementSpeed();
    if (GetNetMode() == NM_DedicatedServer)
        return;
    // 表现阶段按槽位复用 Niagara 组件；专用服务器已经在上方跳过此阶段。
    VisualComponents.SetNum(4);
    for (int32 Index = 0; Index < 4; ++Index)
    {
        UNiagaraSystem* System = Presentation.Systems.IsValidIndex(Index) ? Presentation.Systems[Index].Get() : nullptr;
        const bool bVisible = !Character->IsDead_Implementation() && (Presentation.ActiveMask & (1 << Index)) && System;
        UNiagaraComponent* Component = VisualComponents[Index];
        if (bVisible)
        {
            if (!Component)
            {
                Component = NewObject<UNiagaraComponent>(Character);
                Component->SetAutoActivate(false);
                Component->SetupAttachment(Character->GetRootComponent());
                Component->RegisterComponent();
                VisualComponents[Index] = Component;
            }
            const bool bChanged = Component->GetAsset() != System;
            if (bChanged)
                Component->SetAsset(System);
            Component->SetRelativeLocation(
                FVector(0, 0, -Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 8));
            if (bChanged || !Component->IsActive())
                Component->Activate(true);
        }
        else if (Component && Component->IsActive())
            Component->DeactivateImmediate();
    }
}

// 结束生命周期时释放本类持有的运行期资源，再调用父类清理。
void UTerrainStatusComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    ClearTerrainEffects();
    for (UNiagaraComponent* Component : VisualComponents)
        if (IsValid(Component))
            Component->DestroyComponent();
    VisualComponents.Reset();
    Super::EndPlay(Reason);
}

// 注册本类需要发送给客户端的复制字段。
void UTerrainStatusComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UTerrainStatusComponent, Presentation);
}
