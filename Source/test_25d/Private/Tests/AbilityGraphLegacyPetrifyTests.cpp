// 调用链：Automation -> 实际 GA_PetrifyingGaze 资产 -> ASC::TryActivateAbility -> 旧 GA Commit/Gather/Visible -> 共享石化服务。
// 仅创建临时 World/角色/实体；不修改资产、CDO 或原配置，用真实 GAS 入口验证提取前的行为。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilitySystem/Batch6/Batch6Petrify.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/UnifiedGraphAbility.h"
#include "AbilitySystemComponent.h"
#include "Character/EnemyCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "DungeonPlayerRunTestTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MyGameplayTags.h"
#include "PureMass/PureMassSubsystem.h"
#include "UObject/Script.h"

namespace AbilityGraphLegacyPetrifyTests
{
    /** 加载实际旧 GA；缺资产必须报失败，不能用手工构造的新配置代替回归对象。 */
    UClass* LoadLegacyClass(FAutomationTestBase& Test)
    {
        UClass* Class = LoadClass<UBatch6PetrifyAbility>(
            nullptr, TEXT("/Game/CompositionBatch6/GA_PetrifyingGaze.GA_PetrifyingGaze_C"));
        if (!Test.TestNotNull(TEXT("Existing PetrifyingGaze asset loads with original native parent"), Class))
        {
            return nullptr;
        }
        const auto* Defaults = Class->GetDefaultObject<UBatch6PetrifyAbility>();
        if (!Test.TestNotNull(TEXT("Legacy GA retains its prototype asset"), Defaults->Prototype.Get()) ||
            !Test.TestTrue(TEXT("Legacy prototype passes inherited activation validation"),
                           Defaults->Prototype->IsValidConfiguration()))
        {
            return nullptr;
        }
        return Class;
    }

    /** 真实 PlayerState ASC 驱动原 GA；测试不 BeginPlay，避免启动关卡生成、AI 或玩家存档路径。 */
    struct FFixture
    {
        UWorld* World = nullptr;
        ADungeonPlayerRunTestPawn* Source = nullptr;
        UAbilitySystemComponent* ASC = nullptr;
        FGameplayAbilitySpecHandle Handle;
        UBatch6PetrifyAbility* Ability = nullptr;
        float Radius = 0.f;
        float Duration = 0.f;
        float ManaCost = 0.f;
        float Cooldown = 0.f;
        float InitialMana = 0.f;
        FGameplayTag CooldownTag;

        /** 使用传入的原资产类授予技能，不替换费用/冷却或绕过 CanActivateAbility。 */
        explicit FFixture(UClass* AbilityClass)
        {
            const auto* Defaults = AbilityClass->GetDefaultObject<UBatch6PetrifyAbility>();
            Radius = Defaults->Radius;
            Duration = Defaults->Duration;
            ManaCost = Defaults->Prototype->ManaCost;
            Cooldown = Defaults->Prototype->Cooldown;
            CooldownTag = Defaults->Prototype->CooldownTag;
            InitialMana = FMath::Max(100.f, ManaCost * 3.f);
            World = UWorld::CreateWorld(EWorldType::Game, false);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Source = World->SpawnActor<ADungeonPlayerRunTestPawn>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
            Source->AttachState(World->SpawnActor<AMyPlayerState>());
            Source->Tags = {TEXT("Player")};
            ASC = Source->GetAbilitySystemComponent();
            // 测试世界未 BeginPlay，ASC 尚未自动收集属性子对象；先显式注册再写入法力。
            ASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Source));
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), InitialMana);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), InitialMana);
            Handle = ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1));
            if (FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Handle))
            {
                Ability = Cast<UBatch6PetrifyAbility>(Spec->GetPrimaryInstance());
            }
        }

        /** 取消/移除测试技能，再由 World 销毁负责组件 GE 与子系统回收；提前返回也移除 WorldContext。 */
        ~FFixture()
        {
            ASC->CancelAllAbilities();
            ASC->ClearAllAbilities();
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }

        /** 在指定中心位置生成独立敌人，避免碰撞修正把边界夹具移回半径内。 */
        AEnemyCharacter* SpawnActorTarget(FVector Location)
        {
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Enemy = World->SpawnActor<AEnemyCharacter>(Location, FRotator::ZeroRotator, Params);
            Enemy->Tags = {TEXT("Enemy")};
            auto* EnemyASC = Enemy->GetAbilitySystemComponent();
            EnemyASC->InitAbilityActorInfo(Enemy, Enemy);
            // 与来源相同，未 BeginPlay 的敌人需要显式注册供健康断言使用的属性集。
            EnemyASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Enemy));
            EnemyASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
            EnemyASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
            return Enemy;
        }

        /** 使用实际 Mass 子系统实体，范围与石化状态都来自生产查询/片段。 */
        int32 SpawnMassTarget(FVector Location)
        {
            auto* Mass = World->GetSubsystem<UPureMassSubsystem>();
            Mass->Enable();
            auto* Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = 100.f;
            Type->XPReward = 0;
            return Mass->SpawnMonster(Type, Location);
        }

        /** 验证已有配置对应的实际冷却 GE，而非只检查 CDO 中存了一个数字。 */
        void CheckCommittedCooldown(FAutomationTestBase& Test) const
        {
            if (Cooldown <= 0.f)
            {
                Test.TestTrue(TEXT("Zero configured cooldown leaves no active cooldown effect"),
                              ASC->GetActiveEffects(FGameplayEffectQuery()).IsEmpty());
                return;
            }
            const FGameplayEffectQuery Query =
                FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(CooldownTag));
            const TArray<FActiveGameplayEffectHandle> Effects = ASC->GetActiveEffects(Query);
            Test.TestTrue(TEXT("Original cooldown tag is active"), ASC->HasMatchingGameplayTag(CooldownTag));
            if (Test.TestEqual(TEXT("Exactly one original cooldown applied"), Effects.Num(), 1))
            {
                const FActiveGameplayEffect* Effect = ASC->GetActiveGameplayEffect(Effects[0]);
                if (Test.TestNotNull(TEXT("Cooldown owns a real active effect"), Effect))
                {
                    Test.TestEqual(TEXT("Actual cooldown retains configured duration"), Effect->Spec.GetDuration(),
                                   Cooldown);
                }
            }
        }
    };
} // namespace AbilityGraphLegacyPetrifyTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnifiedLegacyPetrifyMixedRange,
                                 "test_25d.UnifiedGA.LegacyPetrify.MixedRangeCostAndCooldown",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 捕获漏转发某类目标、忽略原范围配置、重复扣费/冷却、错用新样例持续时间或额外伤害。 */
bool FUnifiedLegacyPetrifyMixedRange::RunTest(const FString&)
{
    FEditorScriptExecutionGuard Guard;
    UClass* LegacyClass = AbilityGraphLegacyPetrifyTests::LoadLegacyClass(*this);
    if (!LegacyClass)
    {
        return false;
    }
    AbilityGraphLegacyPetrifyTests::FFixture F(LegacyClass);
    if (!TestNotNull(TEXT("ASC owns an instance of the existing GA"), F.Ability))
    {
        return false;
    }
    AEnemyCharacter* InsideActor = F.SpawnActorTarget(FVector(F.Radius * .99f, 0, 0));
    AEnemyCharacter* OutsideActor = F.SpawnActorTarget(FVector(F.Radius * 1.01f, 0, 0));
    const int32 InsideMass = F.SpawnMassTarget(FVector(0, F.Radius * .99f, 0));
    const int32 OutsideMass = F.SpawnMassTarget(FVector(0, F.Radius * 1.01f, 0));
    TestTrue(TEXT("Actual old GA activates through ASC"), F.ASC->TryActivateAbility(F.Handle));
    TestEqual(TEXT("One cast still charges configured cost once"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), F.InitialMana - F.ManaCost);
    TestFalse(TEXT("Original synchronous GA ends after range application"), F.Ability->IsActive());
    F.CheckCommittedCooldown(*this);
    TestTrue(TEXT("Actor inside original radius receives stun"),
             InsideActor->GetAbilitySystemComponent()->HasMatchingGameplayTag(FMyGameplayTags::Get().Debuff_Stun));
    TestTrue(TEXT("Actor state survives original GA completion"), InsideActor->GetMesh()->bPauseAnims);
    TestNull(TEXT("Actor outside original radius receives no component"),
             OutsideActor->FindComponentByClass<UBatch6PetrifyComponent>());
    TestEqual(TEXT("Petrify still deals no Actor damage"),
              InsideActor->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()),
              100.f);
    auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
    FPureMassView InsideView;
    FPureMassView OutsideView;
    TestTrue(TEXT("Inside Mass remains alive"), Mass->Resolve(InsideMass, InsideView));
    TestTrue(TEXT("Mass inside original radius is petrified"), InsideView.bPetrified);
    TestTrue(TEXT("Outside Mass remains alive"), Mass->Resolve(OutsideMass, OutsideView));
    TestFalse(TEXT("Mass outside original radius is unaffected"), OutsideView.bPetrified);
    TestEqual(TEXT("Petrify still deals no Mass damage"), InsideView.Health, 100.f);
    if (InsideMass > 0 && InsideView.bPetrified)
    {
        auto& Manager = F.World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
        const auto& Fragment = Manager.GetFragmentDataChecked<FPureMassFragment>(Mass->GetEntity(InsideMass));
        TestEqual(TEXT("Original GA keeps configured state duration"), Fragment.PetrifyRemaining, F.Duration);
    }
    if (F.Cooldown > 0.f)
    {
        TestFalse(TEXT("Original cooldown blocks immediate reuse"), F.ASC->TryActivateAbility(F.Handle));
        TestEqual(TEXT("Blocked repeat cannot charge another cost"),
                  F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), F.InitialMana - F.ManaCost);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnifiedLegacyPetrifyEmptyCast,
                                 "test_25d.UnifiedGA.LegacyPetrify.EmptyCastStillCommits",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 捕获把旧范围技能改为必须有目标才 Commit 的行为变化；空放仍正常结束并消耗原费用/冷却。 */
bool FUnifiedLegacyPetrifyEmptyCast::RunTest(const FString&)
{
    FEditorScriptExecutionGuard Guard;
    UClass* LegacyClass = AbilityGraphLegacyPetrifyTests::LoadLegacyClass(*this);
    if (!LegacyClass)
    {
        return false;
    }
    AbilityGraphLegacyPetrifyTests::FFixture F(LegacyClass);
    if (!TestNotNull(TEXT("Existing GA granted"), F.Ability))
    {
        return false;
    }
    TestTrue(TEXT("Empty range activation succeeds"), F.ASC->TryActivateAbility(F.Handle));
    TestEqual(TEXT("Empty cast still charges configured cost once"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), F.InitialMana - F.ManaCost);
    F.CheckCommittedCooldown(*this);
    TestFalse(TEXT("Empty cast ends normally"), F.Ability->IsActive());
    if (F.Cooldown > 0.f)
    {
        TestFalse(TEXT("Empty cast still blocks on cooldown"), F.ASC->TryActivateAbility(F.Handle));
        TestEqual(TEXT("Rejected empty repeat does not double charge"),
                  F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), F.InitialMana - F.ManaCost);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnifiedLegacyPetrifyInsufficientMana,
                                 "test_25d.UnifiedGA.LegacyPetrify.InsufficientManaNoEffects",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 捕获共享服务接入后绕过旧父类 CheckCost，或失败激活仍施加状态/冷却。 */
bool FUnifiedLegacyPetrifyInsufficientMana::RunTest(const FString&)
{
    FEditorScriptExecutionGuard Guard;
    UClass* LegacyClass = AbilityGraphLegacyPetrifyTests::LoadLegacyClass(*this);
    if (!LegacyClass)
    {
        return false;
    }
    AbilityGraphLegacyPetrifyTests::FFixture F(LegacyClass);
    if (F.ManaCost <= 0.f)
    {
        AddInfo(TEXT("Current designer tuning has no mana cost; insufficient-mana scenario does not apply."));
        return true;
    }
    AEnemyCharacter* Target = F.SpawnActorTarget(FVector(F.Radius * .5f, 0, 0));
    const float InsufficientMana = F.ManaCost * .5f;
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), InsufficientMana);
    TestFalse(TEXT("Old CheckCost rejects insufficient mana"), F.ASC->TryActivateAbility(F.Handle));
    TestEqual(TEXT("Rejected cast retains mana"), F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()),
              InsufficientMana);
    TestNull(TEXT("Rejected cast creates no petrify component"),
             Target->FindComponentByClass<UBatch6PetrifyComponent>());
    TestFalse(TEXT("Rejected cast applies no cooldown"), F.ASC->HasMatchingGameplayTag(F.CooldownTag));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnifiedAuthoredSample, "test_25d.UnifiedGA.Integration.AuthoredSample",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 捕获保存后的样例丢失图/GE 引用、配置实例或标准 Mana/冷却协议；允许设计师调整有限正持续时间和费用。 */
bool FUnifiedAuthoredSample::RunTest(const FString&)
{
    UClass* AbilityClass = LoadClass<UUnifiedGraphAbility>(
        nullptr, TEXT("/Game/UnifiedAbilities/Samples/GA_DelayedPetrify.GA_DelayedPetrify_C"));
    UClass* CostClass = LoadClass<UGameplayEffect>(
        nullptr, TEXT("/Game/UnifiedAbilities/Samples/GE_DelayedPetrifyCost.GE_DelayedPetrifyCost_C"));
    UClass* CooldownClass = LoadClass<UGameplayEffect>(
        nullptr, TEXT("/Game/UnifiedAbilities/Samples/GE_DelayedPetrifyCooldown.GE_DelayedPetrifyCooldown_C"));
    auto* Graph = LoadObject<UAbilityGraphDefinition>(
        nullptr, TEXT("/Game/UnifiedAbilities/Samples/DA_DelayedPetrify.DA_DelayedPetrify"));
    if (!TestNotNull(TEXT("Authored sample GA loads"), AbilityClass) ||
        !TestNotNull(TEXT("Authored standard cost GE loads"), CostClass) ||
        !TestNotNull(TEXT("Authored standard cooldown GE loads"), CooldownClass) ||
        !TestNotNull(TEXT("Authored graph loads"), Graph))
    {
        return false;
    }
    const auto* Ability = AbilityClass->GetDefaultObject<UUnifiedGraphAbility>();
    TestTrue(TEXT("GA retains authored graph reference"), Ability->Graph == Graph);
    FAbilityNodeRegistry Registry;
    Registry.RegisterBuiltins();
    TArray<FString> Errors;
    TestTrue(TEXT("Reloaded graph passes native validation"), Graph->Validate(Registry, Errors));
    for (const FString& Error : Errors)
    {
        AddError(Error);
    }
    const UGameplayEffect* Cost = Ability->GetCostGameplayEffect();
    const UGameplayEffect* Cooldown = Ability->GetCooldownGameplayEffect();
    if (!TestNotNull(TEXT("GA standard cost hook is configured"), Cost) ||
        !TestNotNull(TEXT("GA standard cooldown hook is configured"), Cooldown))
    {
        return false;
    }
    TestTrue(TEXT("Standard cost hook retains authored class"), Cost->GetClass() == CostClass);
    TestTrue(TEXT("Standard cooldown hook retains authored class"), Cooldown->GetClass() == CooldownClass);
    TestTrue(TEXT("Cost uses an instant standard GE"), Cost->DurationPolicy == EGameplayEffectDurationType::Instant);
    int32 ManaModifiers = 0;
    float ConfiguredManaDelta = 0.f;
    for (const FGameplayModifierInfo& Modifier : Cost->Modifiers)
    {
        if (Modifier.Attribute == UMyAttributeSet::GetManaAttribute())
        {
            ++ManaModifiers;
            TestTrue(TEXT("Mana cost uses additive modifier"), Modifier.ModifierOp == EGameplayModOp::Additive);
            TestTrue(TEXT("Mana magnitude is available at level one"),
                     Modifier.ModifierMagnitude.GetStaticMagnitudeIfPossible(1.f, ConfiguredManaDelta));
            TestTrue(TEXT("Configured mana delta is a finite cost"),
                     FMath::IsFinite(ConfiguredManaDelta) && ConfiguredManaDelta < 0.f);
        }
    }
    TestEqual(TEXT("Authored cost has one Mana modifier"), ManaModifiers, 1);
    TestTrue(TEXT("Cooldown uses a timed standard GE"),
             Cooldown->DurationPolicy == EGameplayEffectDurationType::HasDuration);
    const FGameplayEffectSpec CooldownSpec(Cooldown, FGameplayEffectContextHandle(), 1.f);
    const float ConfiguredCooldown = CooldownSpec.GetDuration();
    TestTrue(TEXT("Configured cooldown is finite and positive"),
             FMath::IsFinite(ConfiguredCooldown) && ConfiguredCooldown > 0.f);
    FGameplayTagContainer GrantedTags;
    CooldownSpec.GetAllGrantedTags(GrantedTags);
    TestTrue(TEXT("Cooldown grants the sample's native identity"),
             GrantedTags.HasTagExact(FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Unified.DelayedPetrify"))));
    AddInfo(FString::Printf(TEXT("Reloaded sample uses level-one Mana delta %.3f and cooldown %.3f seconds."),
                            ConfiguredManaDelta, ConfiguredCooldown));
    return true;
}
#endif
