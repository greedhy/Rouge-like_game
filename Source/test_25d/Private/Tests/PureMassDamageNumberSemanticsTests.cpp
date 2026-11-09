/**
 * 自动化调用链：RunTest -> PureMassSubsystem::ApplyDamage -> EvaluateDamage -> DamageFragment
 * -> PureMassPlayerController::QueueMassDamageNumber；测试在 0.1 秒批量发送前检查实际待发载荷。
 * 目标：飘字显示结算伤害（与 Actor IncomingDamage 一致），返回值保持实际移除生命，过量伤害不重复击杀或发经验。
 * 夹具仅使用临时世界和内存类型资产，不读取或覆盖玩家调整的 Content 配置；本文件不参与正常游戏 Tick。
 */
#include "Misc/AutomationTest.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassPlayerController.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "MyGameplayTags.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace PureMassDamageNumberSemanticsTests
{
    /** 拥有世界、来源 ASC 和计数委托；析构先解绑回调，避免世界清理访问已析构测试变量。 */
    struct FFixture
    {
        UWorld* World = nullptr;
        UPureMassSubsystem* Sim = nullptr;
        APureMassPlayerController* Controller = nullptr;
        ACharacter* Source = nullptr;
        UAbilitySystemComponent* SourceASC = nullptr;
        FDelegateHandle RemovalHandle;
        FDelegateHandle XPHandle;
        int32 KillEvents = 0;
        int32 XPEvents = 0;
        float AwardedXP = 0.f;

        /** 创建权威世界及真实控制器/ASC，保证伤害走生产飘字分支，而非直接调用队列。 */
        FFixture()
        {
            World = UWorld::CreateWorld(EWorldType::Game, false);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            Sim = World->GetSubsystem<UPureMassSubsystem>();
            if (!Sim)
            {
                return;
            }
            Sim->Enable();
            Controller = World->SpawnActor<APureMassPlayerController>();
            FActorSpawnParameters SpawnParameters;
            SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Source = World->SpawnActor<ACharacter>(FVector(-500, 0, 60), FRotator::ZeroRotator, SpawnParameters);
            if (!Controller || !Source)
            {
                return;
            }
            Controller->Possess(Source);
            SourceASC = NewObject<UAbilitySystemComponent>(Source);
            Source->AddInstanceComponent(SourceASC);
            SourceASC->RegisterComponent();
            SourceASC->InitAbilityActorInfo(Source, Source);
            SourceASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Source));
            RemovalHandle = Sim->OnMonsterRemoved.AddLambda(
                [this](int32, FName, EPureMassRemovalReason Reason)
                {
                    if (Reason == EPureMassRemovalReason::Killed)
                    {
                        ++KillEvents;
                    }
                });
            XPHandle =
                SourceASC->GenericGameplayEventCallbacks.FindOrAdd(FMyGameplayTags::Get().Attributes_Meta_IncomingXP)
                    .AddLambda(
                        [this](const FGameplayEventData* Event)
                        {
                            ++XPEvents;
                            AwardedXP += Event->EventMagnitude;
                        });
        }

        /** 先解绑测试捕获，再让世界销毁控制器、定时器和 Mass 实体，不触发额外游戏奖励。 */
        ~FFixture()
        {
            if (Sim)
            {
                Sim->OnMonsterRemoved.Remove(RemovalHandle);
            }
            if (SourceASC)
            {
                SourceASC->GenericGameplayEventCallbacks.FindOrAdd(FMyGameplayTags::Get().Attributes_Meta_IncomingXP)
                    .Remove(XPHandle);
            }
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }

        /** 为每个场景建立独立类型；无护甲/随机格挡/随机暴击使期望伤害可精确验证。 */
        UPureMassMonsterType* MakeType(float Health) const
        {
            auto* Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = Health;
            Type->Armor = 0.f;
            Type->BlockChance = 0.f;
            Type->CriticalResistance = 0.f;
            Type->XPReward = 13;
            return Type;
        }
    };
} // namespace PureMassDamageNumberSemanticsTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassDamageNumberSemanticsTest,
                                 "test_25d.PureMass.Combat.DamageNumbersUseEvaluatedDamage",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 验证致死/非致死与抵抗格挡暴击后的数字载荷，保持真实扣血和击杀奖励语义。 */
bool FPureMassDamageNumberSemanticsTest::RunTest(const FString&)
{
    using namespace PureMassDamageNumberSemanticsTests;
    FFixture F;
    if (!TestNotNull(TEXT("Mass subsystem exists"), F.Sim) ||
        !TestNotNull(TEXT("Source Mass controller exists"), F.Controller) ||
        !TestNotNull(TEXT("Source ASC exists"), F.SourceASC))
    {
        return false;
    }
    TestEqual(TEXT("Damage source is possessed by feedback recipient"), F.Source->GetController(),
              static_cast<AController*>(F.Controller));

    FPureMassDamage Damage;
    Damage.Amount = 4000.f;
    Damage.DamageType = FMyGameplayTags::Get().Damage_Physics;
    Damage.Source = F.Source;
    const int32 FragileId = F.Sim->SpawnMonster(F.MakeType(10.f), FVector(0, 0, 60));
    if (!TestTrue(TEXT("Ten-health target is spawned"), FragileId > 0))
    {
        return false;
    }
    TestEqual(TEXT("Overkill returns only actual HP removed"), F.Sim->ApplyDamage(FragileId, Damage), 10.f);
    if (TestEqual(TEXT("Overkill queues exactly one damage number"), F.Controller->PendingDamageNumbers.Num(), 1))
    {
        const auto& Number = F.Controller->PendingDamageNumbers[0];
        TestEqual(TEXT("Overkill number retains evaluated damage instead of remaining HP"), Number.Damage, 4000.f);
        TestEqual(TEXT("Damage number retains killed entity identity"), Number.EntityId, FragileId);
        TestFalse(TEXT("Unblocked damage has no block flag"), Number.bBlocked);
        TestFalse(TEXT("Noncritical damage has no critical flag"), Number.bCritical);
    }
    TestEqual(TEXT("Overkill sends one kill disposition"), F.KillEvents, 1);
    TestEqual(TEXT("Overkill sends one XP event"), F.XPEvents, 1);
    TestEqual(TEXT("XP magnitude comes from the defeated type"), F.AwardedXP, 13.f);
    TestEqual(TEXT("Duplicate lethal hit removes no health"), F.Sim->ApplyDamage(FragileId, Damage), 0.f);
    F.Sim->FinishUpdate();
    TestEqual(TEXT("Stale identity after cleanup removes no health"), F.Sim->ApplyDamage(FragileId, Damage), 0.f);
    TestEqual(TEXT("Duplicate hit and cleanup do not add numbers"), F.Controller->PendingDamageNumbers.Num(), 1);
    if (F.Controller->PendingDamageNumbers.Num() == 1)
    {
        TestEqual(TEXT("Duplicate hit cannot aggregate another overkill amount"),
                  F.Controller->PendingDamageNumbers[0].Damage, 4000.f);
    }
    TestEqual(TEXT("Duplicate hit cannot award another kill"), F.KillEvents, 1);
    TestEqual(TEXT("Duplicate hit cannot award another XP event"), F.XPEvents, 1);
    TestEqual(TEXT("Death counter remains one"), F.Sim->GetDeathCount(), int64(1));

    const int32 HealthyId = F.Sim->SpawnMonster(F.MakeType(5000.f), FVector(150, 0, 60));
    TestEqual(TEXT("Nonlethal hit returns full evaluated damage"), F.Sim->ApplyDamage(HealthyId, Damage), 4000.f);
    FPureMassView HealthyView;
    if (TestTrue(TEXT("Nonlethal target still resolves"), F.Sim->Resolve(HealthyId, HealthyView)))
    {
        TestEqual(TEXT("Nonlethal hit updates actual health"), HealthyView.Health, 1000.f);
    }
    const auto* HealthyNumber = F.Controller->PendingDamageNumbers.FindByPredicate(
        [HealthyId](const FPureMassDamageNumber& Number)
        {
            return Number.EntityId == HealthyId;
        });
    if (TestNotNull(TEXT("Nonlethal target has its own number"), HealthyNumber))
    {
        TestEqual(TEXT("Nonlethal number equals evaluated damage"), HealthyNumber->Damage, 4000.f);
    }

    // 确定性路径：4000 基础伤害 * 25% 抗性后剩余 * 50% 格挡 * 200% 暴击 = 1000。
    // 数字必须使用该结算值，不能用原始 4000；目标生命足够，结算与扣血在此场景相等。
    auto* MitigatedType = F.MakeType(2000.f);
    MitigatedType->Resistances.Add(Damage.DamageType, 75.f);
    MitigatedType->BlockChance = 100.f;
    F.SourceASC->SetNumericAttributeBase(UMyAttributeSet::GetCriticalHitChanceAttribute(), 100.f);
    F.SourceASC->SetNumericAttributeBase(UMyAttributeSet::GetCriticalHitDamageAttribute(), 100.f);
    const int32 MitigatedId = F.Sim->SpawnMonster(MitigatedType, FVector(300, 0, 60));
    TestEqual(TEXT("Resistance block and critical determine actual damage"), F.Sim->ApplyDamage(MitigatedId, Damage),
              1000.f);
    const auto* MitigatedNumber = F.Controller->PendingDamageNumbers.FindByPredicate(
        [MitigatedId](const FPureMassDamageNumber& Number)
        {
            return Number.EntityId == MitigatedId;
        });
    if (TestNotNull(TEXT("Mitigated target has a number"), MitigatedNumber))
    {
        TestEqual(TEXT("Number uses damage after mitigation and critical evaluation"), MitigatedNumber->Damage, 1000.f);
        TestTrue(TEXT("Evaluated block flag survives feedback queue"), MitigatedNumber->bBlocked);
        TestTrue(TEXT("Evaluated critical flag survives feedback queue"), MitigatedNumber->bCritical);
    }
    TestEqual(TEXT("Surviving targets do not award kills"), F.KillEvents, 1);
    TestEqual(TEXT("Surviving targets do not award XP"), F.XPEvents, 1);
    return true;
}
#endif
