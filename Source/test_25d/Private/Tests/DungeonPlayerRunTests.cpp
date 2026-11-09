/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Roguelike.Player.DownedDoesNotRespawn
 * - test_25d.Roguelike.Player.RevivePreservesLoadout
 * - test_25d.Roguelike.Player.ReviveRestoresOnlyActivePassives
 * - test_25d.Roguelike.Player.ReviveNeverRestartsLegacyPassiveToggle
 * - test_25d.Roguelike.Player.ReviveRespectsChangedPassiveLoadout
 * - test_25d.Roguelike.Player.DownedBlocksDirectAbilityActivation
 * - test_25d.Roguelike.Player.ReviveValidation
 * - test_25d.Roguelike.Player.ReviveDiscardsDamageReceivedWhileDowned
 * - test_25d.Roguelike.Player.ReviveClearsPeriodicDebuffsAndRetainsBuffs
 * - test_25d.Roguelike.Player.ClientCannotDownOrRevive
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "DungeonPlayerRunTestTypes.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystem/Ability/MyGameplayAbility.h"
#include "Backpack/BackpackComponent.h"
#include "Backpack/ItemDefinition.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "MyGameplayTags.h"
#include "PureMass/PureMassIncomingDamageEffect.h"
#include "GameplayEffect.h"
#include "TimerManager.h"
#include "UObject/Script.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace DungeonPlayerRunTests
{
    struct FFixture
    {
        // 创建独立世界、玩家状态、ASC与背包初值，避免读取真实存档。
        explicit FFixture(EWorldType::Type Type = EWorldType::Game)
        {
            World = UWorld::CreateWorld(
                Type, false,
                MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("DungeonPlayerRunTest")));
            GEngine->CreateNewWorldContext(Type).SetCurrentWorld(World);
            State = World->SpawnActor<AMyPlayerState>();
            Pawn = World->SpawnActor<ADungeonPlayerRunTestPawn>();
            Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            Pawn->AttachState(State);
            Pawn->FOnDeath.AddDynamic(Pawn, &ADungeonPlayerRunTestPawn::CountDeath);
            ASC = State->GetAbilitySystemComponent();
            ASC->AddAttributeSetSubobject(CastChecked<UMyAttributeSet>(State->GetAttributeSet()));
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 200.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 0.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 5.f);
            Ability = ASC->GiveAbility(FGameplayAbilitySpec(UMyGameplayAbility::StaticClass(), 3));
            auto* Catalog = NewObject<UBackpackItemDefinitions>(World);
            auto& Entry = Catalog->Items.AddDefaulted_GetRef();
            Entry.ItemDefinitionId = Item;
            Entry.bStackable = true;
            Entry.MaxStack = 10;
            State->GetBackpackComponent()->SetItemDefinitions(Catalog);
            State->GetBackpackComponent()->Initialize();
            State->GetBackpackComponent()->TryAddItem(Item, 3);
        }
        // 先取消技能，再销毁世界上下文和世界，隔离下一条用例。
        ~FFixture()
        {
            ASC->CancelAllAbilities();
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
        // 汇总测试药水数量，验证倒地/复活没有丢失背包。
        int32 ItemCount() const
        {
            int32 Total = 0;
            for (const auto& Slot : State->GetBackpackComponent()->GetInventoryModel()->GetSlots())
                if (Slot.ItemDefinitionId == Item)
                    Total += Slot.Quantity;
            return Total;
        }
        // 通过真实IncomingDamage GE路径施加一次伤害，保留生产结算逻辑。
        bool ApplyInstantDamage(float Amount)
        {
            auto Context = ASC->MakeEffectContext();
            Context.AddSourceObject(Pawn);
            const auto Spec = ASC->MakeOutgoingSpec(UPureMassIncomingDamageEffect::StaticClass(), 1.f, Context);
            if (!Spec.IsValid())
                return false;
            Spec.Data->SetSetByCallerMagnitude(UPureMassIncomingDamageEffect::GetDamageTag(), Amount);
            ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
            return true;
        }
        // 创建延迟首跳的燃烧周期效果，用于验证倒地时的负面效果清理。
        FActiveGameplayEffectHandle ApplyPeriodicBurn()
        {
            auto* Effect = NewObject<UGameplayEffect>(World);
            Effect->DurationPolicy = EGameplayEffectDurationType::HasDuration;
            Effect->DurationMagnitude = FScalableFloat(10.f);
            Effect->Period = .1f;
            Effect->bExecutePeriodicEffectOnApplication = false;
            auto& Modifier = Effect->Modifiers.AddDefaulted_GetRef();
            Modifier.Attribute = UMyAttributeSet::GetIncomingDamageAttribute();
            Modifier.ModifierOp = EGameplayModOp::Additive;
            Modifier.ModifierMagnitude = FScalableFloat(4.f);
            FGameplayEffectSpec Spec(Effect, ASC->MakeEffectContext(), 1.f);
            Spec.DynamicGrantedTags.AddTag(FMyGameplayTags::Get().Debuff_Burn);
            return ASC->ApplyGameplayEffectSpecToSelf(Spec);
        }
        // 创建持续护甲增益，与应清理的伤害效果区分。
        FActiveGameplayEffectHandle ApplyBeneficialArmor()
        {
            auto* Effect = NewObject<UGameplayEffect>(World);
            Effect->DurationPolicy = EGameplayEffectDurationType::HasDuration;
            Effect->DurationMagnitude = FScalableFloat(30.f);
            auto& Modifier = Effect->Modifiers.AddDefaulted_GetRef();
            Modifier.Attribute = UMyAttributeSet::GetArmorAttribute();
            Modifier.ModifierOp = EGameplayModOp::Additive;
            Modifier.ModifierMagnitude = FScalableFloat(7.f);
            FGameplayEffectSpec Spec(Effect, ASC->MakeEffectContext(), 1.f);
            return ASC->ApplyGameplayEffectSpecToSelf(Spec);
        }
        // 按小步推进定时器和帧号，触发周期GE而不依赖真实墙钟等待。
        void AdvanceEffects(float Seconds)
        {
            for (float Time = 0.f; Time < Seconds; Time += .05f)
            {
                ++GFrameCounter;
                World->GetTimerManager().Tick(FMath::Min(.05f, Seconds - Time));
            }
        }
        UWorld* World = nullptr;
        AMyPlayerState* State = nullptr;
        ADungeonPlayerRunTestPawn* Pawn = nullptr;
        UAbilitySystemComponent* ASC = nullptr;
        FGameplayAbilitySpecHandle Ability;
        FEditorScriptExecutionGuard ScriptGuard;
        const FPrimaryAssetId Item{FPrimaryAssetType(TEXT("BackpackItem")), FName(TEXT("HealthPotion"))};
    };
} // namespace DungeonPlayerRunTests
// 测试夹具在各用例中显式限定命名空间，避免 Unity 合并编译污染其它文件的同名 FFixture。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonPlayerDown, "test_25d.Roguelike.Player.DownedDoesNotRespawn",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Player.DownedDoesNotRespawn 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonPlayerDown::RunTest(const FString&)
{
    DungeonPlayerRunTests::FFixture F;
    F.Pawn->Die(FVector(1));
    F.Pawn->Die(FVector(1));
    TestTrue(TEXT("Death enters dungeon downed state"), F.Pawn->IsDungeonDowned());
    TestTrue(TEXT("Combat consumers see dead"), F.Pawn->IsDead_Implementation());
    TestEqual(TEXT("Duplicate death broadcasts once"), F.Pawn->Deaths, 1);
    TestFalse(TEXT("No inherited auto-respawn timer"), F.World->GetTimerManager().TimerExists(F.Pawn->DeathTimer));
    TestEqual(TEXT("Downed movement is disabled"), F.Pawn->GetCharacterMovement()->MovementMode.GetValue(), MOVE_None);
    TestFalse(TEXT("Downed mesh is hidden"), F.Pawn->GetMesh()->IsVisible());
    TestFalse(TEXT("Downed weapon is hidden"), F.Pawn->GetWeapon_Implementation()->IsVisible());
    TestFalse(TEXT("No irreversible ragdoll"), F.Pawn->GetMesh()->IsSimulatingPhysics());
    TestTrue(TEXT("The original ASC is retained"), F.Pawn->GetAbilitySystemComponent() == F.ASC);
    TestNotNull(TEXT("Granted skill survives"), F.ASC->FindAbilitySpecFromHandle(F.Ability));
    TestEqual(TEXT("Backpack survives"), F.ItemCount(), 3);
    TestTrue(TEXT("Downed input cannot activate abilities"),
             F.ASC->HasMatchingGameplayTag(FMyGameplayTags::Get().Player_Block_InputPressed));
    for (int32 Tick = 0; Tick < 140; ++Tick)
    {
        ++GFrameCounter;
        F.World->GetTimerManager().Tick(.05f);
    }
    TestTrue(TEXT("Pawn remains after legacy respawn deadline"), IsValid(F.Pawn) && !F.Pawn->IsActorBeingDestroyed());
    TestTrue(TEXT("Pawn is still down until room clear"), F.Pawn->IsDungeonDowned());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonPlayerRevive, "test_25d.Roguelike.Player.RevivePreservesLoadout",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Player.RevivePreservesLoadout 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonPlayerRevive::RunTest(const FString&)
{
    DungeonPlayerRunTests::FFixture F;
    // Another system owns one tag count. Revival must release only the dungeon's count.
    F.ASC->AddLooseGameplayTag(FMyGameplayTags::Get().Player_Block_InputPressed);
    F.Pawn->Die(FVector::ZeroVector);
    TestEqual(TEXT("Downed adds its own block count"),
              F.ASC->GetTagCount(FMyGameplayTags::Get().Player_Block_InputPressed), 2);
    const FVector Safe(320, 150, 100);
    TestTrue(TEXT("Server revives downed pawn"), F.Pawn->ReviveForDungeon(Safe, .3f));
    TestFalse(TEXT("Downed state cleared"), F.Pawn->IsDungeonDowned());
    TestFalse(TEXT("Combat consumers see living"), F.Pawn->IsDead_Implementation());
    TestTrue(TEXT("Safe position restored"), F.Pawn->GetActorLocation().Equals(Safe));
    TestEqual(TEXT("Revive restores 30 percent maximum health"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 60.f);
    TestEqual(TEXT("Revive restores 30 percent maximum mana"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 30.f);
    TestTrue(TEXT("Revived mesh visible"), F.Pawn->GetMesh()->IsVisible());
    TestEqual(TEXT("Walking resumes"), F.Pawn->GetCharacterMovement()->MovementMode.GetValue(), MOVE_Walking);
    TestTrue(TEXT("Same ASC after revival"), F.Pawn->GetAbilitySystemComponent() == F.ASC);
    auto* Spec = F.ASC->FindAbilitySpecFromHandle(F.Ability);
    if (TestNotNull(TEXT("Same granted skill after revival"), Spec))
        TestEqual(TEXT("Skill rank preserved"), Spec->Level, 3);
    TestEqual(TEXT("Inventory preserved"), F.ItemCount(), 3);
    TestEqual(TEXT("Other system's input block survives revival"),
              F.ASC->GetTagCount(FMyGameplayTags::Get().Player_Block_InputPressed), 1);
    TestFalse(TEXT("Alive pawn cannot receive duplicate revival"), F.Pawn->ReviveForDungeon(FVector::ZeroVector, 1.f));
    F.Pawn->Die(FVector::ZeroVector);
    TestEqual(TEXT("New down broadcasts exactly once again"), F.Pawn->Deaths, 2);
    TestTrue(TEXT("Full fraction revival accepted"), F.Pawn->ReviveForDungeon(Safe, 1.f));
    TestEqual(TEXT("Full revival caps at max health"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 200.f);
    TestEqual(TEXT("Full revival caps at max mana"), F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()),
              100.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonPlayerPassiveRevival,
                                 "test_25d.Roguelike.Player.ReviveRestoresOnlyActivePassives",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Player.ReviveRestoresOnlyActivePassives 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonPlayerPassiveRevival::RunTest(const FString&)
{
    DungeonPlayerRunTests::FFixture F;
    FGameplayAbilitySpec PassiveSpec(UDungeonPlayerRunTestPassive::StaticClass(), 2);
    PassiveSpec.DynamicAbilityTags.AddTag(FMyGameplayTags::Get().Abilities_Status_Equipped);
    const auto Passive = F.ASC->GiveAbility(PassiveSpec);
    const auto InactivePassive =
        F.ASC->GiveAbility(FGameplayAbilitySpec(UDungeonPlayerRunTestPassive::StaticClass(), 1));
    TestTrue(TEXT("Fixture passive listener activates"), F.ASC->TryActivateAbility(Passive));
    TestTrue(TEXT("Fixture offensive ability activates"), F.ASC->TryActivateAbility(F.Ability));
    TestTrue(TEXT("Passive is initially active"), F.ASC->FindAbilitySpecFromHandle(Passive)->IsActive());
    TestTrue(TEXT("Offensive ability is initially active"), F.ASC->FindAbilitySpecFromHandle(F.Ability)->IsActive());
    F.Pawn->Die(FVector::ZeroVector);
    TestFalse(TEXT("Death cancels active passive work"), F.ASC->FindAbilitySpecFromHandle(Passive)->IsActive());
    TestFalse(TEXT("Death cancels offensive work"), F.ASC->FindAbilitySpecFromHandle(F.Ability)->IsActive());
    TestTrue(TEXT("Room clear revival succeeds"), F.Pawn->ReviveForDungeon(FVector::ZeroVector));
    TestTrue(TEXT("Previously active listener resumes"), F.ASC->FindAbilitySpecFromHandle(Passive)->IsActive());
    TestFalse(TEXT("Previously inactive passive stays inactive"),
              F.ASC->FindAbilitySpecFromHandle(InactivePassive)->IsActive());
    TestFalse(TEXT("Offensive casts never replay on revive"), F.ASC->FindAbilitySpecFromHandle(F.Ability)->IsActive());
    TestEqual(TEXT("Restored passive rank remains unchanged"), F.ASC->FindAbilitySpecFromHandle(Passive)->Level, 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonPlayerNoToggleReplay,
                                 "test_25d.Roguelike.Player.ReviveNeverRestartsLegacyPassiveToggle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Player.ReviveNeverRestartsLegacyPassiveToggle 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonPlayerNoToggleReplay::RunTest(const FString&)
{
    DungeonPlayerRunTests::FFixture F;
    FGameplayAbilitySpec Spec(UDungeonPlayerRunTestToggle::StaticClass(), 1);
    Spec.DynamicAbilityTags.AddTag(FMyGameplayTags::Get().Abilities_Status_Equipped);
    const auto Toggle = F.ASC->GiveAbility(Spec);
    TestTrue(TEXT("Legacy passive-shaped toggle starts"), F.ASC->TryActivateAbility(Toggle));
    F.Pawn->Die(FVector::ZeroVector);
    TestFalse(TEXT("Down cancels the toggle"), F.ASC->FindAbilitySpecFromHandle(Toggle)->IsActive());
    TestTrue(TEXT("Revival succeeds"), F.Pawn->ReviveForDungeon(FVector::ZeroVector));
    TestFalse(TEXT("An offensive toggle requires a new press after revival"),
              F.ASC->FindAbilitySpecFromHandle(Toggle)->IsActive());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonPlayerNoUnequippedReplay,
                                 "test_25d.Roguelike.Player.ReviveRespectsChangedPassiveLoadout",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Player.ReviveRespectsChangedPassiveLoadout 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonPlayerNoUnequippedReplay::RunTest(const FString&)
{
    DungeonPlayerRunTests::FFixture F;
    FGameplayAbilitySpec Spec(UDungeonPlayerRunTestPassive::StaticClass(), 1);
    Spec.DynamicAbilityTags.AddTag(FMyGameplayTags::Get().Abilities_Status_Equipped);
    Spec.DynamicAbilityTags.AddTag(FMyGameplayTags::Get().InputTag_Passive_1);
    const auto Passive = F.ASC->GiveAbility(Spec);
    TestTrue(TEXT("Equipped passive starts"), F.ASC->TryActivateAbility(Passive));
    F.Pawn->Die(FVector::ZeroVector);
    // Equipping another passive through the existing UI RPC changes this live spec while downed.
    auto* Changed = F.ASC->FindAbilitySpecFromHandle(Passive);
    Changed->DynamicAbilityTags.RemoveTag(FMyGameplayTags::Get().Abilities_Status_Equipped);
    Changed->DynamicAbilityTags.RemoveTag(FMyGameplayTags::Get().InputTag_Passive_1);
    Changed->DynamicAbilityTags.AddTag(FMyGameplayTags::Get().Abilities_Status_Unlocked);
    TestTrue(TEXT("Revival succeeds after loadout change"), F.Pawn->ReviveForDungeon(FVector::ZeroVector));
    TestFalse(TEXT("Unequipped passive is not resurrected"), F.ASC->FindAbilitySpecFromHandle(Passive)->IsActive());
    TestTrue(TEXT("Unequipped skill remains learned"),
             F.ASC->FindAbilitySpecFromHandle(Passive)->DynamicAbilityTags.HasTagExact(
                 FMyGameplayTags::Get().Abilities_Status_Unlocked));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonPlayerDirectActivationBlock,
                                 "test_25d.Roguelike.Player.DownedBlocksDirectAbilityActivation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Player.DownedBlocksDirectAbilityActivation 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonPlayerDirectActivationBlock::RunTest(const FString&)
{
    DungeonPlayerRunTests::FFixture F;
    const auto Handle = F.ASC->GiveAbility(FGameplayAbilitySpec(UDungeonPlayerRunTestTaggedAbility::StaticClass(), 1));
    TestTrue(TEXT("Tagged skill can activate while living"), F.ASC->TryActivateAbility(Handle));
    F.ASC->CancelAbilityHandle(Handle);
    F.Pawn->Die(FVector::ZeroVector);
    TestFalse(TEXT("Downed pawn cannot bypass input gate with direct activation"), F.ASC->TryActivateAbility(Handle));
    const FGameplayTagContainer AllAbilities(FGameplayTag::RequestGameplayTag(TEXT("Abilities")));
    F.ASC->BlockAbilitiesWithTags(AllAbilities);
    TestTrue(TEXT("Revival succeeds with another system's activation block"),
             F.Pawn->ReviveForDungeon(FVector::ZeroVector));
    TestFalse(TEXT("Revival preserves another system's activation block"), F.ASC->TryActivateAbility(Handle));
    F.ASC->UnBlockAbilitiesWithTags(AllAbilities);
    TestTrue(TEXT("Dungeon's block was released exactly once"), F.ASC->TryActivateAbility(Handle));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonPlayerReviveValidation, "test_25d.Roguelike.Player.ReviveValidation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Player.ReviveValidation 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonPlayerReviveValidation::RunTest(const FString&)
{
    DungeonPlayerRunTests::FFixture F;
    F.Pawn->Die(FVector::ZeroVector);
    for (const float Fraction :
         {0.f, -1.f, 1.1f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        TestFalse(TEXT("Invalid fraction rejected"), F.Pawn->ReviveForDungeon(FVector::ZeroVector, Fraction));
    TestFalse(TEXT("Nonfinite safe location rejected"),
              F.Pawn->ReviveForDungeon(FVector(std::numeric_limits<float>::infinity(), 0, 0)));
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 0.f);
    TestFalse(TEXT("Cannot revive with zero maximum health"), F.Pawn->ReviveForDungeon(FVector::ZeroVector));
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 200.f);
    auto* WrongState = F.World->SpawnActor<AMyPlayerState>();
    F.ASC->InitAbilityActorInfo(WrongState, F.Pawn);
    TestFalse(TEXT("Cannot revive an ASC owned by another player"), F.Pawn->ReviveForDungeon(FVector::ZeroVector));
    F.ASC->InitAbilityActorInfo(F.State, WrongState);
    TestFalse(TEXT("Cannot revive an ASC attached to another avatar"), F.Pawn->ReviveForDungeon(FVector::ZeroVector));
    TestTrue(TEXT("Invalid attempts leave pawn downed"), F.Pawn->IsDungeonDowned());
    TestEqual(TEXT("Invalid attempts grant no health"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonPlayerNoStaleDamage,
                                 "test_25d.Roguelike.Player.ReviveDiscardsDamageReceivedWhileDowned",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Player.ReviveDiscardsDamageReceivedWhileDowned 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonPlayerNoStaleDamage::RunTest(const FString&)
{
    DungeonPlayerRunTests::FFixture F;
    F.Pawn->Die(FVector::ZeroVector);
    TestTrue(TEXT("First real damage effect applied while downed"), F.ApplyInstantDamage(11.f));
    TestTrue(TEXT("Second real damage effect applied while downed"), F.ApplyInstantDamage(13.f));
    TestEqual(TEXT("Downed damage does not change health"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 0.f);
    TestTrue(TEXT("Server revives the same pawn"), F.Pawn->ReviveForDungeon(FVector::ZeroVector));
    TestEqual(TEXT("Revive discards stale incoming-damage metadata"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetIncomingDamageAttribute()), 0.f);
    TestTrue(TEXT("Fresh real hit applied after revival"), F.ApplyInstantDamage(7.f));
    TestEqual(TEXT("First live hit deals only its own damage"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 53.f);
    TestFalse(TEXT("No stale-damage burst downs the revived pawn"), F.Pawn->IsDungeonDowned());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonPlayerDebuffCleanup,
                                 "test_25d.Roguelike.Player.ReviveClearsPeriodicDebuffsAndRetainsBuffs",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Player.ReviveClearsPeriodicDebuffsAndRetainsBuffs 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonPlayerDebuffCleanup::RunTest(const FString&)
{
    DungeonPlayerRunTests::FFixture F;
    const auto Buff = F.ApplyBeneficialArmor();
    const auto BeforeDown = F.ApplyPeriodicBurn();
    TestTrue(TEXT("Real periodic burn applied before down"), BeforeDown.IsValid());
    TestTrue(TEXT("Beneficial duration buff applied"), Buff.IsValid());
    F.Pawn->Die(FVector::ZeroVector);
    TestNull(TEXT("Down removes an existing harmful duration effect"), F.ASC->GetActiveGameplayEffect(BeforeDown));
    TestNotNull(TEXT("Down retains the beneficial duration buff"), F.ASC->GetActiveGameplayEffect(Buff));
    // A fatal effect can append its debuff after the Die callback; later effects can arrive too.
    const auto AfterDown = F.ApplyPeriodicBurn();
    TestTrue(TEXT("Late periodic burn is a real active effect"), AfterDown.IsValid());
    F.AdvanceEffects(.35f);
    TestTrue(TEXT("Late burn actually executed while the pawn was downed"),
             F.ASC->GetNumericAttribute(UMyAttributeSet::GetIncomingDamageAttribute()) >= 4.f);
    TestTrue(TEXT("Revival succeeds after periodic execution"), F.Pawn->ReviveForDungeon(FVector::ZeroVector));
    TestNull(TEXT("Revive also removes a debuff applied after down"), F.ASC->GetActiveGameplayEffect(AfterDown));
    TestEqual(TEXT("Periodic damage is not carried into living state"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetIncomingDamageAttribute()), 0.f);
    TestNotNull(TEXT("Revival retains the beneficial duration buff"), F.ASC->GetActiveGameplayEffect(Buff));
    TestEqual(TEXT("Beneficial armor magnitude is unchanged"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetArmorAttribute()), 7.f);
    F.AdvanceEffects(.35f);
    TestEqual(TEXT("Removed periodic effect cannot damage revived pawn later"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 60.f);
    TestTrue(TEXT("Fresh hit still follows the normal GAS damage path"), F.ApplyInstantDamage(7.f));
    TestEqual(TEXT("Fresh hit has no accumulated periodic burst"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 53.f);
    TestEqual(TEXT("Debuff cleanup leaves backpack intact"), F.ItemCount(), 3);
    TestNotNull(TEXT("Debuff cleanup leaves granted abilities intact"), F.ASC->FindAbilitySpecFromHandle(F.Ability));
    return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonPlayerClientAuthority, "test_25d.Roguelike.Player.ClientCannotDownOrRevive",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Player.ClientCannotDownOrRevive 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonPlayerClientAuthority::RunTest(const FString&)
{
    DungeonPlayerRunTests::FFixture F(EWorldType::PIE);
    F.World->SetPlayInEditorInitialNetMode(NM_Client);
    F.Pawn->SetTestRole(ROLE_SimulatedProxy);
    TestFalse(TEXT("Client fixture has no authority"), F.Pawn->HasAuthority());
    F.Pawn->Die(FVector::ZeroVector);
    TestFalse(TEXT("Client cannot author death"), F.Pawn->IsDungeonDowned());
    TestEqual(TEXT("Client cannot broadcast death"), F.Pawn->Deaths, 0);
    F.World->SetPlayInEditorInitialNetMode(NM_Standalone);
    F.Pawn->SetTestRole(ROLE_Authority);
    F.Pawn->Die(FVector::ZeroVector);
    F.World->SetPlayInEditorInitialNetMode(NM_Client);
    F.Pawn->SetTestRole(ROLE_SimulatedProxy);
    TestFalse(TEXT("Client cannot revive"), F.Pawn->ReviveForDungeon(FVector::ZeroVector));
    TestTrue(TEXT("Denied client revival preserves downed state"), F.Pawn->IsDungeonDowned());
    TestEqual(TEXT("Denied client revival grants no health"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 0.f);
    return true;
}
#endif
#endif
