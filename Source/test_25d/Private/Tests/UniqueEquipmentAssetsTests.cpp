// 调用链：Automation AuthoredAssets → 真实目录/暗金 GE/曲线 → PlayerState 装备事务 → 属性、技能快照与实际弹体验证。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/DungeonPlayerRunTestTypes.h"
#include "Equipment/EquipmentComponent.h"
#include "Equipment/EquipmentGenerator.h"
#include "Equipment/EquipmentAbilityModifiers.h"
#include "Equipment/EquipmentMeleeModifiers.h"
#include "Equipment/EquipmentStats.h"
#include "Equipment/EquipmentSoulPickup.h"
#include "Equipment/UniqueEquipmentGameplayEffect.h"
#include "Backpack/BackpackComponent.h"
#include "Backpack/BackpackPersistence.h"
#include "Backpack/ItemDefinition.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystem/Ability/StanderWeaponActionAbility.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"
#include "AbilitySystem/Unified/Nodes/AbilityTraditionalNodeExecutions.h"
#include "AbilitySystemComponent.h"
#include "Actor/MyProjectile.h"
#include "Engine/CurveTable.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "MyGameplayTags.h"
#include "NiagaraSystem.h"

namespace UniqueEquipmentAssetsTests
{
    struct FExpectedModifier
    {
        FGameplayAttribute Attribute;
        float Multiplier;
    };

    struct FExpectedItem
    {
        FName Name;
        FGameplayTag Slot;
        TArray<FExpectedModifier> Modifiers;
    };

    // 用真实 PlayerState ASC 与已有测试玩家绑定，避免给测试临时装备重新实现第二套库存通路。
    struct FFixture
    {
        UWorld* World = nullptr;
        AMyPlayerState* State = nullptr;
        ADungeonPlayerRunTestPawn* Pawn = nullptr;
        UAbilitySystemComponent* ASC = nullptr;
        UBackpackComponent* Bag = nullptr;
        UEquipmentComponent* Equipment = nullptr;

        FFixture()
        {
            World = UWorld::CreateWorld(EWorldType::Game, false,
                                        MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            State = World->SpawnActor<AMyPlayerState>();
            Pawn = World->SpawnActor<ADungeonPlayerRunTestPawn>();
            if (!State || !Pawn)
            {
                return;
            }
            Pawn->AttachState(State);
            ASC = State->GetAbilitySystemComponent();
            ASC->AddAttributeSetSubobject(CastChecked<UMyAttributeSet>(State->GetAttributeSet()));
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetStrengthAttribute(), 20.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetIntelligenceAttribute(), 30.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetResilienceAttribute(), 40.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetVigorAttribute(), 50.f);
            Bag = State->GetBackpackComponent();
            Equipment = State->GetEquipmentComponent();
        }

        // 无 BeginPlay/AI/存档写入；世界销毁负责取消临时弹体和 ASC 生命周期。
        ~FFixture()
        {
            if (World)
            {
                GEngine->DestroyWorldContext(World);
                World->DestroyWorld(false);
            }
        }

        // 找到真实新增实例后提交生产双版本事务；相同定义的两个戒指仍有不同 GUID。
        bool AddAndEquip(FName Name, FGameplayTag Slot)
        {
            const FPrimaryAssetId Id(FPrimaryAssetType(TEXT("BackpackItem")), Name);
            if (!Bag->TryAddItem(Id, 1).bSucceeded)
            {
                return false;
            }
            const auto& Slots = Bag->GetInventoryModel()->GetSlots();
            const int32 Index = Slots.IndexOfByPredicate(
                [&Id](const FInventorySlot& Entry)
                {
                    return !Entry.IsEmpty() && Entry.ItemDefinitionId == Id;
                });
            return Index != INDEX_NONE && Equipment->TryEquip(Index, Slot, Slots[Index].ItemInstanceId,
                                                              Bag->GetRevision(), Equipment->Revision);
        }

        // 卸下精确槽中的实例，不按 GE 类或物品定义批量移除。
        bool Unequip(FGameplayTag Slot)
        {
            const auto* Equipped = Equipment->Slots.FindByPredicate(
                [Slot](const FEquipmentSlot& Entry)
                {
                    return Entry.SlotTag == Slot;
                });
            return Equipped &&
                   Equipment->TryUnequip(Slot, Equipped->ItemInstanceId, Bag->GetRevision(), Equipment->Revision);
        }

        // 保存所有受支持持久属性，发现卸装后派生值未恢复的跨属性残留。
        TArray<float> Values() const
        {
            TArray<float> Result;
            for (const auto& Stat : EquipmentStats::All())
            {
                Result.Add(ASC->GetNumericAttribute(Stat.Attribute));
            }
            return Result;
        }
    };
} // namespace UniqueEquipmentAssetsTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUniqueEquipmentAuthoredAssetsTest, "test_25d.Equipment.Unique.AuthoredAssets",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 实际作者资产是验收对象；缺失或曲线绑定错误应失败，不以 transient 替代品绕过制作验证。
bool FUniqueEquipmentAuthoredAssetsTest::RunTest(const FString& Parameters)
{
    using namespace UniqueEquipmentAssetsTests;
    auto* Catalog = LoadObject<UBackpackItemDefinitions>(
        nullptr, TEXT("/Game/BluePrints/DA/DA_BackpackItemDefinitions.DA_BackpackItemDefinitions"));
    auto* Curves = LoadObject<UCurveTable>(
        nullptr, TEXT("/Game/Equipment/Unique/CT_UniqueEquipmentStats.CT_UniqueEquipmentStats"));
    if (!TestNotNull(TEXT("Existing real item catalog"), Catalog) ||
        !TestNotNull(TEXT("Independent unique curve table"), Curves) ||
        !TestNotNull(TEXT("Real equipment metadata table"), Catalog->EquipmentTable.Get()))
    {
        return false;
    }
    TestTrue(TEXT("Equipment uses the established row schema"),
             Catalog->EquipmentTable->GetRowStruct() == FBackpackItemDefEntry::StaticStruct());

    const auto Left = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot.Ring.Left"));
    const auto Right = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot.Ring.Right"));
    const auto Chest = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot.Chest"));
    const auto Helmet = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot.Helmet"));
    const auto Stone = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot.MagicStone"));
    const auto Boots = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot.Boots"));
    const auto Element = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot.Element.1"));
    const auto CrownTag = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Unique.AshenCrown"));
    const auto PrismTag = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Unique.AshenPrism"));
    const TArray<FExpectedItem> Items = {
        {TEXT("UniqueBloodglassRing"),
         Left,
         {{UMyAttributeSet::GetArmorPeneAttribute(), 2.2f}, {UMyAttributeSet::GetMaxHealthAttribute(), .70f}}},
        {TEXT("UniqueStonePactArmor"),
         Chest,
         {{UMyAttributeSet::GetArmorAttribute(), 2.5f}, {UMyAttributeSet::GetMaxManaAttribute(), .60f}}},
        {TEXT("UniqueAshenCrown"),
         Helmet,
         {{UMyAttributeSet::GetMaxManaAttribute(), 2.f},
          {UMyAttributeSet::GetManaRegenerationAttribute(), 2.f},
          {UMyAttributeSet::GetMaxHealthAttribute(), .65f}}},
        {TEXT("UniqueAshenPrism"), Stone, {{UMyAttributeSet::GetMaxManaAttribute(), .75f}}},
        {TEXT("UniqueCinderOathBoots"), Boots, {{UMyAttributeSet::GetArmorAttribute(), .70f}}},
        {TEXT("UniqueStormforgedHelm"), Helmet, {{UMyAttributeSet::GetMaxManaAttribute(), .70f}}},
        {TEXT("UniqueStormboundMail"),
         Chest,
         {{UMyAttributeSet::GetMaxHealthAttribute(), 1.6f}, {UMyAttributeSet::GetArmorAttribute(), .65f}}},
        {TEXT("UniqueGamblersRing"),
         Left,
         {{UMyAttributeSet::GetCriticalHitChanceAttribute(), 2.f},
          {UMyAttributeSet::GetCriticalHitDamageAttribute(), 1.8f},
          {UMyAttributeSet::GetArmorAttribute(), .60f}}},
        {TEXT("UniqueStillwaterStone"),
         Stone,
         {{UMyAttributeSet::GetManaRegenerationAttribute(), 3.f},
          {UMyAttributeSet::GetMaxManaAttribute(), 1.5f},
          {UMyAttributeSet::GetCriticalHitDamageAttribute(), .50f}}},
        {TEXT("UniqueArcaneHorizonRobe"), Chest, {{UMyAttributeSet::GetArmorAttribute(), .75f}}},
        {TEXT("UniqueSingularityHood"), Helmet, {{UMyAttributeSet::GetMaxManaAttribute(), .80f}}},
        {TEXT("UniqueForkstormRing"), Left, {{UMyAttributeSet::GetManaRegenerationAttribute(), .75f}}},
        {TEXT("UniqueFrostWakeHelm"), Helmet, {{UMyAttributeSet::GetArmorAttribute(), .72f}}},
        {TEXT("UniqueSolarStepBoots"),
         Boots,
         {{UMyAttributeSet::GetArmorAttribute(), .85f}, {UMyAttributeSet::GetMaxManaAttribute(), .80f}}},
        {TEXT("UniqueNeedleflamePrism"), Stone, {{UMyAttributeSet::GetMaxManaAttribute(), .70f}}},
        {TEXT("UniqueStormLens"), Stone, {{UMyAttributeSet::GetMaxManaAttribute(), .82f}}},
        {TEXT("UniqueStormweaverMantle"),
         Chest,
         {{UMyAttributeSet::GetManaRegenerationAttribute(), 1.75f}, {UMyAttributeSet::GetArmorAttribute(), .65f}}},
        {TEXT("UniqueFaultlineTreads"), Boots, {{UMyAttributeSet::GetArmorAttribute(), .70f}}},
        {TEXT("UniqueRimefangHelm"), Helmet, {{UMyAttributeSet::GetMaxHealthAttribute(), .82f}}},
        {TEXT("UniqueEmberheartElement"),
         Element,
         {{UMyAttributeSet::GetStrengthAttribute(), 1.55f},
          {UMyAttributeSet::GetHealthRegenerationAttribute(), 1.80f},
          {UMyAttributeSet::GetMaxManaAttribute(), .65f}}}};

    FFixture F;
    if (!TestNotNull(TEXT("Authoritative player ASC"), F.ASC))
    {
        return false;
    }
    UClass* Secondary = LoadClass<UGameplayEffect>(
        nullptr, TEXT("/Game/BluePrints/AbilitySystem/Player/Effect/GE_SecondaryAttributes.GE_SecondaryAttributes_C"));
    if (!TestNotNull(TEXT("Real Channel0 derived attribute initialization"), Secondary))
    {
        return false;
    }
    auto Context = F.ASC->MakeEffectContext();
    Context.AddSourceObject(F.Pawn);
    auto Initialization = F.ASC->MakeOutgoingSpec(Secondary, 1.f, Context);
    if (!TestTrue(TEXT("Initialization spec exists"), Initialization.IsValid()) ||
        !TestTrue(TEXT("Real secondary GE is active"),
                  F.ASC->ApplyGameplayEffectSpecToSelf(*Initialization.Data).IsValid()))
    {
        return false;
    }
    F.Bag->SetItemDefinitions(Catalog);
    F.Bag->Initialize();
    F.Bag->SetCapacity(16);
    if (!TestTrue(TEXT("Equipment reconcile ready"), F.Equipment->ReconcileEffects()))
    {
        return false;
    }
    const FBackpackSnapshot Empty = F.State->ExportBackpackSnapshot();
    const auto Baseline = F.Values();
    const float MaxHealth = F.ASC->GetNumericAttribute(UMyAttributeSet::GetMaxHealthAttribute());
    const float MaxMana = F.ASC->GetNumericAttribute(UMyAttributeSet::GetMaxManaAttribute());
    const float ArmorPene = F.ASC->GetNumericAttribute(UMyAttributeSet::GetArmorPeneAttribute());
    const int32 BaseEffects = F.ASC->GetActiveEffects(FGameplayEffectQuery()).Num();
    TestTrue(TEXT("Derived resources produce a meaningful nonzero baseline"), MaxHealth > 0.f && MaxMana > 0.f);
    constexpr float HealthRatio = .6f;
    constexpr float ManaRatio = .25f;

    TMap<FName, const UUniqueEquipmentGameplayEffect*> Effects;
    for (const auto& Item : Items)
    {
        const FPrimaryAssetId Id(FPrimaryAssetType(TEXT("BackpackItem")), Item.Name);
        const auto* Definition = Catalog->FindDefinition(Id);
        if (!TestNotNull(*Item.Name.ToString(), Definition))
        {
            return false;
        }
        const auto* Effect = Cast<UUniqueEquipmentGameplayEffect>(Definition->EquipEffect.GetDefaultObject());
        if (!TestNotNull(TEXT("Authored effect uses dedicated unique class"), Effect))
        {
            return false;
        }
        Effects.Add(Item.Name, Effect);
        const FString ExpectedClass = FString::Printf(TEXT("/Game/Equipment/Unique/Effects/GE_%s.GE_%s_C"),
                                                      *Item.Name.ToString(), *Item.Name.ToString());
        TestEqual(TEXT("Stable definition references its authored GE"), Effect->GetClass()->GetPathName(),
                  ExpectedClass);
        TestTrue(TEXT("Real authored GE passes safe equipment validation"), Effect->IsSafeEquipmentEffect(1.f));
        TestFalse(TEXT("Fixed unique cannot use randomized validation"), Effect->IsSafeEquipmentEffect(1.f, true));
        TestTrue(TEXT("Fixed nonstackable equipment metadata"), Definition->bEquippable && !Definition->bStackable &&
                                                                    !Definition->bRandomizedEquipment &&
                                                                    !Definition->bUsable && Definition->MaxStack == 1);
        TestTrue(TEXT("Slot tag agrees with authored definition"),
                 Definition->AllowedEquipmentSlots.HasTagExact(Item.Slot));
        TestTrue(TEXT("Authored icon is an imported equipment texture"),
                 Definition->Icon && Definition->Icon->GetPathName().StartsWith(TEXT("/Game/Assets/EquipmentIcon/")));
        TestTrue(TEXT("Uses original Soul interactive pickup"),
                 Definition->PickupActorClass == AEquipmentSoulPickup::StaticClass() &&
                     Definition->PickupMode == EPickupMode::Interact);
        TestEqual(TEXT("Expected authored modifier count"), Effect->Modifiers.Num(), Item.Modifiers.Num());
        const FString Description = FEquipmentGenerator::MakeDescription(Definition, {}).ToString();
        TestTrue(TEXT("Details identify unique equipment"), Description.Contains(TEXT("暗金")));
        for (const auto& Expected : Item.Modifiers)
        {
            const auto* Modifier = Effect->Modifiers.FindByPredicate(
                [&Expected](const FGameplayModifierInfo& Entry)
                {
                    return Entry.Attribute == Expected.Attribute;
                });
            if (!TestNotNull(TEXT("Expected persistent attribute modifier"), Modifier))
            {
                return false;
            }
            const FName RowName(*(Item.Name.ToString() + TEXT("_") + Expected.Attribute.GetName()));
            const auto* Curve = Curves->FindCurve(RowName, TEXT("UniqueEquipmentAssets"), false);
            if (!TestNotNull(TEXT("Dedicated editable attribute curve"), Curve))
            {
                return false;
            }
            float Value = 0.f;
            FScalableFloat ExpectedCurve(1.f);
            ExpectedCurve.Curve.CurveTable = Curves;
            ExpectedCurve.Curve.RowName = RowName;
            TestTrue(TEXT("Modifier binds the exact independent authored curve row"),
                     Modifier->ModifierMagnitude == FGameplayEffectModifierMagnitude(ExpectedCurve));
            TestTrue(TEXT("Actual GE reads its authored curve magnitude"),
                     Modifier->ModifierMagnitude.GetStaticMagnitudeIfPossible(Definition->EquipEffectLevel, Value));
            TestTrue(TEXT("Curve contains the explicit initial balance value"),
                     FMath::IsNearlyEqual(Curve->Eval(Definition->EquipEffectLevel), Expected.Multiplier));
            TestTrue(TEXT("Modifier matches that curve value"), FMath::IsNearlyEqual(Value, Expected.Multiplier));
            if (Expected.Multiplier < 1.f)
            {
                const FString Penalty = FString::Printf(TEXT("%+g%%"), (Expected.Multiplier - 1.f) * 100.f);
                TestTrue(TEXT("Details disclose each percentage penalty"), Description.Contains(Penalty));
            }
            TestTrue(TEXT("Final channel applies a true compound multiplier"),
                     Modifier->ModifierOp == EGameplayModOp::MultiplyCompound &&
                         Modifier->EvaluationChannelSettings.GetEvaluationChannel() ==
                             EGameplayModEvaluationChannel::Channel2);
        }

        F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), MaxHealth * HealthRatio);
        F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), MaxMana * ManaRatio);
        TArray<float> Before;
        for (const auto& Modifier : Item.Modifiers)
        {
            Before.Add(F.ASC->GetNumericAttribute(Modifier.Attribute));
        }
        if (!TestTrue(TEXT("Add authored item and equip with actual identity/revisions"),
                      F.AddAndEquip(Item.Name, Item.Slot)))
        {
            return false;
        }
        for (int32 Index = 0; Index < Item.Modifiers.Num(); ++Index)
        {
            TestTrue(TEXT("Real initialization plus authored unique yields actual multiplied attribute"),
                     FMath::IsNearlyEqual(F.ASC->GetNumericAttribute(Item.Modifiers[Index].Attribute),
                                          Before[Index] * Item.Modifiers[Index].Multiplier, .01f));
        }
        TestTrue(TEXT("Authored unique preserves the fraction of health"),
                 FMath::IsNearlyEqual(
                     F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()),
                     HealthRatio * F.ASC->GetNumericAttribute(UMyAttributeSet::GetMaxHealthAttribute()), .01f));
        TestTrue(TEXT("Authored unique preserves the fraction of mana"),
                 FMath::IsNearlyEqual(F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()),
                                      ManaRatio * F.ASC->GetNumericAttribute(UMyAttributeSet::GetMaxManaAttribute()),
                                      .01f));
        const auto Equipped = F.State->ExportBackpackSnapshot();
        TestTrue(TEXT("Authored item restores through combined persistence"),
                 F.State->ImportBackpackSnapshot(Equipped));
        TestTrue(TEXT("Repeated restore stays idempotent"), F.State->ImportBackpackSnapshot(Equipped));
        TestEqual(TEXT("Restore owns exactly one unique GE"), F.ASC->GetActiveEffects(FGameplayEffectQuery()).Num(),
                  BaseEffects + 1);
        for (int32 Index = 0; Index < Item.Modifiers.Num(); ++Index)
        {
            TestTrue(TEXT("Restored multiplier applies once"),
                     FMath::IsNearlyEqual(F.ASC->GetNumericAttribute(Item.Modifiers[Index].Attribute),
                                          Before[Index] * Item.Modifiers[Index].Multiplier, .01f));
        }
        TestTrue(TEXT("Unequip actual asset by its current instance"), F.Unequip(Item.Slot));
        TestEqual(TEXT("Unequip restores original partial health"),
                  F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), MaxHealth * HealthRatio, .01f);
        TestEqual(TEXT("Unequip restores original partial mana"),
                  F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), MaxMana * ManaRatio, .01f);
        const auto Restored = F.Values();
        for (int32 Index = 0; Index < Baseline.Num(); ++Index)
        {
            TestTrue(TEXT("All persistent and derived attributes return to baseline"),
                     FMath::IsNearlyEqual(Restored[Index], Baseline[Index], .01f));
        }
        TestTrue(TEXT("Reset fixture inventory without changing authored assets"),
                 F.State->ImportBackpackSnapshot(Empty));
    }

    if (!TestTrue(TEXT("Equip first real bloodglass ring"), F.AddAndEquip(TEXT("UniqueBloodglassRing"), Left)) ||
        !TestTrue(TEXT("Equip second real bloodglass ring"), F.AddAndEquip(TEXT("UniqueBloodglassRing"), Right)))
    {
        return false;
    }
    TestTrue(TEXT("Duplicate ring bonuses compound independently"),
             FMath::IsNearlyEqual(F.ASC->GetNumericAttribute(UMyAttributeSet::GetArmorPeneAttribute()),
                                  ArmorPene * 2.2f * 2.2f, .01f));
    TestTrue(TEXT("Duplicate ring penalties also compound"),
             FMath::IsNearlyEqual(F.ASC->GetNumericAttribute(UMyAttributeSet::GetMaxHealthAttribute()),
                                  MaxHealth * .7f * .7f, .01f));
    TestTrue(TEXT("Remove only left ring"), F.Unequip(Left));
    TestTrue(TEXT("Right ring actual effect survives"),
             FMath::IsNearlyEqual(F.ASC->GetNumericAttribute(UMyAttributeSet::GetArmorPeneAttribute()),
                                  ArmorPene * 2.2f, .01f));
    TestTrue(TEXT("Remove right ring"), F.Unequip(Right));
    TestTrue(TEXT("Restore empty before set test"), F.State->ImportBackpackSnapshot(Empty));

    const auto* Prism = Effects.FindChecked(TEXT("UniqueAshenPrism"));
    const auto* Crown = Effects.FindChecked(TEXT("UniqueAshenCrown"));
    TestTrue(TEXT("Crown asset grants its own set identity"), Crown->GetGrantedTags().HasTagExact(CrownTag));
    TestTrue(TEXT("Prism asset grants its own set identity"), Prism->GetGrantedTags().HasTagExact(PrismTag));
    TestTrue(TEXT("Set condition requires both distinct identities"),
             Prism->ProjectileRule.RequiredSetTags.Num() == 2 &&
                 Prism->ProjectileRule.RequiredSetTags.HasTagExact(CrownTag) &&
                 Prism->ProjectileRule.RequiredSetTags.HasTagExact(PrismTag));
    TestTrue(TEXT("Projectile rule uses actual FireBolt identity"),
             Prism->ProjectileRule.AbilityTag == FMyGameplayTags::Get().Abilities_Fire_FireBolt);
    TestTrue(TEXT("Projectile values are independently table driven"),
             Prism->ProjectileRule.ExtraProjectiles.Curve.CurveTable == Curves &&
                 Prism->ProjectileRule.DamageMultiplier.Curve.CurveTable == Curves &&
                 Prism->ProjectileRule.MinimumSpread.Curve.CurveTable == Curves &&
                 Prism->ProjectileRule.SetDamageMultiplier.Curve.CurveTable == Curves);

    FAbilitySourceToken Source;
    FString Error;
    if (!TestTrue(TEXT("Real PlayerState ASC yields an authoritative source token"),
                  AbilityExecutionIdentity::CaptureSource(F.Pawn, Source, Error)))
    {
        AddError(Error);
        return false;
    }
    FAbilityActorProjectileSettings Settings;
    Settings.ProjectileClass = LoadClass<AMyProjectile>(
        nullptr, TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Fire/FireBolt/BP_FireBolt.BP_FireBolt_C"));
    if (!TestNotNull(TEXT("Existing real FireBolt projectile Blueprint"), Settings.ProjectileClass.Get()))
    {
        return false;
    }
    Settings.Count = 5;
    Settings.bLimitCountToLevel = true;
    Settings.bConfigureHoming = true;
    Settings.EquipmentAbilityTag = FMyGameplayTags::Get().Abilities_Fire_FireBolt;
    FDamageEffectParams Damage;
    Damage.BaseDamage = 100.f;
    Damage.DebuffDamage = 20.f;
    Damage.DamageType = FMyGameplayTags::Get().Damage_Fire;
    const auto Spawn = [&Source, &Settings, &Damage](float Level)
    {
        return AbilityTraditionalActions::SpawnProjectiles(Source, Damage, Settings, Level, FVector(0.f, 0.f, 100.f),
                                                           FVector(1000.f, 0.f, 100.f));
    };
    const auto CheckShots = [this](const TArray<AMyProjectile*>& Shots, int32 Count, float Amount)
    {
        TestEqual(TEXT("Authored equipment changes actual completed projectile count"), Shots.Num(), Count);
        for (const auto* Shot : Shots)
        {
            TestTrue(TEXT("Actual projectile carries authored damage fraction"),
                     FMath::IsNearlyEqual(Shot->DamageEffectParams.BaseDamage, Amount, .01f));
            TestTrue(TEXT("Actual projectile carries corresponding burning fraction"),
                     FMath::IsNearlyEqual(Shot->DamageEffectParams.DebuffDamage, Amount * .2f, .01f));
            TestFalse(TEXT("Prism actual projectile no longer homes"), Shot->ProjectileMovement->bIsHomingProjectile);
        }
    };
    TestTrue(TEXT("Equip authored prism"), F.AddAndEquip(TEXT("UniqueAshenPrism"), Stone));
    TestTrue(TEXT("Prism granted tag reaches live ASC"), F.ASC->HasMatchingGameplayTag(PrismTag));
    TestFalse(TEXT("Single prism does not grant crown identity"), F.ASC->HasMatchingGameplayTag(CrownTag));
    const auto FirstShots = Spawn(1.f);
    CheckShots(FirstShots, 3, 55.f);
    TestTrue(TEXT("Equip authored crown"), F.AddAndEquip(TEXT("UniqueAshenCrown"), Helmet));
    TestTrue(TEXT("Two set identities both active"), F.ASC->HasMatchingGameplayTag(CrownTag));
    CheckShots(Spawn(3.f), 5, 70.f);
    const auto SetSnapshot = F.State->ExportBackpackSnapshot();
    TestTrue(TEXT("Restore equipped actual two-piece set"), F.State->ImportBackpackSnapshot(SetSnapshot));
    TestTrue(TEXT("Restore two-piece set twice"), F.State->ImportBackpackSnapshot(SetSnapshot));
    CheckShots(Spawn(1.f), 3, 70.f);
    TestTrue(TEXT("Removing crown breaks actual set"), F.Unequip(Helmet));
    TestFalse(TEXT("Crown tag removed with its instance"), F.ASC->HasMatchingGameplayTag(CrownTag));
    CheckShots(Spawn(1.f), 3, 55.f);
    TestTrue(TEXT("Removing prism restores original skill"), F.Unequip(Stone));
    TestFalse(TEXT("Prism tag removed with its instance"), F.ASC->HasMatchingGameplayTag(PrismTag));
    const auto Original = Spawn(1.f);
    TestEqual(TEXT("Original projectile count restored"), Original.Num(), 1);
    if (!Original.IsEmpty())
    {
        TestEqual(TEXT("Original damage restored"), Original[0]->DamageEffectParams.BaseDamage, 100.f);
        TestTrue(TEXT("Original homing restored"), Original[0]->ProjectileMovement->bIsHomingProjectile);
    }
    for (const auto* Shot : FirstShots)
    {
        TestTrue(TEXT("Equipment removal does not rewrite existing projectile snapshot"),
                 FMath::IsNearlyEqual(Shot->DamageEffectParams.BaseDamage, 55.f, .01f));
    }
    TestTrue(TEXT("Equip the authored needleflame prism"), F.AddAndEquip(TEXT("UniqueNeedleflamePrism"), Stone));
    const auto NeedleShots = Spawn(1.f);
    TestEqual(TEXT("Needleflame keeps FireBolt's original projectile count"), NeedleShots.Num(), 1);
    if (!NeedleShots.IsEmpty())
    {
        const auto* Shot = NeedleShots[0];
        TestTrue(TEXT("Needleflame preserves the original homing"), Shot->ProjectileMovement->bIsHomingProjectile);
        TestEqual(TEXT("Needleflame scales actual direct damage"), Shot->DamageEffectParams.BaseDamage, 165.f, .01f);
        TestEqual(TEXT("Needleflame scales actual burning damage"), Shot->DamageEffectParams.DebuffDamage, 33.f, .01f);
    }
    TestTrue(TEXT("Remove Needleflame by equipped instance"), F.Unequip(Stone));

    const auto& Melee = Effects.FindChecked(TEXT("UniqueCinderOathBoots"))->MeleeRule;
    TestTrue(TEXT("Melee unique only changes sword shield Attack1"),
             Melee.AbilityTag ==
                 FGameplayTag::RequestGameplayTag(TEXT("Abilities.StanderWeapons.Sword_Shield.Attack1")));
    TestTrue(TEXT("Melee unique uses existing fire damage mapping"),
             Melee.DamageTypeOverride == FMyGameplayTags::Get().Damage_Fire);
    TestTrue(TEXT("Melee multiplier uses real curve table"), Melee.DamageMultiplier.Curve.CurveTable == Curves);
    TestTrue(TEXT("Melee base damage is initially one point eight times"),
             FMath::IsNearlyEqual(Melee.DamageMultiplier.GetValueAtLevel(1.f), 1.8f));
    TestTrue(TEXT("Melee uses the inspected real fire impact Niagara asset"),
             Melee.HitEffect && Melee.HitEffect->GetPathName() ==
                                    TEXT("/Game/CompositionBatch6/NS_SolarExplosionFlame.NS_SolarExplosionFlame"));
    const auto& Frost = Effects.FindChecked(TEXT("UniqueFrostWakeHelm"))->MeleeRule;
    const auto& Solar = Effects.FindChecked(TEXT("UniqueSolarStepBoots"))->MeleeRule;
    TestTrue(TEXT("Authored melee variants use separate attached systems and duration curves"),
             Frost.BodyEffect && Solar.BodyEffect && Frost.BodyEffect != Solar.BodyEffect &&
                 Frost.BodyEffectDuration.Curve.CurveTable == Curves && Solar.BodyEffectDuration.Curve.CurveTable == Curves);
    TestTrue(TEXT("Equip real Arcane-area gear"), F.AddAndEquip(TEXT("UniqueArcaneHorizonRobe"), Chest));
    float ShardRadius = 250.f;
    FDamageEffectParams ShardDamage;
    ShardDamage.BaseDamage = 100.f;
    int32 Extra = 0;
    TestTrue(TEXT("Real robe matches another member, Arcane Shards"),
             EquipmentAbilityModifiers::ApplySpellRules(Source, FMyGameplayTags::Get().Abilities_Arcane_ArcaneShards,
                                                         ShardRadius, ShardDamage, Extra));
    TestEqual(TEXT("Shard radius responds to the family rule"), ShardRadius, 350.f);
    TestEqual(TEXT("Shard damage uses the same family penalty"), ShardDamage.BaseDamage, 80.f);
    TestTrue(TEXT("Unrelated fire family remains untouched"),
             EquipmentAbilityModifiers::ApplySpellRules(Source, FMyGameplayTags::Get().Abilities_Fire_FireBolt,
                                                         ShardRadius, ShardDamage, Extra));
    TestEqual(TEXT("Fire tag does not cause another radius change"), ShardRadius, 350.f);
    TestEqual(TEXT("Fire tag does not cause another damage change"), ShardDamage.BaseDamage, 80.f);
    TestTrue(TEXT("Remove family item before fixture cleanup"), F.Unequip(Chest));

    // 逐次改变真实装备事务，验证齐套只替换导管倍率，分雷戒仍用独立的雷链预算。
    const auto CheckChain = [this, &Source](float ExpectedDamage, float ExpectedRadius, int32 ExpectedExtra)
    {
        FDamageEffectParams ChainDamage;
        ChainDamage.BaseDamage = 100.f;
        float ChainRadius = 300.f;
        int32 ChainExtra = 0;
        TestTrue(TEXT("Live equipment resolves through the chain action's parameter resolver"),
                 EquipmentAbilityModifiers::ApplySpellRules(Source, FMyGameplayTags::Get().Abilities_Thunder_Electrocute,
                                                             ChainRadius, ChainDamage, ChainExtra));
        TestEqual(TEXT("Expected chain damage after set or equipment change"), ChainDamage.BaseDamage, ExpectedDamage,
                  .01f);
        TestEqual(TEXT("Expected secondary radius after set or equipment change"), ChainRadius, ExpectedRadius, .01f);
        TestEqual(TEXT("Expected secondary count after set or equipment change"), ChainExtra, ExpectedExtra);
    };
    TestTrue(TEXT("Equip real Storm Lens"), F.AddAndEquip(TEXT("UniqueStormLens"), Stone));
    CheckChain(128.f, 246.f, 0);
    TestTrue(TEXT("Equip distinct Stormweaver Mantle"), F.AddAndEquip(TEXT("UniqueStormweaverMantle"), Chest));
    CheckChain(165.f, 246.f, 0);
    const auto ConduitSnapshot = F.State->ExportBackpackSnapshot();
    TestTrue(TEXT("Restore real Storm Conduit loadout"), F.State->ImportBackpackSnapshot(ConduitSnapshot));
    TestTrue(TEXT("Restore real Storm Conduit loadout again"), F.State->ImportBackpackSnapshot(ConduitSnapshot));
    CheckChain(165.f, 246.f, 0);
    TestTrue(TEXT("A Forkstorm Ring can coexist with Storm Conduit"), F.AddAndEquip(TEXT("UniqueForkstormRing"), Left));
    CheckChain(118.8f, 300.12f, 2);
    TestTrue(TEXT("Remove only the ring"), F.Unequip(Left));
    TestTrue(TEXT("Break Storm Conduit via its lens"), F.Unequip(Stone));
    CheckChain(100.f, 300.f, 0);
    TestTrue(TEXT("Restore Storm Lens"), F.AddAndEquip(TEXT("UniqueStormLens"), Stone));
    CheckChain(165.f, 246.f, 0);
    TestTrue(TEXT("Break Storm Conduit via its mantle"), F.Unequip(Chest));
    CheckChain(128.f, 246.f, 0);
    TestTrue(TEXT("Remove the remaining lens"), F.Unequip(Stone));

    // 从生产 GA 的动作配置读身份，检查同穿时两个动作各取自己的伤害与特效。
    TestTrue(TEXT("Equip the real Hammer boots"), F.AddAndEquip(TEXT("UniqueFaultlineTreads"), Boots));
    TestTrue(TEXT("Equip the real DualBlades helm"), F.AddAndEquip(TEXT("UniqueRimefangHelm"), Helmet));
    const auto CheckWeapon = [this, &F](const TCHAR* AbilityPath, const TCHAR* SystemName, float DamageFactor,
                                        float Duration, FGameplayTag DamageType)
    {
        auto* Class = LoadClass<UStanderWeaponActionAbility>(nullptr, AbilityPath);
        const auto* Ability = Class ? GetDefault<UStanderWeaponActionAbility>(Class) : nullptr;
        if (!TestNotNull(TEXT("Real weapon action GA"), Ability) ||
            !TestNotNull(TEXT("Real weapon action config"), Ability->ActionConfig.Get()))
        {
            return;
        }
        const auto* Contact = Ability->ActionConfig->Events.FindByPredicate(
            [](const FStanderWeaponActionEvent& Event)
            {
                return Event.Kind == EStanderWeaponActionKind::Melee && Event.Damage > 0.f;
            });
        if (!TestNotNull(TEXT("Configured GA has a real damage contact"), Contact))
        {
            return;
        }
        const FGameplayTag Action = Ability->ActionConfig->ActionTag;
        FEquipmentMeleeSnapshot Snapshot;
        Snapshot.Damage = Contact->Damage;
        Snapshot.DamageType = FMyGameplayTags::Get().Damage_Physics;
        TestTrue(TEXT("Compatible equipped melee rules resolve"),
                 EquipmentMeleeModifiers::Resolve(F.ASC, Action, Snapshot));
        TestEqual(TEXT("Contact damage uses its equipped multiplier"), Snapshot.Damage, Contact->Damage * DamageFactor,
                  .01f);
        TestEqual(TEXT("Contact damage uses its equipped type"), Snapshot.DamageType, DamageType);
        TestEqual(TEXT("Attached effect duration uses its equipped curve"), Snapshot.BodyEffectDuration, Duration, .01f);
        TestTrue(TEXT("Contact inherits only its authored attached system"),
                 Snapshot.BodyEffect && Snapshot.BodyEffect->GetName() == SystemName);
    };
    CheckWeapon(TEXT("/Game/BluePrints/Characters/Stander/Weapons/Hammer/GA_Skill7_Around.GA_Skill7_Around_C"),
                TEXT("NS_EarthShockwave"), 1.75f, .55f, FMyGameplayTags::Get().Damage_Physics);
    CheckWeapon(TEXT("/Game/BluePrints/Characters/Stander/Weapons/DualBlades/GA_Skill6_Move.GA_Skill6_Move_C"),
                TEXT("NS_GlacialStorm"), 1.45f, .75f, FMyGameplayTags::Get().Damage_Water);
    TestTrue(TEXT("Remove the Hammer boots"), F.Unequip(Boots));
    TestTrue(TEXT("Remove the DualBlades helm"), F.Unequip(Helmet));
    TestNotNull(TEXT("Health potion remains available in original catalog"),
                Catalog->FindDefinition(FPrimaryAssetId(TEXT("BackpackItem"), TEXT("HealthPotion"))));
    TestNotNull(TEXT("Mana potion remains available in original catalog"),
                Catalog->FindDefinition(FPrimaryAssetId(TEXT("BackpackItem"), TEXT("ManaPotion"))));
    TestEqual(TEXT("No equipped unique effects remain after all authored checks"),
              F.ASC->GetActiveEffects(FGameplayEffectQuery()).Num(), BaseEffects);
    return true;
}
#endif
