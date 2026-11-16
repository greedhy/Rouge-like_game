// ASC 输入 -> 当前武器 Spec -> Tag 转移 -> 动画结束 -> 下一次独立 GAS 激活。
#include "AbilitySystem/Combo/StanderComboComponent.h"

#include "AbilitySystem/Ability/StanderAbility.h"
#include "AbilitySystem/Ability/StanderWeaponActionAbility.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/Time/AbilityWorldTimeSubsystem.h"
#include "Character/StanderPlayer.h"
#include "Equipment/EquipmentComponent.h"
#include "MyGameplayTags.h"
#include "NativeGameplayTags.h"
#include "TimerManager.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ComboLight1, "Combo.Stage.Light1");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ComboLight2, "Combo.Stage.Light2");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ComboLight3, "Combo.Stage.Light3");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ComboReady2, "State.Combo.Ready.Light2");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ComboReady3, "State.Combo.Ready.Light3");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ComboSword1, "Abilities.StanderWeapons.Sword_Shield.Attack1");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ComboSword2, "Abilities.StanderWeapons.Sword_Shield.Attack2");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ComboSword3, "Abilities.StanderWeapons.Sword_Shield.Attack3");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ComboPress, "InputTag.LMB");

namespace
{
    UEquipmentComponent* FindEquipment(const UMyAbilitySystemComponent& ASC)
    {
        return ASC.GetOwnerActor() ? ASC.GetOwnerActor()->FindComponentByClass<UEquipmentComponent>() : nullptr;
    }
}

// 默认规则适用于出生剑盾及同一装备族，也允许派生 Avatar 配置新的精确 Tag 转移。
UStanderComboComponent::UStanderComboComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    EntryStage = TAG_ComboLight1;
    EntryInput = TAG_ComboPress;
    Stages = {{TAG_ComboLight1, TAG_ComboSword1, {}, 0}, {TAG_ComboLight2, TAG_ComboSword2, TAG_ComboReady2, 1},
              {TAG_ComboLight3, TAG_ComboSword3, TAG_ComboReady3, 2}};
    Transitions = {{TAG_ComboLight1, TAG_ComboPress, TAG_ComboLight2}, {TAG_ComboLight2, TAG_ComboPress, TAG_ComboLight3}};
}

const FStanderComboStage* UStanderComboComponent::FindStage(FGameplayTag Stage) const
{
    return Stages.FindByPredicate([Stage](const FStanderComboStage& Row)
    {
        return Row.StageTag == Stage;
    });
}

// 各输入在同一步只能选择唯一后续；入口与后续有各自的门禁。
bool UStanderComboComponent::ValidateRules() const
{
    if (!EntryInput.IsValid() || !FindStage(EntryStage) || !FMath::IsFinite(BufferSeconds) || BufferSeconds <= 0.f ||
        !FMath::IsFinite(ResetSeconds) || ResetSeconds <= 0.f)
    {
        return false;
    }
    FGameplayTagContainer StagesSeen;
    FGameplayTagContainer ActionsSeen;
    FGameplayTagContainer GatesSeen;
    for (const auto& Stage : Stages)
    {
        if (!Stage.StageTag.IsValid() || !Stage.WeaponActionTag.IsValid() || StagesSeen.HasTagExact(Stage.StageTag) ||
            ActionsSeen.HasTagExact(Stage.WeaponActionTag) || GatesSeen.HasTagExact(Stage.RequiredTag) ||
            (Stage.StageTag == EntryStage) == Stage.RequiredTag.IsValid() ||
            Stage.DefaultStrikeIndex < 0 || Stage.DefaultStrikeIndex >= 3)
        {
            return false;
        }
        StagesSeen.AddTag(Stage.StageTag);
        ActionsSeen.AddTag(Stage.WeaponActionTag);
        if (Stage.RequiredTag.IsValid())
        {
            GatesSeen.AddTag(Stage.RequiredTag);
        }
    }
    TMap<FGameplayTag, FGameplayTagContainer> Seen;
    TMap<FGameplayTag, int32> Incoming;
    for (const auto& Stage : Stages)
    {
        Incoming.Add(Stage.StageTag, 0);
    }
    for (const auto& Edge : Transitions)
    {
        auto& Inputs = Seen.FindOrAdd(Edge.From);
        if (!Edge.Input.IsValid() || !FindStage(Edge.From) || !FindStage(Edge.To) || Edge.To == EntryStage ||
            Inputs.HasTagExact(Edge.Input))
        {
            return false;
        }
        Inputs.AddTag(Edge.Input);
        ++Incoming.FindChecked(Edge.To);
    }
    TArray<FGameplayTag> Reachable = {EntryStage};
    for (int32 Index = 0; Index < Reachable.Num(); ++Index)
    {
        for (const auto& Edge : Transitions)
        {
            if (Edge.From == Reachable[Index] && --Incoming.FindChecked(Edge.To) == 0)
            {
                Reachable.Add(Edge.To);
            }
        }
    }
    return Reachable.Num() == Stages.Num();
}

// 当前输入槽是唯一入口。背包同族换装也通过装备世代让旧凭证失效。
FGameplayAbilitySpecHandle UStanderComboComponent::ResolveEntry(UMyAbilitySystemComponent& ASC) const
{
    const auto* Spec = ASC.GetSpecWithSlot(EntryInput);
    const auto* First = FindStage(EntryStage);
    if (!Spec || !Spec->Ability || !First)
    {
        return {};
    }
    if (Spec->Ability->IsA<UStanderComboAbility>())
    {
        return Spec->Handle;
    }
    const auto* Weapon = Cast<UStanderWeaponActionAbility>(Spec->Ability);
    const auto* Equipment = FindEquipment(ASC);
    if (Weapon && Weapon->ActionConfig && Weapon->ActionConfig->ActionTag == First->WeaponActionTag &&
        (!GetOwner()->HasAuthority() || (Equipment && Equipment->IsWeaponAbility(Spec->Handle))))
    {
        return Spec->Handle;
    }
    return {};
}

bool UStanderComboComponent::IsComboInput(UMyAbilitySystemComponent& ASC, FGameplayTag Input) const
{
    if (!Input.IsValid() || !ResolveEntry(ASC).IsValid())
    {
        return false;
    }
    // 备用分支键也可以交给现有输入槽；优先保留这个输入已绑定的独立能力。
    return Input == EntryInput || (!ASC.GetSpecWithSlot(Input) && Transitions.ContainsByPredicate(
        [Input](const FStanderComboTransition& Edge)
        {
            return Edge.Input == Input;
        }));
}

double UStanderComboComponent::Now() const
{
    const auto* Time = GetWorld()->GetSubsystem<UAbilityWorldTimeSubsystem>();
    return Time ? Time->GetGameplayRealTime() : GetWorld()->GetTimeSeconds();
}

// 后续可能在下一帧执行；重新检查来源、输入阻挡和装备，不能跨生命消费缓存。
bool UStanderComboComponent::IsContextCurrent() const
{
    auto* ASC = BoundASC.Get();
    AActor* Avatar = nullptr;
    if (!ASC || ASC->GetAvatarActor() != GetOwner() || !AbilityExecutionIdentity::ResolveSource(Source, Avatar) ||
        Avatar != GetOwner() || ASC->HasMatchingGameplayTag(FMyGameplayTags::Get().Player_Block_InputPressed) ||
        ResolveEntry(*ASC) != EntrySpec)
    {
        return false;
    }
    const auto* Equipment = FindEquipment(*ASC);
    return (!Equipment || Equipment->GetWeaponGeneration() == WeaponGeneration) &&
           !CastChecked<AStanderPlayer>(Avatar)->bIsStunned;
}

FGameplayTag UStanderComboComponent::FindNext(FGameplayTag Input) const
{
    const auto* Edge = Transitions.FindByPredicate([this, Input](const FStanderComboTransition& Row)
    {
        return Row.From == CurrentStage && Row.Input == Input;
    });
    return Edge ? Edge->To : FGameplayTag();
}

// 客机由 ASC 发送输入意图；权威入口只接受可解析的当前普攻来源。
bool UStanderComboComponent::RequestComboInput(FGameplayTag Input)
{
    auto* Player = Cast<AStanderPlayer>(GetOwner());
    auto* ASC = Player ? Cast<UMyAbilitySystemComponent>(Player->GetAbilitySystemComponent()) : nullptr;
    if (!Player || !ASC || !IsComboInput(*ASC, Input) || !ValidateRules())
    {
        return false;
    }
    if (!Player->HasAuthority())
    {
        if (!Player->IsLocallyControlled())
        {
            return false;
        }
        ASC->ServerStanderComboInput(Player, Input);
        return true;
    }
    if (CurrentStage.IsValid() && (!IsContextCurrent() || (!ActiveSpec.IsValid() && Now() > ReadyUntil)))
    {
        if (ActiveSpec.IsValid())
        {
            return false;
        }
        ResetCombo();
    }
    if (ActiveSpec.IsValid())
    {
        if (bOpen && !BufferedInput.IsValid() && FindNext(Input).IsValid())
        {
            BufferedInput = Input;
            return true;
        }
        return false;
    }
    if (CurrentStage.IsValid())
    {
        const FGameplayTag Next = FindNext(Input);
        if (Next.IsValid())
        {
            return StartStage(Next);
        }
        return false;
    }
    FString Error;
    if (Input != EntryInput || !AbilityExecutionIdentity::CaptureSource(Player, Source, Error))
    {
        return false;
    }
    BoundASC = ASC;
    ActivationListener = ASC->AbilityActivatedCallbacks.AddUObject(this, &ThisClass::OnOtherAbilityActivated);
    Player->GetOnDeathDelegate().AddUniqueDynamic(this, &ThisClass::OnOwnerDied);
    EntrySpec = ResolveEntry(*ASC);
    const auto* Equipment = FindEquipment(*ASC);
    WeaponGeneration = Equipment ? Equipment->GetWeaponGeneration() : 0;
    return StartStage(EntryStage);
}

// 只解析本武器组的精确动作身份。ASC 提交阶段时仍执行原 GA 的 CanActivate 和 Commit。
bool UStanderComboComponent::StartStage(FGameplayTag Stage)
{
    auto* ASC = BoundASC.Get();
    const auto* Row = FindStage(Stage);
    const auto* Entry = ASC ? ASC->FindAbilitySpecFromHandle(EntrySpec) : nullptr;
    if (!Row || !Entry || !IsContextCurrent())
    {
        ResetCombo();
        return false;
    }
    FGameplayAbilitySpecHandle Selected = EntrySpec;
    if (Stage != EntryStage)
    {
        if (!ReadyTags.HasTagExact(Row->RequiredTag) || !ASC->HasMatchingGameplayTag(Row->RequiredTag))
        {
            ResetCombo();
            return false;
        }
        if (Entry->Ability->IsA<UStanderWeaponActionAbility>())
        {
            Selected = {};
            const auto* Equipment = FindEquipment(*ASC);
            for (const auto& Spec : ASC->GetActivatableAbilities())
            {
                const auto* Weapon = Cast<UStanderWeaponActionAbility>(Spec.Ability);
                if (Equipment && Equipment->IsWeaponAbility(Spec.Handle) && Weapon && Weapon->ActionConfig &&
                    Weapon->ActionConfig->ActionTag == Row->WeaponActionTag)
                {
                    Selected = Spec.Handle;
                    break;
                }
            }
        }
    }
    ++Serial;
    SetComponentTickEnabled(false);
    const uint32 StartedSerial = Serial;
    ActiveSpec = Selected;
    CurrentStage = Stage;
    BufferedInput = {};
    bOpen = false;
    const bool bActivated = Selected.IsValid() && ASC->TryActivateAbility(Selected, false);
    if (!bActivated)
    {
        ResetCombo();
    }
    else if (Serial == StartedSerial && ActiveSpec.IsValid())
    {
        RemoveReadyTags();
    }
    return bActivated;
}

// 非连招动作以及入口维持原语义。后续段必须持有与目标 Spec 相符的接续凭证。
bool UStanderComboComponent::CanStartWeaponStage(FGameplayAbilitySpecHandle Spec, FGameplayTag Action) const
{
    const auto* Row = Stages.FindByPredicate([Action](const FStanderComboStage& Stage)
    {
        return Stage.WeaponActionTag == Action;
    });
    return !Row || Row->StageTag == EntryStage ||
           (Spec == ActiveSpec && CurrentStage == Row->StageTag && ReadyTags.HasTagExact(Row->RequiredTag) &&
            IsContextCurrent() && BoundASC->HasMatchingGameplayTag(Row->RequiredTag));
}

int32 UStanderComboComponent::DefaultStrikeIndex(FGameplayAbilitySpecHandle Spec) const
{
    const auto* Row = FindStage(Spec == ActiveSpec ? CurrentStage : EntryStage);
    return Row ? FMath::Clamp(Row->DefaultStrikeIndex, 0, 2) : 0;
}

bool UStanderComboComponent::UpdateStage(FGameplayAbilitySpecHandle Spec, bool bAfterContact, float RemainingSeconds)
{
    if (Spec != ActiveSpec)
    {
        return true;
    }
    if (!IsContextCurrent())
    {
        return false;
    }
    bOpen = bAfterContact && RemainingSeconds <= BufferSeconds;
    return true;
}

// 此处已结束本次 GA；下一帧再消费旧缓冲，避免在动画和 GAS 的完成委托内重入。
void UStanderComboComponent::FinishStage(FGameplayAbilitySpecHandle Spec, bool bCompleted)
{
    if (Spec != ActiveSpec)
    {
        return;
    }
    ActiveSpec = {};
    RemoveReadyTags();
    if (!bCompleted || !IsContextCurrent())
    {
        ResetCombo();
        return;
    }
    for (const auto& Edge : Transitions)
    {
        if (Edge.From == CurrentStage)
        {
            ReadyTags.AddTag(FindStage(Edge.To)->RequiredTag);
        }
    }
    if (ReadyTags.IsEmpty())
    {
        ResetCombo();
        return;
    }
    BoundASC->AddLooseGameplayTags(ReadyTags);
    ReadyUntil = Now() + ResetSeconds;
    SetComponentTickEnabled(true);
    const uint32 FinishedSerial = Serial;
    GetWorld()->GetTimerManager().SetTimerForNextTick(
        FTimerDelegate::CreateUObject(this, &ThisClass::ContinueBuffered, FinishedSerial));
}

// 只在接续间隙计时；用与输入相同的时钟，世界变速也不会提早回收。
void UStanderComboComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Tick)
{
    Super::TickComponent(DeltaTime, TickType, Tick);
    if (!IsContextCurrent() || Now() > ReadyUntil)
    {
        ResetCombo();
    }
}

void UStanderComboComponent::ContinueBuffered(uint32 ExpectedSerial)
{
    if (Serial == ExpectedSerial && BufferedInput.IsValid())
    {
        const auto Input = BufferedInput;
        BufferedInput = {};
        RequestComboInput(Input);
    }
}

void UStanderComboComponent::RemoveReadyTags()
{
    if (auto* ASC = BoundASC.Get())
    {
        ASC->RemoveLooseGameplayTags(ReadyTags);
    }
    ReadyTags.Reset();
}

// 已经实际激活的另一项主动技能结束旧连招的接续资格。被门禁拒绝的输入不清除状态。
void UStanderComboComponent::OnOtherAbilityActivated(UGameplayAbility* Ability)
{
    if (!Ability || ActiveSpec.IsValid())
    {
        return;
    }
    auto* ASC = BoundASC.Get();
    const auto* Spec = ASC ? ASC->FindAbilitySpecFromHandle(Ability->GetCurrentAbilitySpecHandle()) : nullptr;
    if (Spec && UMyAbilitySystemComponent::GetInputTagFromSpec(*Spec).IsValid())
    {
        ResetCombo();
    }
}

void UStanderComboComponent::OnOwnerDied(AActor* Actor)
{
    ResetCombo();
}

void UStanderComboComponent::ResetCombo()
{
    SetComponentTickEnabled(false);
    ++Serial;
    RemoveReadyTags();
    if (auto* ASC = BoundASC.Get())
    {
        ASC->AbilityActivatedCallbacks.Remove(ActivationListener);
    }
    ActivationListener.Reset();
    if (auto* Player = Cast<AStanderPlayer>(GetOwner()))
    {
        Player->GetOnDeathDelegate().RemoveDynamic(this, &ThisClass::OnOwnerDied);
    }
    BoundASC.Reset();
    Source = {};
    ActiveSpec = {};
    EntrySpec = {};
    CurrentStage = {};
    BufferedInput = {};
    ReadyUntil = 0.;
    bOpen = false;
}

void UStanderComboComponent::EndPlay(EEndPlayReason::Type Reason)
{
    ResetCombo();
    Super::EndPlay(Reason);
}
