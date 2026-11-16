// 调用链：Automation真实ASC/暗金GE -> 原FireBolt或图共享发射 -> 弹体数量、伤害、套装及生命周期断言。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityTraditionalTestAbility.h"
#include "AbilitySystem/Unified/Nodes/AbilityTraditionalNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "Equipment/UniqueEquipmentGameplayEffect.h"
#include "Actor/MyProjectile.h"
#include "EngineUtils.h"
#include "Engine/CurveTable.h"
#include "Misc/AutomationTest.h"
#include "MyGameplayTags.h"
#include "NativeGameplayTags.h"

namespace UniqueEquipmentProjectileTests
{
    UE_DEFINE_GAMEPLAY_TAG_STATIC(PrismTag, "Test.Equipment.Unique.Prism");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(CrownTag, "Test.Equipment.Unique.Crown");

    /** 每件装备独立Spec/句柄；测试Tag由GE持有，卸下真实撤销。 */
    FActiveGameplayEffectHandle ApplyEffect(FAbilityGraphTestFixture& F, UUniqueEquipmentGameplayEffect* Effect,
                                            FGameplayTag Piece, float Level = 1.f)
    {
        FGameplayEffectSpec Spec(Effect, F.ASC->MakeEffectContext(), Level);
        if (Piece.IsValid())
        {
            Spec.DynamicGrantedTags.AddTag(Piece);
        }
        return F.ASC->ApplyGameplayEffectSpecToSelf(Spec);
    }

    UUniqueEquipmentGameplayEffect* Prism(FAbilityGraphTestFixture& F)
    {
        auto* Effect = NewObject<UUniqueEquipmentGameplayEffect>(F.World);
        auto& Rule = Effect->ProjectileRule;
        Rule.AbilityTag = FMyGameplayTags::Get().Abilities_Fire_FireBolt;
        Rule.ExtraProjectiles = FScalableFloat(2.f);
        Rule.DamageMultiplier = FScalableFloat(.55f);
        Rule.MinimumSpread = FScalableFloat(40.f);
        Rule.bDisableHoming = true;
        Rule.RequiredSetTags.AddTag(PrismTag);
        Rule.RequiredSetTags.AddTag(CrownTag);
        Rule.SetDamageMultiplier = FScalableFloat(.70f);
        return Effect;
    }

    FAbilityActorProjectileSettings Settings()
    {
        FAbilityActorProjectileSettings Result;
        Result.ProjectileClass = AMyProjectile::StaticClass();
        Result.Count = 5;
        Result.bLimitCountToLevel = true;
        Result.bConfigureHoming = true;
        Result.EquipmentAbilityTag = FMyGameplayTags::Get().Abilities_Fire_FireBolt;
        return Result;
    }

    FDamageEffectParams Damage()
    {
        FDamageEffectParams Result;
        Result.BaseDamage = 100.f;
        Result.DebuffDamage = 20.f;
        Result.DebuffChance = 25.f;
        Result.DebuffDuration = 4.f;
        return Result;
    }

    TArray<AMyProjectile*> Spawn(FAbilityGraphTestFixture& F, const FAbilityActorProjectileSettings& Settings,
                                 float Level = 1.f)
    {
        return AbilityTraditionalActions::SpawnProjectiles(F.SourceToken, Damage(), Settings, Level,
                                                           FVector::ZeroVector, FVector(1000, 0, 0), F.Target);
    }
} // namespace UniqueEquipmentProjectileTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUniqueProjectileVariants, "test_25d.Equipment.Unique.ProjectileVariantsAndSet",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUniqueProjectileVariants::RunTest(const FString&)
{
    using namespace UniqueEquipmentProjectileTests;
    FAbilityGraphTestFixture F;
    auto* Effect = Prism(F);
    const auto PrismHandle = ApplyEffect(F, Effect, PrismTag);
    TestTrue(TEXT("Prism effect is active"), PrismHandle.IsValid());
    const auto Config = Settings();
    const auto First = Spawn(F, Config);
    TestEqual(TEXT("Level one receives two extra projectiles after its level cap"), First.Num(), 3);
    for (const auto* Projectile : First)
    {
        TestTrue(TEXT("Prism scales direct damage"),
                 FMath::IsNearlyEqual(Projectile->DamageEffectParams.BaseDamage, 55.f));
        TestTrue(TEXT("Prism scales burning damage"),
                 FMath::IsNearlyEqual(Projectile->DamageEffectParams.DebuffDamage, 11.f));
        TestEqual(TEXT("Burn chance is preserved"), Projectile->DamageEffectParams.DebuffChance, 25.f);
        TestEqual(TEXT("Burn duration is preserved"), Projectile->DamageEffectParams.DebuffDuration, 4.f);
        TestFalse(TEXT("Prism disables homing"), Projectile->ProjectileMovement->bIsHomingProjectile);
    }
    if (First.Num() == 3)
    {
        TestTrue(TEXT("Minimum spread covers negative side"),
                 FMath::IsNearlyEqual(First[0]->GetActorRotation().Yaw, -20.));
        TestTrue(TEXT("Minimum spread covers positive side"),
                 FMath::IsNearlyEqual(First.Last()->GetActorRotation().Yaw, 20.));
    }
    // 同一件Tag的计数变为2不能代替套装的另一种装备。
    const auto DuplicateTag = ApplyEffect(F, NewObject<UUniqueEquipmentGameplayEffect>(F.World), PrismTag);
    const auto SamePiece = Spawn(F, Config);
    if (!SamePiece.IsEmpty())
    {
        TestTrue(TEXT("Duplicate piece tag cannot complete two distinct pieces"),
                 FMath::IsNearlyEqual(SamePiece[0]->DamageEffectParams.BaseDamage, 55.f));
    }
    const auto CrownHandle = ApplyEffect(F, NewObject<UUniqueEquipmentGameplayEffect>(F.World), CrownTag);
    const auto FullSet = Spawn(F, Config, 3.f);
    TestEqual(TEXT("Level three adds two to the original three"), FullSet.Num(), 5);
    if (!FullSet.IsEmpty())
    {
        TestTrue(TEXT("Two different set tags replace penalty with seventy percent"),
                 FMath::IsNearlyEqual(FullSet[0]->DamageEffectParams.BaseDamage, 70.f));
        TestTrue(TEXT("Set also scales burn"), FMath::IsNearlyEqual(FullSet[0]->DamageEffectParams.DebuffDamage, 14.f));
    }
    F.ASC->RemoveActiveGameplayEffect(CrownHandle);
    const auto BrokenSet = Spawn(F, Config);
    if (!BrokenSet.IsEmpty())
    {
        TestTrue(TEXT("Removing crown restores prism-only penalty on next shot"),
                 FMath::IsNearlyEqual(BrokenSet[0]->DamageEffectParams.BaseDamage, 55.f));
    }
    F.ASC->RemoveActiveGameplayEffect(PrismHandle);
    F.ASC->RemoveActiveGameplayEffect(DuplicateTag);
    const auto Normal = Spawn(F, Config);
    TestEqual(TEXT("Removing prism restores original count"), Normal.Num(), 1);
    if (!Normal.IsEmpty())
    {
        TestEqual(TEXT("Original direct damage restored"), Normal[0]->DamageEffectParams.BaseDamage, 100.f);
        TestTrue(TEXT("Original homing restored"), Normal[0]->ProjectileMovement->bIsHomingProjectile);
    }
    for (const auto* Projectile : First)
    {
        TestTrue(TEXT("Unequip does not rewrite in-flight damage"),
                 FMath::IsNearlyEqual(Projectile->DamageEffectParams.BaseDamage, 55.f));
    }
    TestEqual(TEXT("Shared input settings not mutated"), Config.Count, 5);
    TestEqual(TEXT("Shared GE rule not mutated"), Effect->ProjectileRule.DamageMultiplier.GetValueAtLevel(1), .55f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUniqueProjectileBounds, "test_25d.Equipment.Unique.ProjectileOptInAndBounds",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUniqueProjectileBounds::RunTest(const FString&)
{
    using namespace UniqueEquipmentProjectileTests;
    FAbilityGraphTestFixture F;
    auto* Effect = Prism(F);
    auto Handle = ApplyEffect(F, Effect, PrismTag);
    auto Config = Settings();
    Config.EquipmentAbilityTag = {};
    auto Unchanged = Spawn(F, Config);
    TestEqual(TEXT("Empty skill identity opts out"), Unchanged.Num(), 1);
    if (!Unchanged.IsEmpty())
    {
        TestEqual(TEXT("Empty identity retains damage"), Unchanged[0]->DamageEffectParams.BaseDamage, 100.f);
    }
    Config.EquipmentAbilityTag = FMyGameplayTags::Get().Damage_Fire;
    TestEqual(TEXT("Damage identity cannot impersonate skill"), Spawn(F, Config).Num(), 1);
    Config = Settings();
    Config.Count = 0;
    TestEqual(TEXT("Equipment cannot enable an intentionally empty attack"), Spawn(F, Config).Num(), 0);
    F.ASC->RemoveActiveGameplayEffect(Handle);
    // 每个独立活跃Spec只组合一次，最终弹数有上限；不把GrantedTag计数作为循环次数。
    auto* Many = Prism(F);
    Many->ProjectileRule.ExtraProjectiles = FScalableFloat(16.f);
    const auto First = ApplyEffect(F, Many, PrismTag);
    const auto Second = ApplyEffect(F, Many, PrismTag);
    Config = Settings();
    TestEqual(TEXT("Multiple legitimate rules cap total count at thirty-two"), Spawn(F, Config).Num(), 32);
    F.ASC->RemoveActiveGameplayEffect(First);
    F.ASC->RemoveActiveGameplayEffect(Second);
    auto* Invalid = Prism(F);
    Invalid->ProjectileRule.DamageMultiplier = FScalableFloat(-1.f);
    const auto InvalidHandle = ApplyEffect(F, Invalid, PrismTag);
    auto Rejected = Spawn(F, Config);
    TestEqual(TEXT("Invalid rule cannot add projectiles"), Rejected.Num(), 1);
    if (!Rejected.IsEmpty())
    {
        TestEqual(TEXT("Invalid rule cannot make negative damage"), Rejected[0]->DamageEffectParams.BaseDamage, 100.f);
    }
    F.ASC->RemoveActiveGameplayEffect(InvalidHandle);
    F.ASC->InitAbilityActorInfo(F.Source, F.Target);
    TestEqual(TEXT("Old source life cannot spawn equipment projectiles"), Spawn(F, Config).Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUniqueProjectileCurves,
                                 "test_25d.Equipment.Unique.ProjectileCurveLevelsAndInhibition",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUniqueProjectileCurves::RunTest(const FString&)
{
    using namespace UniqueEquipmentProjectileTests;
    FAbilityGraphTestFixture F;
    auto* Effect = Prism(F);
    auto* Table = NewObject<UCurveTable>(F.World);
    const auto AddCurve = [Table](const FName Name, float First, float Second)
    {
        auto& Curve = Table->AddRichCurve(Name);
        Curve.AddKey(1.f, First);
        Curve.AddKey(2.f, Second);
        FScalableFloat Value(1.f);
        Value.Curve.CurveTable = Table;
        Value.Curve.RowName = Name;
        return Value;
    };
    Effect->ProjectileRule.ExtraProjectiles = AddCurve(TEXT("Extra"), 2.f, 3.f);
    Effect->ProjectileRule.DamageMultiplier = AddCurve(TEXT("Damage"), .55f, .80f);
    Effect->ProjectileRule.MinimumSpread = AddCurve(TEXT("Spread"), 40.f, 90.f);
    const auto Config = Settings();
    auto Handle = ApplyEffect(F, Effect, PrismTag, 2.f);
    const auto Projectiles = Spawn(F, Config, 1.f);
    TestEqual(TEXT("Rule curves use equipment Spec level, not skill level"), Projectiles.Num(), 4);
    if (!Projectiles.IsEmpty())
    {
        TestTrue(TEXT("Level two equipment damage curve applies"),
                 FMath::IsNearlyEqual(Projectiles[0]->DamageEffectParams.BaseDamage, 80.f));
        TestTrue(TEXT("Level two equipment spread curve applies"),
                 FMath::IsNearlyEqual(Projectiles[0]->GetActorRotation().Yaw, -45.));
    }
    Handle = F.ASC->SetActiveGameplayEffectInhibit(MoveTemp(Handle), true, false);
    const auto Inhibited = Spawn(F, Config);
    TestEqual(TEXT("Inhibited equipment effect cannot alter projectile count"), Inhibited.Num(), 1);
    if (!Inhibited.IsEmpty())
    {
        TestEqual(TEXT("Inhibited equipment effect cannot scale damage"), Inhibited[0]->DamageEffectParams.BaseDamage,
                  100.f);
    }
    Handle = F.ASC->SetActiveGameplayEffectInhibit(MoveTemp(Handle), false, false);
    TestEqual(TEXT("Uninhibiting restores table-driven rule"), Spawn(F, Config).Num(), 4);
    F.ASC->RemoveActiveGameplayEffect(Handle);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUniqueProjectileIntegration, "test_25d.Equipment.Unique.FireBoltAndGraphIntegration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUniqueProjectileIntegration::RunTest(const FString&)
{
    using namespace UniqueEquipmentProjectileTests;
    FAbilityGraphTestFixture F;
    ApplyEffect(F, Prism(F), PrismTag);
    const auto Handle = F.ASC->GiveAbility(FGameplayAbilitySpec(UAbilityTraditionalFireBoltTest::StaticClass(), 1));
    TestTrue(TEXT("Original FireBolt frontend activates"), F.ASC->TryActivateAbility(Handle));
    auto* Ability =
        Cast<UAbilityTraditionalFireBoltTest>(F.ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
    if (!TestNotNull(TEXT("FireBolt instance"), Ability))
    {
        return false;
    }
    Ability->SpawnProjectiles(F.Target->GetActorLocation(), {}, false, 0.f, F.Target);
    TArray<AMyProjectile*> Native;
    for (TActorIterator<AMyProjectile> It(F.World); It; ++It)
    {
        Native.Add(*It);
    }
    TestEqual(TEXT("Native FireBolt explicitly opts into equipment rule"), Native.Num(), 3);
    auto* Graph = NewObject<UAbilityGraphDefinition>(F.World);
    Graph->SchemaVersion = 2;
    Graph->CompatibilityProfile = TEXT("UnifiedV2");
    FAbilityGraphNode Node;
    Node.NodeId = FGuid::NewGuid();
    Node.TypeId = TEXT("Delivery.ActorProjectiles");
    auto* Config = NewObject<UAbilityActorProjectileNodeConfig>(Graph);
    Config->Settings = Settings();
    Node.Config = Config;
    Graph->EntryNodeId = Node.NodeId;
    Graph->Nodes.Add(Node);
    FAbilityNodeInput Input;
    Input.SourceToken = F.SourceToken;
    Input.Level = 1.f;
    Input.Origin = FVector::ZeroVector;
    Input.EventCenter = F.Target->GetActorLocation();
    Input.DamageParams = Ability->MakeDamageEffectParamsFromClassDefaults();
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    TestTrue(TEXT("Existing registered projectile strategy starts"), Run->StartWithInput(Graph, Input, {}, Error));
    int32 GraphCount = 0;
    for (TActorIterator<AMyProjectile> It(F.World); It; ++It)
    {
        if (!Native.Contains(*It))
        {
            ++GraphCount;
            TestTrue(TEXT("Graph applies identical scaled FireBolt damage"),
                     FMath::IsNearlyEqual(It->DamageEffectParams.BaseDamage, 42.f * .55f));
        }
    }
    TestEqual(TEXT("Graph uses same equipment count"), GraphCount, 3);
    TestEqual(TEXT("Graph asset settings remain unchanged"), Config->Settings.Count, 5);
    F.ASC->CancelAbilityHandle(Handle);
    Ability->SpawnProjectiles(F.Target->GetActorLocation(), {}, false, 0.f, F.Target);
    int32 AfterCancel = 0;
    for (TActorIterator<AMyProjectile> It(F.World); It; ++It)
    {
        ++AfterCancel;
    }
    TestEqual(TEXT("Cancelled frontend cannot issue additional equipment shots"), AfterCancel,
              Native.Num() + GraphCount);
    return true;
}
#endif
