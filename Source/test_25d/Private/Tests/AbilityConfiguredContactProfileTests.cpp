// 测试链：真实Mixed Orbit宿主的原Hit载荷 -> Actor实际GE/HP/Context -> 显式ConfiguredForces图载荷对照。
// 本用例验证伤害owner合同；直接调用生产Hit边界，不声称覆盖Orbit轨迹或实体投射物飞行。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/Data/OrbitingFireBoltConfig.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/LegacyMixedAbilityDriver.h"
#include "AbilitySystem/Unified/Nodes/AbilityInputDamageNode.h"
#include "Actor/MyOrbitingFireBolt.h"
#include "PureMass/PureMassRuntime.h"
#include "PureMass/PureMassIncomingDamageEffect.h"
#include "MyGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "UObject/Script.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConfiguredContactMixedActor,
                                 "test_25d.DynamicMage.ContactProfiles.ConfiguredForcesMatchesOriginalMixedActor",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConfiguredContactMixedActor::RunTest(const FString&)
{
    struct FObserved
    {
        int32 Count = 0;
        FVector Death = FVector::ZeroVector;
        FVector Knockback = FVector::ZeroVector;
        float Main = 0.f;
        float DOT = 0.f;
    };
    FObserved Original;
    int32 OldNextRandom = 0;
    for (const bool bFine : {false, true})
    {
        FAbilityGraphTestFixture F;
        FEditorScriptExecutionGuard ScriptGuard;
        auto* TargetASC = F.Target->GetAbilitySystemComponent();
        TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 1000.f);
        TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 1000.f);
        FDamageEffectParams Params;
        Params.SourceAbilitySystemComponent = F.ASC;
        Params.DamageGameplayEffectClass = UPureMassIncomingDamageEffect::StaticClass();
        Params.DamageType = UPureMassIncomingDamageEffect::GetDamageTag();
        Params.BaseDamage = 25.f;
        Params.DebuffDamage = 8.f;
        Params.DebuffChance = 100.f;
        Params.DebuffDuration = 2.f;
        Params.DebuffFrequency = .5f;
        Params.DeathImpulse = FVector(11, 22, 33);
        Params.KnockbackForce = FVector(-44, 55, 66);
        Params.DeathImpulseMagnitude = 10000.f;
        Params.KnockbackForceMagnitude = 15000.f;
        Params.KnockbackChance = 100.f; // Mixed旧Hit仍不应掷击退随机数，也不应覆盖显式向量。
        FObserved Actual;
        const auto Hook = TargetASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
            [&Actual](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
            {
                ++Actual.Count;
                Actual.Death = UMyAbilitySystemLibrary::GetDeathImpulse(Spec.GetContext());
                Actual.Knockback = UMyAbilitySystemLibrary::GetKnockbackForce(Spec.GetContext());
                Actual.Main = Spec.GetSetByCallerMagnitude(UPureMassIncomingDamageEffect::GetDamageTag(), false);
                Actual.DOT = Spec.GetSetByCallerMagnitude(FMyGameplayTags::Get().Debuff_Damage, false);
            });
        FString Error;
        constexpr int32 Seed = 502911;
        if (!bFine)
        {
            auto* Config = NewObject<UAbilityPrototype>(F.World);
            Config->Kind = EAbilityPrototypeKind::Orbit;
            Config->Damage = 50.f;
            Config->DamagePerLevel = 0.f;
            Config->DamageType = Params.DamageType;
            Config->Cooldown = 0.f;
            Config->OrbitSettings = NewObject<UOrbitingFireBoltConfig>(Config);
            Config->OrbitSettings->FireBoltClass = AMyOrbitingFireBolt::StaticClass();
            auto* Context = NewObject<UPrototypeCastContext>(F.World);
            Context->Caster = F.Source;
            Context->DamageParams = Params;
            // 对照的是同一已解析数值：旧driver以Prototype覆写主伤，DOT与向量仍来自原Params。
            Context->DamageParams.BaseDamage = 50.f;
            Context->DamageParams.DebuffDamage = 2.f;
            auto* Host = APureMassRuntime::Spawn(Context, Config, FVector::ZeroVector, FVector::ForwardVector);
            if (!TestNotNull(TEXT("Actual original mixed Orbit host spawns"), Host))
            {
                TargetASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Hook);
                return false;
            }
            if (!Host->HasActorBegunPlay())
            {
                Host->DispatchBeginPlay();
            }
            if (!TestTrue(TEXT("Actual original driver exists without ticking inventory"), Host->GetDriver().IsValid()))
            {
                TargetASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Hook);
                return false;
            }
            FMath::RandInit(Seed);
            TestTrue(TEXT("Production mixed contact payload accepts the actual original Actor life"),
                     Host->GetDriver()->Hit(FPureMassCombatTarget::FromActor(F.Target)));
        }
        else
        {
            auto* Graph = NewObject<UAbilityGraphDefinition>(F.World);
            Graph->SchemaVersion = 2;
            Graph->CompatibilityProfile = TEXT("UnifiedV2");
            const FGuid Id = FGuid::NewGuid();
            Graph->EntryNodeId = Id;
            Graph->Nodes = {{Id, TEXT("Effect.InputDamage"), NewObject<UAbilityInputDamageNodeConfig>(Graph)}};
            FAbilityNodeInput Input;
            Input.SourceToken = F.SourceToken;
            Input.LiveTargetToken = F.TargetToken;
            Input.Origin = F.Target->GetActorLocation();
            Input.DamageParams = Params;
            Input.DamageScale = 2.f;
            Input.DebuffDamageScale = .25f;
            FAbilityImpactSnapshot Impact;
            Impact.Location = Input.Origin;
            Impact.Direction = FVector::ForwardVector;
            Impact.ConfirmedTarget = F.TargetToken;
            Impact.ContactDamageProfile = EAbilityContactDamageProfile::ConfiguredForces;
            Input.Impact = Impact;
            auto* Run = NewObject<UAbilityGraphInstance>(F.World);
            FMath::RandInit(Seed);
            TestTrue(TEXT("Explicit configured-force payload graph starts through the real damage owner"),
                     Run->StartWithInput(Graph, Input, {}, Error));
            TestEqual(TEXT("Shared original main params remain raw for later contacts"), Input.DamageParams.BaseDamage,
                      25.f);
            TestEqual(TEXT("Shared original DOT params remain independently raw"), Input.DamageParams.DebuffDamage,
                      8.f);
        }
        const int32 NextRandom = FMath::Rand();
        if (!bFine)
        {
            Original = Actual;
            OldNextRandom = NextRandom;
            FMath::RandInit(Seed);
            TestEqual(TEXT("Original mixed payload consumes no implicit knockback RNG"), OldNextRandom, FMath::Rand());
            TestEqual(TEXT("Original control creates one actual GE"), Original.Count, 1);
            TestEqual(TEXT("Original control preserves its authored death impulse"), Original.Death,
                      Params.DeathImpulse);
            TestEqual(TEXT("Original control preserves authored knockback"), Original.Knockback, Params.KnockbackForce);
            TestEqual(TEXT("Original control carries resolved main damage"), Original.Main, 50.f);
            TestEqual(TEXT("Original control carries independent resolved DOT"), Original.DOT, 2.f);
        }
        else
        {
            TestEqual(TEXT("Configured profile matches old exact GE count"), Actual.Count, Original.Count);
            TestEqual(TEXT("Configured profile never derives a new impulse"), Actual.Death, Original.Death);
            TestEqual(TEXT("Configured profile never replaces preset knockback"), Actual.Knockback, Original.Knockback);
            TestEqual(TEXT("Configured profile consumes cumulative main once"), Actual.Main, Original.Main);
            TestEqual(TEXT("Configured profile consumes independent DOT once"), Actual.DOT, Original.DOT);
            TestEqual(TEXT("Configured profile adds no RNG draw"), NextRandom, OldNextRandom);
        }
        TestEqual(TEXT("Both actual damage owners produce the same real HP"),
                  TargetASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 950.f);
        TargetASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Hook);
    }
    return true;
}
#endif
