// 调用链：Automation test_25d.Equipment.Unique → 真实 PlayerState/装备事务/GAS → 校验倍率、展示与回收。
#include "Misc/AutomationTest.h"
#include "Equipment/UniqueEquipmentGameplayEffect.h"
#include "Equipment/EquipmentGenerator.h"
#include "Equipment/EquipmentComponent.h"
#include "Backpack/BackpackComponent.h"
#include "Backpack/ItemDefinition.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Player/MyPlayerState.h"
#include "Tests/EquipmentTestEffects.h"
#include "Tests/UniqueEquipmentTestEffects.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "NativeGameplayTags.h"
#include "GameplayEffectComponents/AssetTagsGameplayEffectComponent.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "MyGameplayTags.h"
#include <limits>

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_UniqueTestAbility, "Equipment.Test.Unique.Projectile");

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUniqueEquipmentValidationTest, "test_25d.Equipment.Unique.Validation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 校验权限边界：普通/随机装备不因暗金支持而获得乘法权限，非法倍率不能进入事务。
bool FUniqueEquipmentValidationTest::RunTest(const FString& Parameters)
{
    auto* Unique = NewObject<UUniqueEquipmentTestOffense>();
    UEquipmentGameplayEffect* ThroughBase = Unique;
    TestTrue(TEXT("Dedicated fixed unique accepts compound multipliers through base API"),
             ThroughBase->IsSafeEquipmentEffect(1.f));
    TestFalse(TEXT("Unique cannot become random template"), Unique->IsSafeEquipmentEffect(1.f, true));

    auto* Ordinary = NewObject<UEquipmentGameplayEffect>();
    Ordinary->Modifiers = Unique->Modifiers;
    TestFalse(TEXT("Ordinary equipment still rejects compound multipliers"), Ordinary->IsSafeEquipmentEffect(1.f));
    auto* Random = NewObject<URandomEquipmentGameplayEffect>();
    Random->Modifiers[0] = Unique->Modifiers[0];
    TestFalse(TEXT("Random equipment still rejects compound multipliers"), Random->IsSafeEquipmentEffect(1.f, true));

    for (float Invalid :
         {0.f, -1.f, 0.01f, 10.1f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
    {
        Unique->Modifiers[0].ModifierMagnitude = FScalableFloat(Invalid);
        TestFalse(TEXT("Unsafe factor rejected"), Unique->IsSafeEquipmentEffect(1.f));
    }
    Unique->Modifiers[0].ModifierMagnitude = FScalableFloat(2.f);
    for (const FGameplayAttribute& Attribute :
         {UMyAttributeSet::GetHealthAttribute(), UMyAttributeSet::GetManaAttribute(),
          UMyAttributeSet::GetIncomingDamageAttribute(), UMyAttributeSet::GetIncomingXPAttribute()})
    {
        Unique->Modifiers[0].Attribute = Attribute;
        TestFalse(TEXT("Current resources and meta attributes remain forbidden"), Unique->IsSafeEquipmentEffect(1.f));
    }
    Unique->Modifiers[0].Attribute = UMyAttributeSet::GetArmorAttribute();
    Unique->Modifiers[0].EvaluationChannelSettings.SetEvaluationChannel(EGameplayModEvaluationChannel::Channel1);
    TestFalse(TEXT("Multiplier cannot run before final channel"), Unique->IsSafeEquipmentEffect(1.f));
    Unique->Modifiers[0].EvaluationChannelSettings.SetEvaluationChannel(EGameplayModEvaluationChannel::Channel2);
    Unique->Modifiers[0].ModifierOp = EGameplayModOp::MultiplyAdditive;
    TestFalse(TEXT("Additive multiplication cannot pretend to be compound"), Unique->IsSafeEquipmentEffect(1.f));
    Unique->Modifiers[0].ModifierOp = EGameplayModOp::MultiplyCompound;
    FSetByCallerFloat Dynamic;
    Dynamic.DataTag = TAG_UniqueTestAbility;
    Unique->Modifiers[0].ModifierMagnitude = FGameplayEffectModifierMagnitude(Dynamic);
    TestFalse(TEXT("Unique multipliers must be static or curve based, never SetByCaller"),
              Unique->IsSafeEquipmentEffect(1.f));
    Unique->Modifiers[0].ModifierMagnitude = FScalableFloat(2.f);
    Unique->Period = FScalableFloat(1.f);
    TestFalse(TEXT("Periodic unique effects remain forbidden"), Unique->IsSafeEquipmentEffect(1.f));
    Unique->Period = FScalableFloat(0.f);
    Unique->DurationPolicy = EGameplayEffectDurationType::Instant;
    TestFalse(TEXT("Instant unique effects remain forbidden"), Unique->IsSafeEquipmentEffect(1.f));
    Unique->DurationPolicy = EGameplayEffectDurationType::Infinite;
    Unique->StackingType = EGameplayEffectStackingType::AggregateByTarget;
    TestFalse(TEXT("Stacked unique effects remain forbidden"), Unique->IsSafeEquipmentEffect(1.f));
    Unique->StackingType = EGameplayEffectStackingType::None;
    Unique->Executions.AddDefaulted();
    TestFalse(TEXT("Execution calculations remain forbidden"), Unique->IsSafeEquipmentEffect(1.f));
    Unique->Executions.Reset();
    Unique->FindOrAddComponent<UTargetTagsGameplayEffectComponent>();
    TestTrue(TEXT("Granted equipment and set tags remain allowed"), Unique->IsSafeEquipmentEffect(1.f));
    Unique->FindOrAddComponent<UAssetTagsGameplayEffectComponent>();
    TestFalse(TEXT("Non TargetTags GE components remain forbidden"), Unique->IsSafeEquipmentEffect(1.f));

    FEquipmentProjectileRule Rule;
    TestTrue(TEXT("Default empty rule is inert and valid"), Rule.IsValid(1.f));
    Rule.AbilityTag = TAG_UniqueTestAbility;
    Rule.ExtraProjectiles = FScalableFloat(2.f);
    Rule.DamageMultiplier = FScalableFloat(0.55f);
    Rule.MinimumSpread = FScalableFloat(40.f);
    TestTrue(TEXT("Bounded projectile rule valid"), Rule.IsValid(1.f));
    Rule.ExtraProjectiles = FScalableFloat(17.f);
    TestFalse(TEXT("Extra projectile count bounded"), Rule.IsValid(1.f));
    Rule.ExtraProjectiles = FScalableFloat(2.f);
    Rule.DamageMultiplier = FScalableFloat(0.f);
    TestFalse(TEXT("Damage factor cannot be zero"), Rule.IsValid(1.f));
    Rule.DamageMultiplier = FScalableFloat(std::numeric_limits<float>::quiet_NaN());
    TestFalse(TEXT("Damage factor cannot be NaN"), Rule.IsValid(1.f));
    Rule.DamageMultiplier = FScalableFloat(1.f);
    Rule.MinimumSpread = FScalableFloat(361.f);
    TestFalse(TEXT("Scatter angle bounded"), Rule.IsValid(1.f));
    Rule.MinimumSpread = FScalableFloat(40.f);
    Rule.AbilityTag = FGameplayTag();
    TestFalse(TEXT("Non-default rule requires ability identity"), Rule.IsValid(1.f));

    FEquipmentMeleeRule Melee;
    TestTrue(TEXT("Default melee rule is inert"), Melee.IsValid(1.f));
    Melee.AbilityTag = TAG_UniqueTestAbility;
    Melee.DamageTypeOverride = FMyGameplayTags::Get().Damage_Fire;
    Melee.DamageMultiplier = FScalableFloat(1.8f);
    TestTrue(TEXT("Existing elemental damage type allowed"), Melee.IsValid(1.f));
    Melee.DamageTypeOverride = TAG_UniqueTestAbility;
    TestFalse(TEXT("Unknown damage type rejected"), Melee.IsValid(1.f));
    Melee.DamageTypeOverride = FMyGameplayTags::Get().Damage_Fire;
    Melee.DamageMultiplier = FScalableFloat(std::numeric_limits<float>::infinity());
    TestFalse(TEXT("Infinite melee multiplier rejected"), Melee.IsValid(1.f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUniqueEquipmentDescriptionTest, "test_25d.Equipment.Unique.Description",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 属性详情按倍率解释百分比，避免把 2.0 错画为 +2 护甲。
bool FUniqueEquipmentDescriptionTest::RunTest(const FString& Parameters)
{
    FBackpackItemDefEntry Definition;
    Definition.bEquippable = true;
    Definition.EquipEffect = UUniqueEquipmentTestOffense::StaticClass();
    const FString Description = FEquipmentGenerator::MakeDescription(&Definition, {}).ToString();
    TestTrue(TEXT("Unique identity shown"), Description.Contains(TEXT("暗金")));
    TestTrue(TEXT("Compound increase shown as percent"), Description.Contains(TEXT("+100%")));
    TestTrue(TEXT("Compound penalty shown as percent"), Description.Contains(TEXT("-30%")));
    TestTrue(TEXT("Custom unique rule text shown"), Description.Contains(TEXT("测试暗金机制")));
    return true;
}

namespace UniqueEquipmentTests
{
    struct FFixture
    {
        UWorld* World;
        AMyPlayerState* State;
        APawn* Pawn;
        UAbilitySystemComponent* ASC;
        UBackpackComponent* Bag;
        UEquipmentComponent* Equipment;
        UBackpackItemDefinitions* Catalog;
        const FGameplayTag Left = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot.Ring.Left"));
        const FGameplayTag Right = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot.Ring.Right"));
        const FPrimaryAssetId First{TEXT("BackpackItem"), TEXT("UniqueTestFirst")};
        const FPrimaryAssetId Second{TEXT("BackpackItem"), TEXT("UniqueTestSecond")};

        // 复用真实玩家 ASC 和生产目录；测试效果通过正常物品/装备接口应用。
        FFixture()
        {
            World = UWorld::CreateWorld(EWorldType::Game, false,
                                        MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            State = World->SpawnActor<AMyPlayerState>();
            Pawn = World->SpawnActor<APawn>();
            Pawn->SetPlayerState(State);
            ASC = State->GetAbilitySystemComponent();
            ASC->InitAbilityActorInfo(State, Pawn);
            ASC->AddAttributeSetSubobject(CastChecked<UMyAttributeSet>(State->GetAttributeSet()));
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetResilienceAttribute(), 40.f);
            Bag = State->GetBackpackComponent();
            Equipment = State->GetEquipmentComponent();
            Catalog = NewObject<UBackpackItemDefinitions>(State);
            AddDefinition(First, UUniqueEquipmentTestOffense::StaticClass());
            AddDefinition(Second, UUniqueEquipmentTestDefense::StaticClass());
            Bag->SetItemDefinitions(Catalog);
            Bag->Initialize();
            Bag->SetCapacity(4);
            Equipment->ReconcileEffects();
        }

        // 按两个戒指槽配置不可堆叠的测试装备，保留生产 GUID 创建规则。
        void AddDefinition(FPrimaryAssetId Id, TSubclassOf<UEquipmentGameplayEffect> Effect)
        {
            auto& Definition = Catalog->Items.AddDefaulted_GetRef();
            Definition.ItemDefinitionId = Id;
            Definition.bEquippable = true;
            Definition.bStackable = false;
            Definition.MaxStack = 1;
            Definition.AllowedEquipmentSlots.AddTag(Left);
            Definition.AllowedEquipmentSlots.AddTag(Right);
            Definition.EquipEffect = Effect;
        }

        // 携带当前背包与装备版本调用生产服务器事务。
        bool Equip(int32 Index, FGameplayTag Slot)
        {
            return Equipment->TryEquip(Index, Slot, Bag->GetInventoryModel()->GetSlots()[Index].ItemInstanceId,
                                       Bag->GetRevision(), Equipment->Revision);
        }

        // 精确按槽中的实例卸装，验证另一件仍保留其 GE。
        bool Unequip(FGameplayTag Tag)
        {
            const auto* Slot = Equipment->Slots.FindByPredicate(
                [Tag](const FEquipmentSlot& Entry)
                {
                    return Entry.SlotTag == Tag;
                });
            return Slot && Equipment->TryUnequip(Tag, Slot->ItemInstanceId, Bag->GetRevision(), Equipment->Revision);
        }

        // 销毁测试世界，清理装备组件及其拥有的效果。
        ~FFixture()
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
    };
} // namespace UniqueEquipmentTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUniqueEquipmentLifecycleTest, "test_25d.Equipment.Unique.Lifecycle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 真实基础 GE 与独立加法效果共同参与计算，证明穿脱影响真实属性而不只生成 Active Handle。
bool FUniqueEquipmentLifecycleTest::RunTest(const FString& Parameters)
{
    UniqueEquipmentTests::FFixture Fixture;
    UClass* Secondary = LoadClass<UGameplayEffect>(
        nullptr, TEXT("/Game/BluePrints/AbilitySystem/Player/Effect/GE_SecondaryAttributes.GE_SecondaryAttributes_C"));
    if (!TestNotNull(TEXT("Real secondary attribute GE exists"), Secondary))
    {
        return false;
    }
    auto Context = Fixture.ASC->MakeEffectContext();
    Context.AddSourceObject(Fixture.Pawn);
    const auto Spec = Fixture.ASC->MakeOutgoingSpec(Secondary, 1.f, Context);
    if (!TestTrue(TEXT("Real base override applied"),
                  Spec.IsValid() && Fixture.ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data).IsValid()))
    {
        return false;
    }
    const auto BuffSpec = Fixture.ASC->MakeOutgoingSpec(UEquipmentTestArmor::StaticClass(), 1.f, Context);
    const auto Buff = Fixture.ASC->ApplyGameplayEffectSpecToSelf(*BuffSpec.Data);
    const float BaseArmor = Fixture.ASC->GetNumericAttribute(UMyAttributeSet::GetArmorAttribute());
    const float BaseHealth = Fixture.ASC->GetNumericAttribute(UMyAttributeSet::GetMaxHealthAttribute());
    const float BaseMana = Fixture.ASC->GetNumericAttribute(UMyAttributeSet::GetMaxManaAttribute());
    const float InitialHealth = BaseHealth * .6f;
    const float InitialMana = BaseMana * .25f;
    Fixture.ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), InitialHealth);
    Fixture.ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), InitialMana);
    Fixture.Bag->TryAddItem(Fixture.First, 1);
    Fixture.Bag->TryAddItem(Fixture.Second, 1);
    if (!TestTrue(TEXT("Equip compound unique through existing transaction"), Fixture.Equip(0, Fixture.Left)))
    {
        return false;
    }
    TestTrue(
        TEXT("First multiplier applied after additive bonus"),
        FMath::IsNearlyEqual(Fixture.ASC->GetNumericAttribute(UMyAttributeSet::GetArmorAttribute()), BaseArmor * 2.f));
    TestTrue(TEXT("Health percentage survives the compound maximum decrease"),
             FMath::IsNearlyEqual(Fixture.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()),
                                  InitialHealth * 0.7f));
    if (!TestTrue(TEXT("Second independent unique equips"), Fixture.Equip(1, Fixture.Right)))
    {
        return false;
    }
    TestTrue(
        TEXT("Two compound factors multiply, not add"),
        FMath::IsNearlyEqual(Fixture.ASC->GetNumericAttribute(UMyAttributeSet::GetArmorAttribute()), BaseArmor * 3.f));
    Fixture.ASC->SetNumericAttributeBase(UMyAttributeSet::GetResilienceAttribute(), 44.f);
    TestTrue(TEXT("Derived base recalculation retains additive and compound equipment"),
             FMath::IsNearlyEqual(Fixture.ASC->GetNumericAttribute(UMyAttributeSet::GetArmorAttribute()),
                                  (BaseArmor + 1.f) * 3.f));
    Fixture.ASC->SetNumericAttributeBase(UMyAttributeSet::GetResilienceAttribute(), 40.f);
    TestTrue(TEXT("Mana percentage survives the compound maximum decrease"),
             FMath::IsNearlyEqual(Fixture.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()),
                                  InitialMana * 0.5f));
    const auto Snapshot = Fixture.State->ExportBackpackSnapshot();
    TestTrue(TEXT("Combined snapshot restores"), Fixture.State->ImportBackpackSnapshot(Snapshot));
    TestTrue(TEXT("Repeated reconcile succeeds"), Fixture.Equipment->ReconcileEffects());
    TestTrue(
        TEXT("Restore does not duplicate factors"),
        FMath::IsNearlyEqual(Fixture.ASC->GetNumericAttribute(UMyAttributeSet::GetArmorAttribute()), BaseArmor * 3.f));
    TestTrue(TEXT("First unique removes independently"), Fixture.Unequip(Fixture.Left));
    TestTrue(
        TEXT("Second factor remains"),
        FMath::IsNearlyEqual(Fixture.ASC->GetNumericAttribute(UMyAttributeSet::GetArmorAttribute()), BaseArmor * 1.5f));
    TestTrue(
        TEXT("Unequip restores the original partial health"),
        FMath::IsNearlyEqual(Fixture.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), InitialHealth));
    TestTrue(TEXT("Second unique removes"), Fixture.Unequip(Fixture.Right));
    TestTrue(TEXT("Armor restored including unrelated buff"),
             FMath::IsNearlyEqual(Fixture.ASC->GetNumericAttribute(UMyAttributeSet::GetArmorAttribute()), BaseArmor));
    TestTrue(TEXT("Unrelated active effect survives"), Fixture.ASC->GetActiveGameplayEffect(Buff) != nullptr);
    TestTrue(TEXT("Unequip restores the original partial mana"),
             FMath::IsNearlyEqual(Fixture.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), InitialMana));
    return true;
}
#endif
