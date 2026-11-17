// 调用链：Automation 真实背包换装 -> 生产武器 GA/蒙太奇/Timeline -> 共享接触动作 -> Mass 扣血或周身 Niagara。
// 不直接调用共享动作，不覆盖技能CDO或动作DA；移除生产GA传递的装备技能身份时，本测试必须失败。
#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/AbilityTimelineTestAbility.h"
#include "AbilitySystem/Ability/StanderWeaponActionAbility.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystem/Timeline/AbilityTimelineDefinition.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Backpack/BackpackComponent.h"
#include "Backpack/BackpackPersistence.h"
#include "Backpack/ItemDefinition.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"
#include "Equipment/UniqueEquipmentGameplayEffect.h"
#include "Equipment/WeaponSetConfig.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "MyGameplayTags.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "PureMass/PureMassSubsystem.h"
#include "TimerManager.h"
#include "UObject/Script.h"

namespace UniqueEquipmentMeleeIntegrationTests
{
    /** 与现有Timeline运行时测试一致的真实玩家绑定；销毁临时世界清理任务、装备和ASC。 */
    struct FFixture
    {
        UWorld* World = nullptr;
        AMyPlayerState* State = nullptr;
        AAbilityTimelineTestStander* Pawn = nullptr;
        UMyAbilitySystemComponent* ASC = nullptr;
        UBackpackComponent* Bag = nullptr;
        UEquipmentComponent* Equipment = nullptr;

        FFixture()
        {
            World = UWorld::CreateWorld(EWorldType::Game, false);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            State = World->SpawnActor<AMyPlayerState>();
            Pawn = World->SpawnActor<AAbilityTimelineTestStander>();
            Pawn->AttachState(State);
            ASC = CastChecked<UMyAbilitySystemComponent>(State->GetAbilitySystemComponent());
            ASC->AddAttributeSetSubobject(CastChecked<UMyAttributeSet>(State->GetAttributeSet()));
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 1000.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 1000.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetCriticalHitChanceAttribute(), 0.f);
            Bag = State->GetBackpackComponent();
            Equipment = State->GetEquipmentComponent();
        }

        ~FFixture()
        {
            ASC->CancelAllAbilities();
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }

        /** 只推进原GA所持有的任务；不人工派发攻击事件、不直接调用ExecuteStrike。 */
        void Advance(UStanderWeaponActionAbility* Ability, int32 Frames)
        {
            for (int32 Frame = 0; Frame < Frames && Ability->IsActive(); ++Frame)
            {
                Pawn->GetMesh()->TickAnimation(.01f, false);
                Pawn->GetMesh()->GetAnimInstance()->DispatchQueuedAnimEvents();
                if (auto* Task = Ability->GetActiveTimelineTask())
                {
                    Task->TickTask(.01f);
                }
                World->GetTimerManager().Tick(.01f);
            }
        }
    };
} // namespace UniqueEquipmentMeleeIntegrationTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUniqueStanderAttackTimelineIntegration,
                                 "test_25d.Equipment.Unique.StanderAttackTimelineIntegration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 使用真实装备事务与实际Mass抗性区分“只改倍率”和“真正从物理转为火焰”，同时验证卸装还原。
bool FUniqueStanderAttackTimelineIntegration::RunTest(const FString&)
{
    using namespace UniqueEquipmentMeleeIntegrationTests;
    auto* AttackClass = LoadClass<UStanderWeaponActionAbility>(
        nullptr, TEXT("/Game/BluePrints/Characters/Stander/Weapons/Sword_Shield/GA_Attack1.GA_Attack1_C"));
    auto* WeaponSet = LoadObject<UWeaponSetConfig>(
        nullptr, TEXT("/Game/BluePrints/Characters/Stander/Weapons/Sword_Shield/DA_WeaponSet.DA_WeaponSet"));
    auto* Catalog = LoadObject<UBackpackItemDefinitions>(
        nullptr, TEXT("/Game/BluePrints/DA/DA_BackpackItemDefinitions.DA_BackpackItemDefinitions"));
    auto* MeshAsset = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/PlatformAnimation/Models/Stander.Stander"));
    if (!TestNotNull(TEXT("Production Sword_Shield GA_Attack1"), AttackClass) ||
        !TestNotNull(TEXT("Production weapon set"), WeaponSet) ||
        !TestNotNull(TEXT("Production item catalog"), Catalog) ||
        !TestNotNull(TEXT("Stander skeletal mesh"), MeshAsset))
    {
        return false;
    }
    const FPrimaryAssetId BootsId(TEXT("BackpackItem"), TEXT("UniqueCinderOathBoots"));
    const auto* Boots = Catalog->FindDefinition(BootsId);
    const auto* BootsEffect =
        Boots ? Cast<UUniqueEquipmentGameplayEffect>(Boots->EquipEffect.GetDefaultObject()) : nullptr;
    if (!TestNotNull(TEXT("Authored Cinder Oath boots definition"), Boots) ||
        !TestNotNull(TEXT("Authored boots use unique gameplay effect"), BootsEffect))
    {
        return false;
    }
    FEditorScriptExecutionGuard ScriptGuard;
    FFixture F;
    F.Pawn->GetMesh()->SetSkeletalMeshAsset(MeshAsset);
    F.Pawn->DefaultWeaponSet = WeaponSet;
    F.Bag->SetItemDefinitions(Catalog);
    F.Bag->Initialize();
    F.Bag->SetCapacity(4);
    if (!TestTrue(TEXT("Default Sword_Shield grants its real abilities"), F.Equipment->ReconcileEffects()) ||
        !TestNotNull(TEXT("Weapon appearance supplies a real AnimInstance"), F.Pawn->GetMesh()->GetAnimInstance()))
    {
        return false;
    }
    auto* Spec = F.ASC->FindAbilitySpecFromClass(AttackClass);
    auto* Ability = Spec ? Cast<UStanderWeaponActionAbility>(Spec->GetPrimaryInstance()) : nullptr;
    if (!TestNotNull(TEXT("Equipment component grants real Attack1 instance"), Ability) ||
        !TestNotNull(TEXT("Production action configuration"), Ability->ActionConfig.Get()) ||
        !TestNotNull(TEXT("Production Attack1 timeline"), Ability->ActionConfig->Timeline.Get()))
    {
        return false;
    }
    const auto Handle = Spec->Handle;
    const auto* Config = Ability->ActionConfig.Get();
    TestTrue(TEXT("Granted handle belongs to the equipment subsystem"), F.Equipment->IsWeaponAbility(Handle));
    TestEqual(TEXT("Authored boots target this actual action identity"), BootsEffect->MeleeRule.AbilityTag,
              Config->ActionTag);
    TestEqual(TEXT("Actual unmodified GA damage type is physical"),
              Ability->MakeDamageEffectParamsFromClassDefaults().DamageType, FMyGameplayTags::Get().Damage_Physics);
    float AuthoredDamage = 0.f;
    float MinRadius = MAX_flt;
    for (const auto& Event : Config->Events)
    {
        if (Event.Kind == EStanderWeaponActionKind::Melee)
        {
            AuthoredDamage += Event.Damage;
            MinRadius = FMath::Min(MinRadius, Event.Radius);
        }
        TestFalse(TEXT("Attack1 fixture is a stationary attack"), Event.Kind == EStanderWeaponActionKind::Move);
    }
    if (!TestTrue(TEXT("Current authored melee damage and reach are meaningful"),
                  AuthoredDamage > 0.f && MinRadius > 0.f))
    {
        return false;
    }
    FAbilityTimelineSnapshot Timing;
    FString Error;
    if (!TestTrue(TEXT("Current authored timing resolves"),
                  Config->Timeline->ResolveMontage(Config->Montage, Timing, Error)))
    {
        return false;
    }
    const int32 Frames = FMath::CeilToInt((Timing.ElapsedAt(Config->Montage->GetPlayLength()) + 1.f) / .01f);
    auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(F.World);
    Type->Health = FMath::Max(1000.f, AuthoredDamage * 100.f);
    Type->Speed = 0.f;
    Type->XPReward = 0;
    Type->Armor = Type->BlockChance = Type->CriticalResistance = 0.f;
    Type->Resistances.Add(FMyGameplayTags::Get().Damage_Physics, 50.f);
    Type->Resistances.Add(FMyGameplayTags::Get().Damage_Fire, 0.f);
    const int32 Target = Mass->SpawnMonster(Type, F.Pawn->GetActorLocation() + FVector(MinRadius * .5f, 0, 0));
    FPureMassView Before;
    if (!TestTrue(TEXT("Stationary living Mass target exists"), Mass->Resolve(Target, Before)))
    {
        return false;
    }
    const auto BootsSlot = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot.Boots"));
    if (!TestTrue(TEXT("Real boots enter the production backpack"), F.Bag->TryAddItem(BootsId, 1).bSucceeded))
    {
        return false;
    }
    const auto& BagSlots = F.Bag->GetInventoryModel()->GetSlots();
    const int32 BagIndex = BagSlots.IndexOfByPredicate(
        [&BootsId](const FInventorySlot& Entry)
        {
            return !Entry.IsEmpty() && Entry.ItemDefinitionId == BootsId;
        });
    if (!TestTrue(TEXT("Production inventory retained a distinct boots instance"), BagIndex != INDEX_NONE))
    {
        return false;
    }
    const FGuid InstanceId = BagSlots[BagIndex].ItemInstanceId;
    if (!TestTrue(TEXT("Production equip transaction applies boots GE"),
                  F.Equipment->TryEquip(BagIndex, BootsSlot, InstanceId, F.Bag->GetRevision(), F.Equipment->Revision)))
    {
        return false;
    }
    const float Multiplier = BootsEffect->MeleeRule.DamageMultiplier.GetValueAtLevel(Boots->EquipEffectLevel);
    TestTrue(TEXT("Authored elemental conversion is a meaningful damage boost"), Multiplier > 1.f);
    bool bEndedNormally = false;
    const auto EndHandle = F.ASC->OnAbilityEnded.AddLambda(
        [&](const FAbilityEndedData& Ended)
        {
            if (Ended.AbilitySpecHandle == Handle)
            {
                bEndedNormally = !Ended.bWasCancelled;
            }
        });
    F.Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    TestTrue(TEXT("Real equipped Attack1 activates"), F.ASC->TryActivateAbility(Handle));
    TestTrue(TEXT("Real Attack1 plays its authored montage"), F.ASC->GetCurrentMontage() == Config->Montage);
    F.Advance(Ability, Frames);
    TestTrue(TEXT("Real attack finishes through its timeline"), bEndedNormally);
    TestFalse(TEXT("Finished attack is no longer active"), Ability->IsActive());
    FPureMassView AfterEquipped;
    TestTrue(TEXT("Target survives the equipped attack"), Mass->Resolve(Target, AfterEquipped));
    const float EquippedDamage = Before.Health - AfterEquipped.Health;
    TestTrue(
        TEXT(
            "Production GA forwards skill identity: actual fire damage equals authored event damage times boots multiplier"),
        FMath::IsNearlyEqual(EquippedDamage, AuthoredDamage * Multiplier, .01f));

    // 自然结束后使用同一实例执行生产卸装，默认剑盾与已装备剑盾都保留同一武器GA路径。
    F.ASC->CancelAbilityHandle(Handle);
    if (!TestTrue(TEXT("Production unequip removes exact boots instance"),
                  F.Equipment->TryUnequip(BootsSlot, InstanceId, F.Bag->GetRevision(), F.Equipment->Revision)))
    {
        F.ASC->OnAbilityEnded.Remove(EndHandle);
        return false;
    }
    TestNotNull(TEXT("Boots removal preserves original weapon ability handle"),
                F.ASC->FindAbilitySpecFromHandle(Handle));
    if (Config->CooldownTag.IsValid())
    {
        F.ASC->RemoveActiveEffectsWithGrantedTags(FGameplayTagContainer(Config->CooldownTag));
    }
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 1000.f);
    F.Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    bEndedNormally = false;
    TestTrue(TEXT("Same real Attack1 reactivates after unequip"), F.ASC->TryActivateAbility(Handle));
    F.Advance(Ability, Frames);
    F.ASC->OnAbilityEnded.Remove(EndHandle);
    TestTrue(TEXT("Second original attack finishes normally"), bEndedNormally);
    FPureMassView AfterUnequipped;
    TestTrue(TEXT("Target survives restored physical attack"), Mass->Resolve(Target, AfterUnequipped));
    TestTrue(TEXT("Removing boots restores physical resistance and original authored damage"),
             FMath::IsNearlyEqual(AfterEquipped.Health - AfterUnequipped.Health, AuthoredDamage * .5f, .01f));
    TestTrue(TEXT("Configured action object remains the original shared DA"), Ability->ActionConfig == Config);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUniqueStormSetTimelineIntegration,
                                 "test_25d.Equipment.Unique.StormSetTimelineIntegration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 使用真实键1技能验证套装奖励只消费一次，拆任意件/恢复存档可逆，旧战靴只影响另一个动作。
bool FUniqueStormSetTimelineIntegration::RunTest(const FString&)
{
    using namespace UniqueEquipmentMeleeIntegrationTests;
    auto* StormClass = LoadClass<UStanderWeaponActionAbility>(
        nullptr, TEXT("/Game/BluePrints/Characters/Stander/Weapons/Sword_Shield/GA_Skill7_Around.GA_Skill7_Around_C"));
    auto* AttackClass = LoadClass<UStanderWeaponActionAbility>(
        nullptr, TEXT("/Game/BluePrints/Characters/Stander/Weapons/Sword_Shield/GA_Attack1.GA_Attack1_C"));
    auto* WeaponSet = LoadObject<UWeaponSetConfig>(
        nullptr, TEXT("/Game/BluePrints/Characters/Stander/Weapons/Sword_Shield/DA_WeaponSet.DA_WeaponSet"));
    auto* Catalog = LoadObject<UBackpackItemDefinitions>(
        nullptr, TEXT("/Game/BluePrints/DA/DA_BackpackItemDefinitions.DA_BackpackItemDefinitions"));
    auto* MeshAsset = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/PlatformAnimation/Models/Stander.Stander"));
    if (!TestNotNull(TEXT("Production Sword_Shield Skill7_Around"), StormClass) ||
        !TestNotNull(TEXT("Production Sword_Shield Attack1"), AttackClass) ||
        !TestNotNull(TEXT("Production Sword_Shield weapon set"), WeaponSet) ||
        !TestNotNull(TEXT("Production unique item catalog"), Catalog) ||
        !TestNotNull(TEXT("Production Stander mesh"), MeshAsset))
    {
        return false;
    }
    const FPrimaryAssetId HelmId(TEXT("BackpackItem"), TEXT("UniqueStormforgedHelm"));
    const FPrimaryAssetId MailId(TEXT("BackpackItem"), TEXT("UniqueStormboundMail"));
    const FPrimaryAssetId BootsId(TEXT("BackpackItem"), TEXT("UniqueCinderOathBoots"));
    const auto* Helm = Catalog->FindDefinition(HelmId);
    const auto* Mail = Catalog->FindDefinition(MailId);
    const auto* Boots = Catalog->FindDefinition(BootsId);
    const auto* HelmEffect =
        Helm ? Cast<UUniqueEquipmentGameplayEffect>(Helm->EquipEffect.GetDefaultObject()) : nullptr;
    const auto* BootsEffect =
        Boots ? Cast<UUniqueEquipmentGameplayEffect>(Boots->EquipEffect.GetDefaultObject()) : nullptr;
    if (!TestNotNull(TEXT("Authored storm helm"), HelmEffect) || !TestNotNull(TEXT("Authored storm mail"), Mail) ||
        !TestNotNull(TEXT("Authored existing cinder boots"), BootsEffect))
    {
        return false;
    }
    const float SingleMultiplier = HelmEffect->MeleeRule.DamageMultiplier.GetValueAtLevel(Helm->EquipEffectLevel);
    const float SetMultiplier = HelmEffect->MeleeRule.SetDamageMultiplier.GetValueAtLevel(Helm->EquipEffectLevel);
    const float FireMultiplier = BootsEffect->MeleeRule.DamageMultiplier.GetValueAtLevel(Boots->EquipEffectLevel);
    TestTrue(TEXT("Current authored set improves its single piece"), SetMultiplier > SingleMultiplier);
    const auto& Tags = FMyGameplayTags::Get();
    TestEqual(TEXT("Storm helm is an actual thunder conversion"), HelmEffect->MeleeRule.DamageTypeOverride,
              Tags.Damage_Thunder);
    TestEqual(TEXT("Existing boots remain an actual fire conversion"), BootsEffect->MeleeRule.DamageTypeOverride,
              Tags.Damage_Fire);

    FEditorScriptExecutionGuard ScriptGuard;
    FFixture F;
    F.Pawn->GetMesh()->SetSkeletalMeshAsset(MeshAsset);
    F.Pawn->DefaultWeaponSet = WeaponSet;
    F.Bag->SetItemDefinitions(Catalog);
    F.Bag->Initialize();
    F.Bag->SetCapacity(8);
    if (!TestTrue(TEXT("Real weapon set grants both tested abilities"), F.Equipment->ReconcileEffects()) ||
        !TestNotNull(TEXT("Real weapon set supplies its animation instance"), F.Pawn->GetMesh()->GetAnimInstance()))
    {
        return false;
    }

    struct FMeasuredAction
    {
        UClass* Class = nullptr;
        const UStanderWeaponActionConfig* Config = nullptr;
        float Damage = 0.f;
        float MinRadius = MAX_flt;
        float MaxRadius = 0.f;
        int32 Frames = 0;
    };
    // 原始数值和Timeline长度直接读取当前生产DA，不复制技能事件或人工派发接触回调。
    const auto MeasureAction = [this, &F](UClass* Class, FMeasuredAction& Out)
    {
        const auto* Spec = F.ASC->FindAbilitySpecFromClass(Class);
        const auto* Ability = Spec ? Cast<UStanderWeaponActionAbility>(Spec->GetPrimaryInstance()) : nullptr;
        if (!TestNotNull(TEXT("Production ability instance exists"), Ability) ||
            !TestNotNull(TEXT("Production action definition exists"), Ability->ActionConfig.Get()) ||
            !TestNotNull(TEXT("Production action timeline exists"), Ability->ActionConfig->Timeline.Get()))
        {
            return false;
        }
        Out.Class = Class;
        Out.Config = Ability->ActionConfig;
        TestEqual(TEXT("Original configured action damage type is physical"),
                  Ability->MakeDamageEffectParamsFromClassDefaults().DamageType, FMyGameplayTags::Get().Damage_Physics);
        for (const auto& Event : Out.Config->Events)
        {
            if (Event.Kind == EStanderWeaponActionKind::Melee)
            {
                Out.Damage += Event.Damage;
                Out.MinRadius = FMath::Min(Out.MinRadius, Event.Radius);
                Out.MaxRadius = FMath::Max(Out.MaxRadius, Event.Radius);
            }
            TestFalse(TEXT("Tested authored contact action does not move its source"),
                      Event.Kind == EStanderWeaponActionKind::Move);
        }
        FAbilityTimelineSnapshot Timing;
        FString Error;
        if (!TestTrue(TEXT("Current real timeline resolves with its real montage"),
                      Out.Config->Timeline->ResolveMontage(Out.Config->Montage, Timing, Error)) ||
            !TestTrue(TEXT("Current action has meaningful damage and reach"),
                      Out.Damage > 0.f && Out.MinRadius > 0.f && Out.MaxRadius >= Out.MinRadius))
        {
            return false;
        }
        Out.Frames = FMath::CeilToInt((Timing.ElapsedAt(Out.Config->Montage->GetPlayLength()) + 1.f) / .01f);
        return true;
    };
    FMeasuredAction Storm;
    FMeasuredAction Attack;
    if (!MeasureAction(StormClass, Storm) || !MeasureAction(AttackClass, Attack))
    {
        return false;
    }
    TestEqual(TEXT("Helm consumes the exact real Skill7 identity"), HelmEffect->MeleeRule.AbilityTag,
              Storm.Config->ActionTag);
    TestEqual(TEXT("Boots consume only the real Attack1 identity"), BootsEffect->MeleeRule.AbilityTag,
              Attack.Config->ActionTag);
    TestTrue(TEXT("Storm action exercises a real mana cost"), Storm.Config->ManaCost > 0.f);

    auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(F.World);
    Type->Health = FMath::Max(5000.f, (Storm.Damage + Attack.Damage) * 50.f);
    Type->Speed = 0.f;
    Type->XPReward = 0;
    Type->Armor = Type->BlockChance = Type->CriticalResistance = 0.f;
    Type->Resistances.Add(Tags.Damage_Physics, 50.f);
    Type->Resistances.Add(Tags.Damage_Thunder, 0.f);
    // 火抗与雷抗不同，证明旧战靴保留火焰类型，而非错误继承新套装的雷伤。
    Type->Resistances.Add(Tags.Damage_Fire, 25.f);
    const FVector Origin = F.Pawn->GetActorLocation();
    const int32 Near =
        Mass->SpawnMonster(Type, Origin + FVector(FMath::Min(Storm.MinRadius, Attack.MinRadius) * .5f, 0, 0));
    const int32 Far =
        Mass->SpawnMonster(Type, Origin + FVector(FMath::Max(Storm.MaxRadius, Attack.MaxRadius) + 250.f, 0, 0));

    // 每次重新查真实Spec，避免存档恢复后持有已撤销的能力句柄；只清理该动作冷却以隔离场景。
    const auto RunAttack =
        [this, &F, Mass, Near, Far](const TCHAR* Scenario, const FMeasuredAction& Action, float ExpectedMultiplier)
    {
        const auto Label = [Scenario](const TCHAR* Check)
        {
            return FString::Printf(TEXT("%s: %s"), Scenario, Check);
        };
        const auto* Spec = F.ASC->FindAbilitySpecFromClass(Action.Class);
        auto* Ability = Spec ? Cast<UStanderWeaponActionAbility>(Spec->GetPrimaryInstance()) : nullptr;
        if (!TestNotNull(Label(TEXT("real granted ability survives current equipment state")), Ability))
        {
            return false;
        }
        const auto Handle = Spec->Handle;
        if (Action.Config->CooldownTag.IsValid())
        {
            F.ASC->RemoveActiveEffectsWithGrantedTags(FGameplayTagContainer(Action.Config->CooldownTag));
        }
        const float MaxMana = F.ASC->GetNumericAttribute(UMyAttributeSet::GetMaxManaAttribute());
        F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), MaxMana);
        F.Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        FPureMassView NearBefore;
        FPureMassView FarBefore;
        if (!TestTrue(Label(TEXT("near and far living targets exist")),
                      Mass->Resolve(Near, NearBefore) && Mass->Resolve(Far, FarBefore)))
        {
            return false;
        }
        bool bEndedNormally = false;
        const auto EndHandle = F.ASC->OnAbilityEnded.AddLambda(
            [&](const FAbilityEndedData& Ended)
            {
                if (Ended.AbilitySpecHandle == Handle)
                {
                    bEndedNormally = !Ended.bWasCancelled;
                }
            });
        const bool bActivated = TestTrue(Label(TEXT("real GA activates")), F.ASC->TryActivateAbility(Handle));
        if (bActivated)
        {
            TestTrue(Label(TEXT("real authored montage plays")), F.ASC->GetCurrentMontage() == Action.Config->Montage);
            F.Advance(Ability, Action.Frames);
        }
        F.ASC->OnAbilityEnded.Remove(EndHandle);
        if (!bActivated)
        {
            return false;
        }
        bool bPassed = TestTrue(Label(TEXT("real timeline ends normally")), bEndedNormally);
        bPassed &= TestFalse(Label(TEXT("finished GA is inactive")), Ability->IsActive());
        bPassed &=
            TestTrue(Label(TEXT("actual mana payment equals original configured cost")),
                     FMath::IsNearlyEqual(MaxMana - F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()),
                                          Action.Config->ManaCost, .01f));
        FPureMassView NearAfter;
        FPureMassView FarAfter;
        if (!TestTrue(Label(TEXT("both targets survive")),
                      Mass->Resolve(Near, NearAfter) && Mass->Resolve(Far, FarAfter)))
        {
            return false;
        }
        bPassed &= TestTrue(
            Label(TEXT("actual damage reflects current element, multiplier and original contact count")),
            FMath::IsNearlyEqual(NearBefore.Health - NearAfter.Health, Action.Damage * ExpectedMultiplier, .05f));
        bPassed &=
            TestEqual(Label(TEXT("equipment does not extend authored reach")), FarAfter.Health, FarBefore.Health);
        bPassed &= TestTrue(Label(TEXT("shared action DA remains the original object")),
                            Ability->ActionConfig == Action.Config);
        return bPassed;
    };
    const auto EquipExisting = [this, &F](const FPrimaryAssetId& Id, FGameplayTag Slot)
    {
        const auto& Slots = F.Bag->GetInventoryModel()->GetSlots();
        const int32 Index = Slots.IndexOfByPredicate(
            [&Id](const FInventorySlot& Entry)
            {
                return !Entry.IsEmpty() && Entry.ItemDefinitionId == Id;
            });
        return TestTrue(TEXT("Existing exact inventory instance equips through production revisions"),
                        Index != INDEX_NONE && F.Equipment->TryEquip(Index, Slot, Slots[Index].ItemInstanceId,
                                                                     F.Bag->GetRevision(), F.Equipment->Revision));
    };
    const auto AddAndEquip = [this, &F, &EquipExisting](const FPrimaryAssetId& Id, FGameplayTag Slot)
    {
        return TestTrue(TEXT("Authored item enters real backpack"), F.Bag->TryAddItem(Id, 1).bSucceeded) &&
               EquipExisting(Id, Slot);
    };
    const auto Unequip = [this, &F](FGameplayTag Slot)
    {
        const auto* Entry = F.Equipment->Slots.FindByPredicate(
            [Slot](const FEquipmentSlot& Candidate)
            {
                return Candidate.SlotTag == Slot;
            });
        return TestTrue(
            TEXT("Exact equipped instance unequips through production revisions"),
            Entry && F.Equipment->TryUnequip(Slot, Entry->ItemInstanceId, F.Bag->GetRevision(), F.Equipment->Revision));
    };
    const auto HelmSlot = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot.Helmet"));
    const auto MailSlot = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot.Chest"));
    const auto BootsSlot = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot.Boots"));
    if (!RunAttack(TEXT("No storm equipment"), Storm, .5f) || !AddAndEquip(HelmId, HelmSlot) ||
        !RunAttack(TEXT("Storm helm alone"), Storm, SingleMultiplier) || !AddAndEquip(MailId, MailSlot) ||
        !RunAttack(TEXT("Complete storm set"), Storm, SetMultiplier) || !AddAndEquip(BootsId, BootsSlot) ||
        !RunAttack(TEXT("Storm set alongside old fire boots"), Storm, SetMultiplier) ||
        !RunAttack(TEXT("Old Attack1 alongside storm set uses fire resistance"), Attack, FireMultiplier * .75f) ||
        !Unequip(HelmSlot) || !RunAttack(TEXT("Mail alone cannot modify Skill7"), Storm, .5f) ||
        !EquipExisting(HelmId, HelmSlot) || !RunAttack(TEXT("Re-equipped helm restores set"), Storm, SetMultiplier) ||
        !Unequip(MailSlot) || !RunAttack(TEXT("Removing mail restores helm-only damage"), Storm, SingleMultiplier) ||
        !EquipExisting(MailId, MailSlot))
    {
        return false;
    }
    // 真实联合存档重建只能产生一份套装收益；不人工重新施加GE或授予技能。
    const auto Snapshot = F.State->ExportBackpackSnapshot();
    if (!TestTrue(TEXT("Real combined persistence restores storm set with old boots"),
                  F.State->ImportBackpackSnapshot(Snapshot)) ||
        !TestTrue(TEXT("Repeated persistence restore is idempotent"), F.State->ImportBackpackSnapshot(Snapshot)) ||
        !RunAttack(TEXT("Restored set consumes its reward once"), Storm, SetMultiplier) ||
        !RunAttack(TEXT("Restored old boots retain fire Attack1"), Attack, FireMultiplier * .75f) ||
        !Unequip(HelmSlot) || !Unequip(MailSlot) || !Unequip(BootsSlot) ||
        !RunAttack(TEXT("All uniques removed restores original Skill7"), Storm, .5f))
    {
        return false;
    }
    TestFalse(
        TEXT("Helm identity is removed with its last instance"),
        F.ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Equipment.Unique.StormforgedHelm"))));
    TestFalse(TEXT("Mail identity is removed with its last instance"),
              F.ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Equipment.Unique.StormboundMail"))));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUniqueMeleeBodyEffectIntegration,
                                 "test_25d.Equipment.Unique.WeaponBodyEffectIntegration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 两个生产武器 GA 在空场地完整施放。可渲染进程观察根组件，验证每次接触生成一次并按期回收。
bool FUniqueMeleeBodyEffectIntegration::RunTest(const FString&)
{
    using namespace UniqueEquipmentMeleeIntegrationTests;
    FEditorScriptExecutionGuard ScriptGuard;
    auto* Catalog = LoadObject<UBackpackItemDefinitions>(
        nullptr, TEXT("/Game/BluePrints/DA/DA_BackpackItemDefinitions.DA_BackpackItemDefinitions"));
    auto* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/PlatformAnimation/Models/Stander.Stander"));
    if (!TestNotNull(TEXT("Production catalog"), Catalog) || !TestNotNull(TEXT("Production Stander mesh"), Mesh))
    {
        return false;
    }

    const bool bCanRender = FApp::CanEverRender();
    if (!bCanRender)
    {
        AddInfo(TEXT("No render device: validating both real GA casts; rerun with an RHI to exercise attached Niagara."));
    }
    const auto RunWeapon = [this, Catalog, Mesh, bCanRender](
                               const TCHAR* Family, const TCHAR* Skill, FName Gear, const TCHAR* SlotTag)
    {
        const FString Folder = FString::Printf(TEXT("/Game/BluePrints/Characters/Stander/Weapons/%s/"), Family);
        auto* Set = LoadObject<UWeaponSetConfig>(nullptr, *(Folder + TEXT("DA_WeaponSet.DA_WeaponSet")));
        auto* Class = LoadClass<UStanderWeaponActionAbility>(
            nullptr, *FString::Printf(TEXT("%sGA_%s.GA_%s_C"), *Folder, Skill, Skill));
        const FPrimaryAssetId ItemId(TEXT("BackpackItem"), Gear);
        const auto* Item = Catalog->FindDefinition(ItemId);
        const auto* Effect = Item ? Cast<UUniqueEquipmentGameplayEffect>(Item->EquipEffect.GetDefaultObject()) : nullptr;
        if (!TestNotNull(TEXT("Production weapon set"), Set) || !TestNotNull(TEXT("Production skill class"), Class) ||
            !TestNotNull(TEXT("Production item effect"), Effect) ||
            !TestNotNull(TEXT("Production attached system"), Effect->MeleeRule.BodyEffect.Get()))
        {
            return false;
        }

        FFixture F;
        F.Pawn->GetMesh()->SetSkeletalMeshAsset(Mesh);
        F.Pawn->DefaultWeaponSet = Set;
        F.Bag->SetItemDefinitions(Catalog);
        F.Bag->Initialize();
        F.Bag->SetCapacity(4);
        if (!TestTrue(TEXT("Production weapon set equips with its actions"), F.Equipment->ReconcileEffects()) ||
            !TestNotNull(TEXT("Real weapon animation instance"), F.Pawn->GetMesh()->GetAnimInstance()) ||
            !TestTrue(TEXT("Production backpack accepts the unique"), F.Bag->TryAddItem(ItemId, 1).bSucceeded))
        {
            return false;
        }
        const auto* Spec = F.ASC->FindAbilitySpecFromClass(Class);
        auto* Ability = Spec ? Cast<UStanderWeaponActionAbility>(Spec->GetPrimaryInstance()) : nullptr;
        if (!TestNotNull(TEXT("Real equipped ability instance"), Ability) ||
            !TestNotNull(TEXT("Actual action configuration"), Ability->ActionConfig.Get()) ||
            !TestNotNull(TEXT("Actual action Timeline"), Ability->ActionConfig->Timeline.Get()))
        {
            return false;
        }
        const auto* Config = Ability->ActionConfig.Get();
        const auto Handle = Spec->Handle;
        const auto Slot = FGameplayTag::RequestGameplayTag(FName(SlotTag));
        const auto& Items = F.Bag->GetInventoryModel()->GetSlots();
        const int32 Index = Items.IndexOfByPredicate(
            [&ItemId](const FInventorySlot& Entry)
            {
                return Entry.ItemDefinitionId == ItemId;
            });
        if (!TestTrue(TEXT("Unique has an inventory instance"), Index != INDEX_NONE))
        {
            return false;
        }
        const FGuid Instance = Items[Index].ItemInstanceId;
        if (!TestTrue(TEXT("Equip the real unique with production revisions"),
                      F.Equipment->TryEquip(Index, Slot, Instance, F.Bag->GetRevision(), F.Equipment->Revision)))
        {
            return false;
        }

        FAbilityTimelineSnapshot Timing;
        FString Error;
        if (!TestTrue(TEXT("Timeline resolves the real montage"),
                      Config->Timeline->ResolveMontage(Config->Montage, Timing, Error)))
        {
            return false;
        }
        const int32 Frames = FMath::CeilToInt((Timing.ElapsedAt(Config->Montage->GetPlayLength()) + 1.f) / .01f);
        const auto* System = Effect->MeleeRule.BodyEffect.Get();
        const auto CountEffects = [&F, System](TSet<FName>& Instances)
        {
            int32 Live = 0;
            for (const auto& Child : F.Pawn->GetRootComponent()->GetAttachChildren())
            {
                const auto* Niagara = Cast<UNiagaraComponent>(Child);
                if (IsValid(Niagara) && Niagara->GetAsset() == System)
                {
                    Instances.Add(Niagara->GetFName());
                    ++Live;
                }
            }
            return Live;
        };
        // 采样每一帧的真实挂接物。GA 结束后继续推进世界时钟，让视觉租期独立完成。
        const auto CastWithoutTarget = [&, this](TSet<FName>& Instances)
        {
            F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 1000.f);
            F.Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            if (!TestTrue(TEXT("Production GA activates with no enemy"), F.ASC->TryActivateAbility(Handle)))
            {
                return false;
            }
            TestTrue(TEXT("Production GA plays its authored montage"), F.ASC->GetCurrentMontage() == Config->Montage);
            for (int32 Frame = 0; Frame < Frames; ++Frame)
            {
                ++GFrameCounter;
                if (Ability->IsActive())
                {
                    F.Advance(Ability, 1);
                }
                else
                {
                    F.World->GetTimerManager().Tick(.01f);
                }
                CountEffects(Instances);
            }
            TestEqual(TEXT("Attached effects expire after contact"), CountEffects(Instances), 0);
            return TestFalse(TEXT("Weapon cast finishes"), Ability->IsActive());
        };
        TSet<FName> EquippedEffects;
        if (!CastWithoutTarget(EquippedEffects))
        {
            return false;
        }
        if (bCanRender)
        {
            const int32 Contacts = Config->Events.FilterByPredicate(
                [](const FStanderWeaponActionEvent& Event)
                {
                    return Event.Kind == EStanderWeaponActionKind::Melee;
                }).Num();
            TestTrue(TEXT("Real action contains contacts"), Contacts > 0);
            TestEqual(TEXT("Each whiffed contact spawns one attached effect"), EquippedEffects.Num(), Contacts);
        }

        if (!TestTrue(TEXT("Exact unique instance unequips"),
                      F.Equipment->TryUnequip(Slot, Instance, F.Bag->GetRevision(), F.Equipment->Revision)))
        {
            return false;
        }
        if (Config->CooldownTag.IsValid())
        {
            F.ASC->RemoveActiveEffectsWithGrantedTags(FGameplayTagContainer(Config->CooldownTag));
        }
        TSet<FName> UnequippedEffects;
        return CastWithoutTarget(UnequippedEffects) &&
               TestEqual(TEXT("Future casts no longer spawn the unequipped effect"), UnequippedEffects.Num(), 0);
    };
    const bool bHammer = RunWeapon(TEXT("Hammer"), TEXT("Skill7_Around"), TEXT("UniqueFaultlineTreads"),
                                   TEXT("Equipment.Slot.Boots"));
    const bool bDualBlades = RunWeapon(TEXT("DualBlades"), TEXT("Skill6_Move"), TEXT("UniqueRimefangHelm"),
                                       TEXT("Equipment.Slot.Helmet"));
    return bHammer && bDualBlades;
}
#endif
