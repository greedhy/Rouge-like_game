/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Prototypes.CompositionFieldFinalPulseAndCatchup
 * - test_25d.Prototypes.ConfigAndCompositionBudget
 * - test_25d.Prototypes.BurstDamageTeamsAndComposition
 * - test_25d.Prototypes.FieldTrapExpiryAndCancellation
 * - test_25d.Prototypes.ProjectileSweepAndMultishot
 * - test_25d.Prototypes.BeamUpkeepAndLostTarget
 * - test_25d.Prototypes.OrbitCompositionAndTerrainAttribution
 * - test_25d.Prototypes.SkillTerrainFiltersCasterAndFriends
 * - test_25d.Prototypes.EightAssetsCatalogAndCost
 * - test_25d.Compositions.ChildSurfaceProjectsToFloor
 * - test_25d.Prototypes2.HomingTracksAndLosesTarget
 * - test_25d.Prototypes2.PiercingUniqueTargetsAndWall
 * - test_25d.Prototypes2.ChainFalloffNoRevisitAndWall
 * - test_25d.Prototypes2.UnsupportedPeriodAndMissingGroundDoNotCharge
 * - test_25d.Prototypes2.ChainExpiresAtLastHitTarget
 * - test_25d.Prototypes3.ConeAngleRangeTeamsAndWall
 * - test_25d.Prototypes3.WaveSweepsOnceAndSkipsPassedInterior
 * - test_25d.Prototypes3.ReturningTwoPassesAndMovedCaster
 * - test_25d.Prototypes3.EmitterSelectsEnemyAndRespectsCountAndCancel
 * - test_25d.Prototypes3.ReturningConsumesTurnaroundAndFinalFrameTime
 * - test_25d.Prototypes3.EmitterCatchupAndSuccessfulEmissionCount
 * - test_25d.Prototypes3.ConePresentationAndChildrenSharePlanarDirection
 * - test_25d.Prototypes4.DelayFixedOriginOnceAndCancel
 * - test_25d.Prototypes4.RingHoleBoundaryHeightAndWall
 * - test_25d.Prototypes4.LineOrientedRectangleOnceAndWall
 * - test_25d.Prototypes4.InvalidTimingAndShapeLinksRejected
 * - test_25d.Prototypes4.SavedAbilitiesDescriptionsAndSingleCost
 * - test_25d.Prototypes4.SavedCompositionsRealChildrenAndDamage
 * - test_25d.Prototypes4.LineCursorOnlySelectsDirection
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystem/Data/AbilityInfo.h"
#include "AbilitySystem/Data/CharacterClassInfo.h"
#include "Character/EnemyCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Game/MyGameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "MyGameplayTags.h"
#include "Terrain/TerrainSurface.h"
#include "Terrain/TerrainStatusComponent.h"
#include "Terrain/TerrainStatusEffects.h"
#include "Actor/MyOrbitingFireBolt.h"
#include "AbilitySystem/Data/OrbitingFireBoltConfig.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/Script.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace PrototypeTests
{
    struct FWorld
    {
        UWorld* World;
        AEnemyCharacter* Caster;
        UMyAbilitySystemComponent* ASC;
        FEditorScriptExecutionGuard Guard;
        FWorld()
        {
            World = UWorld::CreateWorld(
                EWorldType::Game, false,
                MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("PrototypeTest")));
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->SetGameInstance(NewObject<UGameInstance>(GEngine));
            FURL URL;
            URL.AddOption(TEXT("game=/Script/test_25d.MyGameModeBase"));
            World->SetGameMode(URL);
            auto* Mode = CastChecked<AMyGameModeBase>(World->GetAuthGameMode());
            Mode->CharacterClassInfo = LoadObject<UCharacterClassInfo>(
                nullptr, TEXT("/Game/BluePrints/AbilitySystem/Data/DA_CharacterClassInfo.DA_CharacterClassInfo"));
            Mode->AbilityInfo = LoadObject<UAbilityInfo>(
                nullptr, TEXT("/Game/BluePrints/AbilitySystem/Data/DA_AbilityInfo.DA_AbilityInfo"));
            Caster = Character(FVector(-500, 0, 0), TEXT("Player"));
            ASC = CastChecked<UMyAbilitySystemComponent>(Caster->GetAbilitySystemComponent());
            auto* PC = World->SpawnActor<APlayerController>();
            PC->SetPawn(Caster);
            ASC->AbilityActorInfo->PlayerController = PC;
        }
        ~FWorld()
        {
            ASC->CancelAllAbilities();
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
        AEnemyCharacter* Character(FVector Location, FName Team)
        {
            FActorSpawnParameters P;
            P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* C = World->SpawnActor<AEnemyCharacter>(Location, FRotator::ZeroRotator, P);
            C->Tags.Reset();
            C->Tags.Add(Team);
            C->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            C->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
            C->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
            auto* Component = C->GetAbilitySystemComponent();
            Component->InitAbilityActorInfo(C, C);
            Component->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(C));
            Component->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 1000);
            Component->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 1000);
            Component->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100);
            Component->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 100);
            return C;
        }
        UAbilityPrototype* Config(EAbilityPrototypeKind Kind)
        {
            auto* C = NewObject<UAbilityPrototype>();
            C->Kind = Kind;
            C->DamageType = FMyGameplayTags::Get().Damage_Magic;
            C->Cooldown = 0.f;
            C->Duration = 3.f;
            C->Radius = 200.f;
            return C;
        }
        UPrototypeCastContext* Context()
        {
            auto* C = NewObject<UPrototypeCastContext>();
            C->Caster = Caster;
            C->CastId = FGuid::NewGuid();
            C->DamageParams.SourceAbilitySystemComponent = ASC;
            C->DamageParams.DamageGameplayEffectClass = LoadClass<UGameplayEffect>(
                nullptr, TEXT("/Game/BluePrints/AbilitySystem/Player/Effect/GE_Damage.GE_Damage_C"));
            C->DamageParams.WorldContextObject = Caster;
            return C;
        }
        APrototypeRuntime* Start(UAbilityPrototype* C, UPrototypeCastContext* Context, AActor* Target = nullptr)
        {
            auto* R = APrototypeRuntime::Spawn(Context, C, FVector::ZeroVector, FVector::ForwardVector, Target);
            if (R && !R->HasActorBegunPlay())
                R->DispatchBeginPlay();
            return R;
        }
        float Health(AEnemyCharacter* C)
        {
            return C->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
        }
    };
} // namespace PrototypeTests
using namespace PrototypeTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCompositionFieldTiming, "test_25d.Prototypes.CompositionFieldFinalPulseAndCatchup",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Prototypes.CompositionFieldFinalPulseAndCatchup 回归，失败信息由下方断言交给自动化框架收集。 */
bool FCompositionFieldTiming::RunTest(const FString&)
{
    for (const TCHAR* Name : {TEXT("FireSeed_Field"), TEXT("StormSeal")})
    {
        FWorld F;
        const FString Path =
            FString::Printf(TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Compositions/DA_%s.DA_%s"), Name, Name);
        auto* C = LoadObject<UAbilityPrototype>(nullptr, *Path);
        if (!TestNotNull(TEXT("Saved field"), C))
            return false;
        auto* Enemy = F.Character(FVector(50, 0, 0), TEXT("Enemy"));
        auto* R = F.Start(C, F.Context());
        if (!TestNotNull(TEXT("Field runtime"), R))
            return false;
        R->Tick(C->Duration);
        const int32 Pulses = FMath::FloorToInt(C->Duration / C->Period);
        TestEqual(TEXT("All scheduled field pulses including final boundary"), F.Health(Enemy),
                  1000.f - Pulses * C->DamageAtLevel(1));
        const float Health = F.Health(Enemy);
        R->Tick(1.f);
        TestEqual(TEXT("Finished field cannot pulse again"), F.Health(Enemy), Health);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeBudget, "test_25d.Prototypes.ConfigAndCompositionBudget",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Prototypes.ConfigAndCompositionBudget 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPrototypeBudget::RunTest(const FString&)
{
    auto* C = NewObject<UAbilityPrototype>();
    C->DamageType = FMyGameplayTags::Get().Damage_Magic;
    C->Cooldown = 0.f;
    TestTrue(TEXT("Valid basic node"), C->IsValidConfiguration());
    C->Count = 17;
    TestFalse(TEXT("Unbounded multishot rejected"), C->IsValidConfiguration());
    C->Count = 3;
    C->Period = 0.f;
    TestFalse(TEXT("Zero period rejected"), C->IsValidConfiguration());
    C->Period = 1.f;
    C->Kind = EAbilityPrototypeKind::Surface;
    TestFalse(TEXT("Missing terrain class rejected"), C->IsValidConfiguration());
    auto* Context = NewObject<UPrototypeCastContext>();
    TestFalse(TEXT("Depth four rejected"), Context->ReserveSpawn(4));
    for (int32 Index = 0; Index < 64; ++Index)
        TestTrue(TEXT("Budget available"), Context->ReserveSpawn(3));
    TestFalse(TEXT("Sixty-fifth entity rejected"), Context->ReserveSpawn(0));
    Context->Spawned = 0;
    Context->bCancelled = true;
    TestFalse(TEXT("Cancelled cast cannot spawn"), Context->ReserveSpawn(0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeBurst, "test_25d.Prototypes.BurstDamageTeamsAndComposition",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Prototypes.BurstDamageTeamsAndComposition 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPrototypeBurst::RunTest(const FString&)
{
    FWorld F;
    auto* Enemy = F.Character(FVector(100, 0, 0), TEXT("Enemy"));
    auto* Friend = F.Character(FVector(0, 100, 0), TEXT("Player"));
    auto* Outside = F.Character(FVector(250, 0, 0), TEXT("Enemy"));
    auto* C = F.Config(EAbilityPrototypeKind::Burst);
    C->OnHit = F.Config(EAbilityPrototypeKind::Field);
    auto* Context = F.Context();
    F.Start(C, Context);
    TestEqual(TEXT("Enemy takes one full damage effect"), F.Health(Enemy), 975.f);
    TestEqual(TEXT("Friendly caster faction excluded"), F.Health(Friend), 1000.f);
    TestEqual(TEXT("Centre beyond radius excluded"), F.Health(Outside), 1000.f);
    TestEqual(TEXT("OnHit spawns a real child field sharing the cast"), Context->Spawned, 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeTimed, "test_25d.Prototypes.FieldTrapExpiryAndCancellation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Prototypes.FieldTrapExpiryAndCancellation 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPrototypeTimed::RunTest(const FString&)
{
    FWorld F;
    auto* Enemy = F.Character(FVector(100, 0, 0), TEXT("Enemy"));
    auto* C = F.Config(EAbilityPrototypeKind::Field);
    auto* Context = F.Context();
    C->OnExpire = F.Config(EAbilityPrototypeKind::Burst);
    auto* R = F.Start(C, Context);
    R->Tick(.5f);
    TestEqual(TEXT("Field waits for period"), F.Health(Enemy), 1000.f);
    R->Tick(.5f);
    TestEqual(TEXT("First periodic hit"), F.Health(Enemy), 975.f);
    R->Tick(1.f);
    TestEqual(TEXT("Second periodic hit"), F.Health(Enemy), 950.f);
    R->Tick(1.f);
    TestEqual(TEXT("Natural expiration spawns child"), Context->Spawned, 2);
    auto* CancelContext = F.Context();
    auto* Cancel = F.Start(C, CancelContext);
    Cancel->CancelCast();
    TestEqual(TEXT("Cancel never invokes OnExpire"), CancelContext->Spawned, 1);
    TestTrue(TEXT("Cancel disables all child nodes"), CancelContext->bCancelled);
    auto* Trap = F.Config(EAbilityPrototypeKind::Trap);
    auto* T = F.Start(Trap, F.Context());
    const float Before = F.Health(Enemy);
    T->Tick(.5f);
    TestEqual(TEXT("Trap arming has no damage"), F.Health(Enemy), Before);
    T->Tick(.3f);
    TestEqual(TEXT("Armed overlap triggers exactly one burst"), F.Health(Enemy), Before - 25.f);
    TestTrue(TEXT("Triggered trap destroyed"), T->IsFinished());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeProjectile, "test_25d.Prototypes.ProjectileSweepAndMultishot",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Prototypes.ProjectileSweepAndMultishot 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPrototypeProjectile::RunTest(const FString&)
{
    FWorld F;
    auto* Enemy = F.Character(FVector(400, 0, 0), TEXT("Enemy"));
    auto* Friend = F.Character(FVector(200, 0, 0), TEXT("Player"));
    // Real enemy capsules ignore Visibility; projectiles still have to hit them.
    Enemy->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
    auto* Trigger = F.World->SpawnActor<AActor>();
    auto* Volume = NewObject<UBoxComponent>(Trigger);
    Trigger->SetRootComponent(Volume);
    Volume->SetBoxExtent(FVector(1000));
    Volume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Volume->SetCollisionObjectType(ECC_WorldDynamic);
    Volume->SetCollisionResponseToAllChannels(ECR_Overlap);
    Volume->RegisterComponent();
    auto* C = F.Config(EAbilityPrototypeKind::Projectile);
    auto* R = F.Start(C, F.Context());
    R->Tick(.8f);
    TestEqual(TEXT("Sweep catches enemy across a long frame"), F.Health(Enemy), 975.f);
    TestEqual(TEXT("Friendly blocking capsule neither takes damage nor shields enemy"), F.Health(Friend), 1000.f);
    TestTrue(TEXT("Projectile consumed on impact"), R->IsFinished());
    auto* Multi = F.Config(EAbilityPrototypeKind::Multishot);
    Multi->Count = 5;
    auto* Context = F.Context();
    F.Start(Multi, Context);
    TestEqual(TEXT("Five children and one root, without recursive multishot"), Context->Spawned, 6);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeBeam, "test_25d.Prototypes.BeamUpkeepAndLostTarget",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Prototypes.BeamUpkeepAndLostTarget 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPrototypeBeam::RunTest(const FString&)
{
    FWorld F;
    auto* Enemy = F.Character(FVector(100, 0, 0), TEXT("Enemy"));
    auto* C = F.Config(EAbilityPrototypeKind::Beam);
    C->ManaPerSecond = 3;
    C->OnPeriod = F.Config(EAbilityPrototypeKind::Field);
    auto* Context = F.Context();
    auto* R = F.Start(C, Context, Enemy);
    R->Tick(1.f);
    TestEqual(TEXT("Beam created a child field"), Context->Spawned, 2);
    TestEqual(TEXT("One second upkeep"), F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 97.f);
    TestEqual(TEXT("Beam tick uses real damage"), F.Health(Enemy), 975.f);
    Enemy->SetActorLocation(FVector(2000, 0, 0));
    R->Tick(.1f);
    TestTrue(TEXT("Out of range stops beam"), R->IsFinished());
    TestTrue(TEXT("Lost target cancels existing child effects"), Context->bCancelled);
    auto* R2 = F.Start(C, F.Context(), Enemy);
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 2.f);
    R2->Tick(1.f);
    TestEqual(TEXT("Insufficient upkeep never makes mana negative"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 2.f);
    TestTrue(TEXT("Insufficient upkeep destroys channel"), R2->IsFinished());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeCompositionEdges,
                                 "test_25d.Prototypes.OrbitCompositionAndTerrainAttribution",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Prototypes.OrbitCompositionAndTerrainAttribution 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPrototypeCompositionEdges::RunTest(const FString&)
{
    FWorld F;
    auto* Enemy = F.Character(FVector(100, 0, 0), TEXT("Enemy"));
    auto* Config = F.Config(EAbilityPrototypeKind::Orbit);
    Config->OrbitSettings = LoadObject<UOrbitingFireBoltConfig>(
        nullptr,
        TEXT(
            "/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/DA_Prototype_OrbitSettings.DA_Prototype_OrbitSettings"));
    if (!TestNotNull(TEXT("Orbit settings"), Config->OrbitSettings.Get()))
        return false;
    Config->Duration = .2f;
    Config->OnHit = F.Config(EAbilityPrototypeKind::Field);
    auto* ChildContext = F.Context();
    auto* Child =
        APrototypeRuntime::Spawn(ChildContext, Config, FVector::ZeroVector, FVector::ForwardVector, nullptr, 1);
    if (!TestNotNull(TEXT("Child orbit spawned"), Child))
        return false;
    Child->DispatchBeginPlay();
    Child->Tick(.3f);
    TestTrue(TEXT("Child orbit expires without needing an input slot"), Child->IsFinished());
    auto* Context = F.Context();
    auto* Root = F.Start(Config, Context);
    Root->Tick(3.1f);
    TestEqual(TEXT("Root orbit survives duration and replenishes"), Root->GetOrbitCount(), 1);
    AMyOrbitingFireBolt* Bolt = nullptr;
    for (TActorIterator<AMyOrbitingFireBolt> It(F.World); It; ++It)
        if (IsValid(*It))
        {
            Bolt = *It;
            break;
        }
    if (!TestNotNull(TEXT("Reused firebolt"), Bolt))
        return false;
    if (!Bolt->HasActorBegunPlay())
        Bolt->DispatchBeginPlay();
    Bolt->LaunchAt(Enemy);
    Bolt->OnSphereOverlap(Bolt->Sphere, Enemy, Enemy->GetCapsuleComponent(), 0, false, FHitResult());
    TestEqual(TEXT("Orbit impact composes a field without double damage"), Context->Spawned, 2);
    TestEqual(TEXT("Orbit impact damage once"), F.Health(Enemy), 975.f);
    Root->CancelCast();
    auto* OtherCaster = F.Character(FVector(500, 0, 0), TEXT("Player"));
    Enemy->SetActorLocation(FVector(0, 0, Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
    auto MakeSurface = [&F](AActor* Owner, float Damage)
    {
        auto* S = F.World->SpawnActor<ATerrainSurface>();
        S->SetOwner(Owner);
        S->bOnlyAffectCasterEnemies = true;
        S->bRequireGrounded = false;
        S->Config = NewObject<UTerrainSurfaceConfig>(S);
        S->Config->DamagePerSecond = Damage;
        return S;
    };
    auto* First = MakeSurface(F.Caster, 5);
    auto* Second = MakeSurface(OtherCaster, 10);
    Enemy->TerrainStatus->RefreshContact(First);
    auto* ASC = Enemy->GetAbilitySystemComponent();
    auto Handles = ASC->GetActiveEffects(
        FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(TerrainTags::Burning)));
    if (!TestEqual(TEXT("Single merged burn GE"), Handles.Num(), 1))
        return false;
    TestEqual(TEXT("Initial damage caster"),
              ASC->GetActiveGameplayEffect(Handles[0])->Spec.GetContext().GetOriginalInstigator(),
              static_cast<AActor*>(F.Caster));
    Enemy->TerrainStatus->RefreshContact(Second);
    TestEqual(TEXT("Stronger ground updates caster on same effect handle"),
              ASC->GetActiveGameplayEffect(Handles[0])->Spec.GetContext().GetOriginalInstigator(),
              static_cast<AActor*>(OtherCaster));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeSurface, "test_25d.Prototypes.SkillTerrainFiltersCasterAndFriends",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Prototypes.SkillTerrainFiltersCasterAndFriends 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPrototypeSurface::RunTest(const FString&)
{
    FWorld F;
    auto* Enemy = F.Character(FVector(0, 0, 88), TEXT("Enemy"));
    auto* Friend = F.Character(FVector(0, 0, 88), TEXT("Player"));
    auto* S = F.World->SpawnActor<ATerrainSurface>();
    S->SetOwner(F.Caster);
    S->bOnlyAffectCasterEnemies = true;
    S->Config = NewObject<UTerrainSurfaceConfig>(S);
    S->bRequireGrounded = false;
    Enemy->SetActorLocation(FVector(0, 0, Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
    Friend->SetActorLocation(FVector(0, 0, Friend->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
    TestTrue(TEXT("Enemy on skill ground affected"), S->CanAffectCharacter(Enemy));
    TestFalse(TEXT("Co-op friend immune"), S->CanAffectCharacter(Friend));
    S->SetOwner(nullptr);
    TestFalse(TEXT("Orphaned skill ground disabled"), S->CanAffectCharacter(Enemy));
    S->bOnlyAffectCasterEnemies = false;
    TestTrue(TEXT("Existing world terrain behavior preserved"), S->CanAffectCharacter(Friend));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeAssets, "test_25d.Prototypes.EightAssetsCatalogAndCost",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Prototypes.EightAssetsCatalogAndCost 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPrototypeAssets::RunTest(const FString&)
{
    FWorld F;
    const TCHAR* Names[] = {TEXT("Projectile"), TEXT("Multishot"), TEXT("Burst"), TEXT("Beam"),
                            TEXT("Surface"),    TEXT("Orbit"),     TEXT("Field"), TEXT("Trap")};
    auto* Catalog = CastChecked<AMyGameModeBase>(F.World->GetAuthGameMode())->AbilityInfo.Get();
    for (const TCHAR* Name : Names)
    {
        const FString Path = FString::Printf(
            TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/GA_Prototype_%s.GA_Prototype_%s_C"), Name,
            Name);
        auto* Class = LoadClass<UPrototypeAbility>(nullptr, *Path);
        if (!TestNotNull(*Path, Class))
            continue;
        auto* GA = Class->GetDefaultObject<UPrototypeAbility>();
        TestTrue(TEXT("Data asset valid"), GA->Prototype && GA->Prototype->IsValidConfiguration());
        TestFalse(TEXT("No startup slot"), GA->StartupInputTag.IsValid());
        auto Tag = FGameplayTag::RequestGameplayTag(*FString::Printf(TEXT("Abilities.Prototype.%s"), Name));
        TestEqual(TEXT("Catalog resolves exact GA"), Catalog->FindAbilityInfoForTag(Tag).Ability.Get(), Class);
    }
    UClass* BurstClass = LoadClass<UPrototypeAbility>(
        nullptr,
        TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/GA_Prototype_Burst.GA_Prototype_Burst_C"));
    if (!BurstClass)
        return false;
    const FGameplayAbilitySpecHandle H = F.ASC->GiveAbility(FGameplayAbilitySpec(BurstClass, 1));
    auto* GA = CastChecked<UPrototypeAbility>(F.ASC->FindAbilitySpecFromHandle(H)->GetPrimaryInstance());
    GA->Prototype = F.Config(EAbilityPrototypeKind::Burst);
    GA->Prototype->bTargetSelf = true;
    GA->Prototype->ManaCost = 12;
    GA->Prototype->Cooldown = 2;
    GA->Prototype->CooldownTag = FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Prototype.Burst"));
    GA->Prototype->Radius = 321.f;
    TestTrue(TEXT("Description follows edited data rather than stale summary text"),
             GA->GetDescription(1).Contains(TEXT("321")));
    TestTrue(TEXT("Next-rank description uses computed damage"),
             GA->GetNextLevelDescription(2).Contains(TEXT("30 damage")));
    FindFProperty<FClassProperty>(UPrototypeAbility::StaticClass(), TEXT("DamageEffectClass"))
        ->SetObjectPropertyValue_InContainer(GA, F.Context()->DamageParams.DamageGameplayEffectClass);
    TestTrue(TEXT("Self cast activates"), F.ASC->TryActivateAbility(H));
    TestEqual(TEXT("Activation charges once"), F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 88.f);
    TestFalse(TEXT("Instant cast ends"), F.ASC->FindAbilitySpecFromHandle(H)->IsActive());
    TestFalse(TEXT("Cooldown rejects next cast"), F.ASC->TryActivateAbility(H));
    TestEqual(TEXT("Rejected repeat costs no mana"), F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()),
              88.f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCompositionSurfaceGround, "test_25d.Compositions.ChildSurfaceProjectsToFloor",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Compositions.ChildSurfaceProjectsToFloor 回归，失败信息由下方断言交给自动化框架收集。 */
bool FCompositionSurfaceGround::RunTest(const FString&)
{
    FWorld F;
    auto* Floor = F.World->SpawnActor<AActor>();
    auto* Box = NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(1000, 1000, 10));
    Box->SetWorldLocation(FVector(0, 0, -10));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->RegisterComponent();
    auto* Enemy = F.Character(FVector(0, 0, 90), TEXT("Enemy"));
    auto* C = F.Config(EAbilityPrototypeKind::Surface);
    C->SurfaceClass = ATerrainSurface::StaticClass();
    auto* R = APrototypeRuntime::Spawn(F.Context(), C, Enemy->GetActorLocation(), FVector::ForwardVector, Enemy, 1);
    if (!TestNotNull(TEXT("Surface child"), R))
        return false;
    TestEqual(TEXT("Child ground is projected below the target capsule"), R->GetActorLocation().Z, 2.0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeHoming, "test_25d.Prototypes2.HomingTracksAndLosesTarget",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Prototypes2.HomingTracksAndLosesTarget 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPrototypeHoming::RunTest(const FString&)
{
    FWorld F;
    auto* Enemy = F.Character(FVector(400, 0, 0), TEXT("Enemy"));
    auto* C = F.Config(static_cast<EAbilityPrototypeKind>(8));
    C->Speed = 400;
    C->Duration = 6;
    auto* R = F.Start(C, F.Context(), Enemy);
    if (!TestNotNull(TEXT("Homing runtime"), R))
        return false;
    Enemy->SetActorLocation(FVector(300, 200, 0));
    R->Tick(.2f);
    TestTrue(TEXT("Turns towards moving target"), R->GetActorLocation().Y > 5);
    for (int32 I = 0; I < 30 && !R->IsFinished(); ++I)
        R->Tick(.1f);
    TestEqual(TEXT("Tracked target hit once"), F.Health(Enemy), 975.f);
    auto* Lost = F.Start(C, F.Context(), Enemy);
    Enemy->Destroy();
    Lost->Tick(.1f);
    TestTrue(TEXT("Lost homing target ends projectile"), Lost->IsFinished());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypePiercing, "test_25d.Prototypes2.PiercingUniqueTargetsAndWall",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Prototypes2.PiercingUniqueTargetsAndWall 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPrototypePiercing::RunTest(const FString&)
{
    FWorld F;
    auto* A = F.Character(FVector(200, 0, 0), TEXT("Enemy"));
    auto* B = F.Character(FVector(400, 0, 0), TEXT("Enemy"));
    auto* C = F.Character(FVector(600, 0, 0), TEXT("Enemy"));
    auto* Config = F.Config(static_cast<EAbilityPrototypeKind>(9));
    Config->Count = 2;
    Config->Speed = 1000;
    auto* R = F.Start(Config, F.Context());
    if (!TestNotNull(TEXT("Piercing runtime"), R))
        return false;
    R->Tick(.19f);
    R->Tick(.02f);
    TestEqual(TEXT("Remaining inside same capsule does not repeat damage"), F.Health(A), 975.f);
    R->Tick(1.f);
    TestEqual(TEXT("Second target hit"), F.Health(B), 975.f);
    TestEqual(TEXT("Hit cap excludes third target"), F.Health(C), 1000.f);
    auto* Wall = F.World->SpawnActor<AActor>();
    auto* Box = NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(10, 500, 500));
    Box->SetWorldLocation(FVector(300, 0, 0));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->RegisterComponent();
    auto* Blocked = F.Start(Config, F.Context());
    Blocked->Tick(1.f);
    TestEqual(TEXT("Piercing cannot pass a wall to second target"), F.Health(B), 975.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeChain, "test_25d.Prototypes2.ChainFalloffNoRevisitAndWall",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Prototypes2.ChainFalloffNoRevisitAndWall 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPrototypeChain::RunTest(const FString&)
{
    FWorld F;
    auto* A = F.Character(FVector(100, 0, 0), TEXT("Enemy"));
    auto* B = F.Character(FVector(200, 0, 0), TEXT("Enemy"));
    auto* C = F.Character(FVector(300, 0, 0), TEXT("Enemy"));
    auto* Config = F.Config(static_cast<EAbilityPrototypeKind>(10));
    Config->Count = 3;
    Config->Radius = 150;
    Config->Period = .2f;
    if (auto* Falloff = FindFProperty<FFloatProperty>(UAbilityPrototype::StaticClass(), TEXT("DamageFalloff")))
        Falloff->SetPropertyValue_InContainer(Config, .5f);
    auto* R = F.Start(Config, F.Context(), A);
    if (!TestNotNull(TEXT("Chain runtime"), R))
        return false;
    R->Tick(.1f);
    R->Tick(.2f);
    R->Tick(.2f);
    R->Tick(.2f);
    TestEqual(TEXT("First target once"), F.Health(A), 975.f);
    TestEqual(TEXT("Second hop falloff"), F.Health(B), 987.5f);
    TestEqual(TEXT("Third hop falloff"), F.Health(C), 993.75f);
    TestTrue(TEXT("Chain bounded"), R->IsFinished());
    auto* Wall = F.World->SpawnActor<AActor>();
    auto* Box = NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(5, 500, 500));
    Box->SetWorldLocation(FVector(150, 0, 0));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->RegisterComponent();
    auto* Blocked = F.Start(Config, F.Context(), A);
    Blocked->Tick(.1f);
    Blocked->Tick(.2f);
    TestEqual(TEXT("Chain cannot select enemy through wall"), F.Health(B), 987.5f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeCompositionValidation,
                                 "test_25d.Prototypes2.UnsupportedPeriodAndMissingGroundDoNotCharge",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Prototypes2.UnsupportedPeriodAndMissingGroundDoNotCharge 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPrototypeCompositionValidation::RunTest(const FString&)
{
    FWorld F;
    for (const auto Kind :
         {EAbilityPrototypeKind::Homing, EAbilityPrototypeKind::Piercing, EAbilityPrototypeKind::Chain})
    {
        auto* Config = F.Config(Kind);
        Config->OnPeriod = F.Config(EAbilityPrototypeKind::Burst);
        TestFalse(TEXT("Unsupported flight/hop period link rejected"), Config->IsValidConfiguration());
    }
    // ServerOnly template avoids a synthetic world's missing local prediction channel.
    UClass* Class = LoadClass<UPrototypeAbility>(
        nullptr,
        TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/GA_Prototype_Burst.GA_Prototype_Burst_C"));
    if (!TestNotNull(TEXT("Authority GA template"), Class))
        return false;
    const auto Handle = F.ASC->GiveAbility(FGameplayAbilitySpec(Class, 1));
    auto* GA = CastChecked<UPrototypeAbility>(F.ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
    GA->Prototype = F.Config(EAbilityPrototypeKind::Surface);
    GA->Prototype->SurfaceClass = ATerrainSurface::StaticClass();
    GA->Prototype->bTargetSelf = true;
    GA->Prototype->ManaCost = 12;
    GA->Prototype->Cooldown = 2;
    GA->Prototype->CooldownTag = FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Prototype.Surface"));
    TestTrue(TEXT("Surface test reaches activation"), F.ASC->TryActivateAbility(Handle));
    TestEqual(TEXT("No supporting floor does not charge MP"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 100.f);
    TestFalse(TEXT("No supporting floor does not apply cooldown"),
              F.ASC->HasMatchingGameplayTag(GA->Prototype->CooldownTag));
    TestFalse(TEXT("Invalid surface cast ends"), F.ASC->FindAbilitySpecFromHandle(Handle)->IsActive());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChainExpiryLocation, "test_25d.Prototypes2.ChainExpiresAtLastHitTarget",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Prototypes2.ChainExpiresAtLastHitTarget 回归，失败信息由下方断言交给自动化框架收集。 */
bool FChainExpiryLocation::RunTest(const FString&)
{
    FWorld F;
    auto* A = F.Character(FVector(700, 0, 0), TEXT("Enemy"));
    F.Character(FVector(800, 0, 0), TEXT("Enemy"));
    auto* Config = F.Config(EAbilityPrototypeKind::Chain);
    Config->Count = 1;
    Config->Period = .2f;
    auto* Burst = F.Config(EAbilityPrototypeKind::Burst);
    Config->OnExpire = Burst;
    auto* R = F.Start(Config, F.Context(), A);
    R->Tick(.1f);
    R->Tick(.3f);
    APrototypeRuntime* Child = nullptr;
    for (TActorIterator<APrototypeRuntime> It(F.World); It; ++It)
        if (It->Prototype == Burst)
            Child = *It;
    if (!TestNotNull(TEXT("End burst spawned"), Child))
        return false;
    TestEqual(TEXT("Chain ends at last impact, not segment origin"), Child->GetActorLocation(), A->GetActorLocation());
    TestEqual(TEXT("End target is last hit, not unused next candidate"), Child->BeamEnd, A->GetActorLocation());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeCone, "test_25d.Prototypes3.ConeAngleRangeTeamsAndWall",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Prototypes3.ConeAngleRangeTeamsAndWall 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPrototypeCone::RunTest(const FString&)
{
    FWorld F;
    auto* Front = F.Character(FVector(150, 0, 0), TEXT("Enemy"));
    auto* Back = F.Character(FVector(-150, 0, 0), TEXT("Enemy"));
    auto* Side = F.Character(FVector(0, 150, 0), TEXT("Enemy"));
    auto* Friend = F.Character(FVector(120, 0, 0), TEXT("Player"));
    auto* Far = F.Character(FVector(400, 0, 0), TEXT("Enemy"));
    auto* C = F.Config(static_cast<EAbilityPrototypeKind>(11));
    C->Radius = 250;
    C->SpreadDegrees = 90;
    auto* R = F.Start(C, F.Context());
    if (!TestNotNull(TEXT("Cone runtime"), R))
        return false;
    TestEqual(TEXT("Front hit"), F.Health(Front), 975.f);
    TestEqual(TEXT("Back excluded"), F.Health(Back), 1000.f);
    TestEqual(TEXT("Side excluded"), F.Health(Side), 1000.f);
    TestEqual(TEXT("Friend excluded"), F.Health(Friend), 1000.f);
    TestEqual(TEXT("Range respected"), F.Health(Far), 1000.f);
    R->Tick(.2f);
    TestEqual(TEXT("Only once"), F.Health(Front), 975.f);
    auto* Wall = F.World->SpawnActor<AActor>();
    auto* Box = NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(5, 500, 500));
    Box->SetWorldLocation(FVector(70, 0, 0));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->RegisterComponent();
    F.Start(C, F.Context());
    TestEqual(TEXT("Wall blocks cone"), F.Health(Front), 975.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeWave, "test_25d.Prototypes3.WaveSweepsOnceAndSkipsPassedInterior",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Prototypes3.WaveSweepsOnceAndSkipsPassedInterior 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPrototypeWave::RunTest(const FString&)
{
    FWorld F;
    auto* A = F.Character(FVector(100, 0, 0), TEXT("Enemy"));
    auto* B = F.Character(FVector(300, 0, 0), TEXT("Enemy"));
    auto* C = F.Config(static_cast<EAbilityPrototypeKind>(12));
    C->Radius = 400;
    C->Speed = 200;
    auto* R = F.Start(C, F.Context());
    if (!TestNotNull(TEXT("Wave runtime"), R))
        return false;
    R->Tick(.75f);
    TestEqual(TEXT("Near target swept"), F.Health(A), 975.f);
    TestEqual(TEXT("Far waits for front"), F.Health(B), 1000.f);
    auto* Late = F.Character(FVector(70, 0, 0), TEXT("Enemy"));
    R->Tick(1.f);
    TestEqual(TEXT("Large frame does not skip outer target"), F.Health(B), 975.f);
    TestEqual(TEXT("Inner target not repeated"), F.Health(A), 975.f);
    TestEqual(TEXT("Late entrant behind wave not hit"), F.Health(Late), 1000.f);
    R->Tick(.5f);
    TestTrue(TEXT("Maximum radius finishes"), R->IsFinished());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeReturning, "test_25d.Prototypes3.ReturningTwoPassesAndMovedCaster",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Prototypes3.ReturningTwoPassesAndMovedCaster 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPrototypeReturning::RunTest(const FString&)
{
    FWorld F;
    F.Caster->SetActorLocation(FVector(-100, 0, 0));
    auto* Enemy = F.Character(FVector(200, 0, 0), TEXT("Enemy"));
    auto* C = F.Config(static_cast<EAbilityPrototypeKind>(13));
    C->Range = 400;
    C->Speed = 400;
    C->DamageFalloff = .5f;
    C->Duration = 5;
    auto* R = F.Start(C, F.Context());
    if (!TestNotNull(TEXT("Returning runtime"), R))
        return false;
    R->Tick(.6f);
    TestEqual(TEXT("Outbound once"), F.Health(Enemy), 975.f);
    R->Tick(.4f);
    R->Tick(.6f);
    TestEqual(TEXT("Return applies scaled second hit"), F.Health(Enemy), 962.5f);
    R->Tick(.1f);
    TestEqual(TEXT("No repeat while inside capsule"), F.Health(Enemy), 962.5f);
    F.Caster->SetActorLocation(FVector(-100, 100, 0));
    for (int32 I = 0; I < 30 && !R->IsFinished(); ++I)
        R->Tick(.1f);
    TestTrue(TEXT("Returns to moved caster and ends"), R->IsFinished());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeEmitter, "test_25d.Prototypes3.EmitterSelectsEnemyAndRespectsCountAndCancel",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Prototypes3.EmitterSelectsEnemyAndRespectsCountAndCancel 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPrototypeEmitter::RunTest(const FString&)
{
    FWorld F;
    auto* C = F.Config(static_cast<EAbilityPrototypeKind>(14));
    C->OnPeriod = F.Config(EAbilityPrototypeKind::Projectile);
    C->Radius = 400;
    C->Count = 2;
    C->Period = .2f;
    auto* Context = F.Context();
    auto* R = F.Start(C, Context);
    if (!TestNotNull(TEXT("Emitter runtime"), R))
        return false;
    R->Tick(2.3f);
    TestEqual(TEXT("No enemy wastes no emission"), Context->Spawned, 1);
    auto* Enemy = F.Character(FVector(0, 200, 0), TEXT("Enemy"));
    F.Character(FVector(50, 0, 0), TEXT("Player"));
    R->Tick(.15f);
    TestEqual(TEXT("First child without idle backlog"), Context->Spawned, 2);
    APrototypeRuntime* Child = nullptr;
    for (TActorIterator<APrototypeRuntime> It(F.World); It; ++It)
        if (It->Prototype == C->OnPeriod)
            Child = *It;
    if (!TestNotNull(TEXT("Real projectile child"), Child))
        return false;
    TestEqual(TEXT("Child has selected enemy target"), Child->BeamEnd, Enemy->GetActorLocation());
    TestTrue(TEXT("Child aimed toward enemy, not initial cast direction"), Child->GetActorForwardVector().Y > .9f);
    R->Tick(.3f);
    R->Tick(.3f);
    TestEqual(TEXT("Two shot cap"), Context->Spawned, 3);
    TestTrue(TEXT("Emitter completes"), R->IsFinished());
    auto* CancelContext = F.Context();
    auto* Cancel = F.Start(C, CancelContext);
    Cancel->CancelCast();
    Cancel->Tick(1.f);
    TestEqual(TEXT("Cancelled emitter never fires"), CancelContext->Spawned, 1);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReturningFrameTime,
                                 "test_25d.Prototypes3.ReturningConsumesTurnaroundAndFinalFrameTime",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Prototypes3.ReturningConsumesTurnaroundAndFinalFrameTime 回归，失败信息由下方断言交给自动化框架收集。 */
bool FReturningFrameTime::RunTest(const FString&)
{
    FWorld F;
    F.Caster->SetActorLocation(FVector(-100, 0, 0));
    auto* Enemy = F.Character(FVector(200, 0, 0), TEXT("Enemy"));
    auto* C = F.Config(EAbilityPrototypeKind::Returning);
    C->Range = 400;
    C->Speed = 400;
    C->Duration = 2.5f;
    C->DamageFalloff = .5f;
    auto* R = F.Start(C, F.Context());
    R->Tick(2.f);
    TestEqual(TEXT("Two seconds include outbound and return movement"), R->GetActorLocation().X, 0.0);
    TestEqual(TEXT("Both passes hit during a large frame"), F.Health(Enemy), 962.5f);
    R->Tick(.6f);
    TestEqual(TEXT("Final partial frame reaches caster before expiry"), R->GetActorLocation(),
              F.Caster->GetActorLocation());
    TestTrue(TEXT("Returned projectile complete"), R->IsFinished());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEmitterFrameTime, "test_25d.Prototypes3.EmitterCatchupAndSuccessfulEmissionCount",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Prototypes3.EmitterCatchupAndSuccessfulEmissionCount 回归，失败信息由下方断言交给自动化框架收集。 */
bool FEmitterFrameTime::RunTest(const FString&)
{
    FWorld F;
    F.Character(FVector(0, 200, 0), TEXT("Enemy"));
    auto* C = F.Config(EAbilityPrototypeKind::Emitter);
    C->Radius = 400;
    C->Count = 4;
    C->Period = .2f;
    C->OnPeriod = F.Config(EAbilityPrototypeKind::Projectile);
    auto* Context = F.Context();
    auto* R = F.Start(C, Context);
    R->Tick(.65f);
    TestEqual(TEXT("Three elapsed periods produce three children"), Context->Spawned, 4);
    R->Tick(.2f);
    TestEqual(TEXT("Emission cap respected during catchup"), Context->Spawned, 5);
    TestTrue(TEXT("Four emissions complete"), R->IsFinished());
    auto* Invalid = F.Config(EAbilityPrototypeKind::Emitter);
    Invalid->OnPeriod = F.Config(EAbilityPrototypeKind::Projectile);
    Invalid->OnPeriod->Speed = 0;
    Invalid->Count = 1;
    Invalid->Period = .2f;
    auto* RetryContext = F.Context();
    auto* Retry = F.Start(Invalid, RetryContext);
    Retry->Tick(.25f);
    TestFalse(TEXT("Rejected child does not consume successful shot count"), Retry->IsFinished());
    TestEqual(TEXT("No failed child counted"), RetryContext->Spawned, 1);
    Invalid->OnPeriod->Speed = 900;
    Retry->Tick(.25f);
    TestEqual(TEXT("Fixed child can still fire"), RetryContext->Spawned, 2);
    TestTrue(TEXT("Only successful emission completes"), Retry->IsFinished());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConePlanarDirection,
                                 "test_25d.Prototypes3.ConePresentationAndChildrenSharePlanarDirection",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Prototypes3.ConePresentationAndChildrenSharePlanarDirection 回归，失败信息由下方断言交给自动化框架收集。 */
bool FConePlanarDirection::RunTest(const FString&)
{
    FWorld F;
    F.Character(FVector(100, 0, 0), TEXT("Enemy"));
    auto* C = F.Config(EAbilityPrototypeKind::Cone);
    C->SpreadDegrees = 90;
    C->OnHit = F.Config(EAbilityPrototypeKind::Projectile);
    auto* R = APrototypeRuntime::Spawn(F.Context(), C, FVector::ZeroVector, FVector(1, 0, -1).GetSafeNormal(), nullptr);
    R->DispatchBeginPlay();
    TestEqual(TEXT("Cone presentation has no ground aim pitch"), R->GetActorForwardVector().Z, 0.0);
    APrototypeRuntime* Child = nullptr;
    for (TActorIterator<APrototypeRuntime> It(F.World); It; ++It)
        if (It->Prototype == C->OnHit)
            Child = *It;
    if (!TestNotNull(TEXT("Cone on-hit child"), Child))
        return false;
    TestEqual(TEXT("Cone child inherits same horizontal direction"), Child->GetActorForwardVector().Z, 0.0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeDelay, "test_25d.Prototypes4.DelayFixedOriginOnceAndCancel",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Prototypes4.DelayFixedOriginOnceAndCancel 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPrototypeDelay::RunTest(const FString&)
{
    FWorld F;
    auto* Enemy = F.Character(FVector(150, 0, 0), TEXT("Enemy"));
    auto* C = F.Config(static_cast<EAbilityPrototypeKind>(15));
    C->ArmDelay = .5f;
    C->OnExpire = F.Config(EAbilityPrototypeKind::Burst);
    auto* Context = F.Context();
    auto* R = F.Start(C, Context, Enemy);
    if (!TestNotNull(TEXT("Delay runtime"), R))
        return false;
    R->Tick(.4f);
    TestEqual(TEXT("No early child"), Context->Spawned, 1);
    TestEqual(TEXT("No direct damage"), F.Health(Enemy), 1000.f);
    F.Caster->SetActorLocation(FVector(-900, 0, 0));
    Enemy->SetActorLocation(FVector(190, 0, 0));
    R->Tick(2.f);
    TestTrue(TEXT("Crossed delay completes"), R->IsFinished());
    TestEqual(TEXT("One child"), Context->Spawned, 2);
    APrototypeRuntime* Child = nullptr;
    for (TActorIterator<APrototypeRuntime> It(F.World); It; ++It)
        if (It->Prototype == C->OnExpire)
            Child = *It;
    if (!TestNotNull(TEXT("Delay child"), Child))
        return false;
    if (!Child->HasActorBegunPlay())
        Child->DispatchBeginPlay();
    TestEqual(TEXT("Fixed origin despite caster and target motion"), Child->GetActorLocation(), FVector::ZeroVector);
    TestEqual(TEXT("Real delayed damage"), F.Health(Enemy), 975.f);
    R->Tick(2.f);
    TestEqual(TEXT("No double expiry"), Context->Spawned, 2);
    auto* CancelContext = F.Context();
    auto* Cancel = F.Start(C, CancelContext);
    Cancel->CancelCast();
    Cancel->Tick(1.f);
    TestEqual(TEXT("Cancelled delay no child"), CancelContext->Spawned, 1);
    auto* DeathContext = F.Context();
    auto* Death = F.Start(C, DeathContext);
    F.Caster->Destroy();
    Death->Tick(1.f);
    TestEqual(TEXT("Caster lost no child"), DeathContext->Spawned, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeRing, "test_25d.Prototypes4.RingHoleBoundaryHeightAndWall",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Prototypes4.RingHoleBoundaryHeightAndWall 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPrototypeRing::RunTest(const FString&)
{
    FWorld F;
    auto* Inner = F.Character(FVector(50, 0, 0), TEXT("Enemy"));
    auto* Edge = F.Character(FVector(300, 0, 0), TEXT("Enemy"));
    auto* Near = F.Character(FVector(100, 0, 0), TEXT("Enemy"));
    auto* High = F.Character(FVector(200, 0, 200), TEXT("Enemy"));
    auto* Raised = F.Character(FVector(290, 0, 90), TEXT("Enemy"));
    auto* Friend = F.Character(FVector(0, 200, 0), TEXT("Player"));
    auto* Outside = F.Character(FVector(301, 0, 0), TEXT("Enemy"));
    auto* C = F.Config(static_cast<EAbilityPrototypeKind>(16));
    C->Radius = 300;
    auto* R = F.Start(C, F.Context());
    if (!TestNotNull(TEXT("Ring runtime"), R))
        return false;
    TestEqual(TEXT("Hole safe"), F.Health(Inner), 1000.f);
    TestEqual(TEXT("Outer boundary hit"), F.Health(Edge), 975.f);
    TestEqual(TEXT("Inner boundary hit"), F.Health(Near), 975.f);
    TestEqual(TEXT("Wrong floor safe"), F.Health(High), 1000.f);
    TestEqual(TEXT("Horizontal radius independent of allowed height"), F.Health(Raised), 975.f);
    TestEqual(TEXT("Friend safe"), F.Health(Friend), 1000.f);
    TestEqual(TEXT("Outside safe"), F.Health(Outside), 1000.f);
    R->Tick(.2f);
    TestEqual(TEXT("Only one ring pulse"), F.Health(Edge), 975.f);
    auto* Wall = F.World->SpawnActor<AActor>();
    auto* Box = NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(5, 500, 500));
    Box->SetWorldLocation(FVector(70, 0, 0));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->RegisterComponent();
    F.Start(C, F.Context());
    TestEqual(TEXT("Ring blocked by wall"), F.Health(Edge), 975.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeLine, "test_25d.Prototypes4.LineOrientedRectangleOnceAndWall",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Prototypes4.LineOrientedRectangleOnceAndWall 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPrototypeLine::RunTest(const FString&)
{
    FWorld F;
    auto* End = F.Character(FVector(70, 600, 0), TEXT("Enemy"));
    auto* Side = F.Character(FVector(71, 300, 0), TEXT("Enemy"));
    auto* Back = F.Character(FVector(0, -50, 0), TEXT("Enemy"));
    auto* High = F.Character(FVector(0, 200, 200), TEXT("Enemy"));
    auto* Friend = F.Character(FVector(0, 300, 0), TEXT("Player"));
    auto* Far = F.Character(FVector(0, 601, 0), TEXT("Enemy"));
    auto* C = F.Config(static_cast<EAbilityPrototypeKind>(17));
    C->Range = 600;
    C->OnHit = F.Config(EAbilityPrototypeKind::Field);
    auto* Context = F.Context();
    auto* R = APrototypeRuntime::Spawn(Context, C, FVector::ZeroVector, FVector(0, 1, -1), nullptr);
    if (!TestNotNull(TEXT("Line runtime"), R))
        return false;
    R->DispatchBeginPlay();
    TestEqual(TEXT("Long rectangle corner hit beyond old Radius"), F.Health(End), 975.f);
    TestEqual(TEXT("Side excluded"), F.Health(Side), 1000.f);
    TestEqual(TEXT("Back excluded"), F.Health(Back), 1000.f);
    TestEqual(TEXT("High excluded"), F.Health(High), 1000.f);
    TestEqual(TEXT("Friend excluded"), F.Health(Friend), 1000.f);
    TestEqual(TEXT("Past endpoint excluded"), F.Health(Far), 1000.f);
    TestEqual(TEXT("Planar presentation"), R->GetActorForwardVector().Z, 0.0);
    TestEqual(TEXT("One hit child"), Context->Spawned, 2);
    R->Tick(.2f);
    TestEqual(TEXT("No repeat"), F.Health(End), 975.f);
    auto* Wall = F.World->SpawnActor<AActor>();
    auto* Box = NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(500, 5, 500));
    Box->SetWorldLocation(FVector(0, 100, 0));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->RegisterComponent();
    auto* Blocked = APrototypeRuntime::Spawn(F.Context(), C, FVector::ZeroVector, FVector(0, 1, 0), nullptr);
    Blocked->DispatchBeginPlay();
    TestEqual(TEXT("Line blocked by wall"), F.Health(End), 975.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeFourthValidation, "test_25d.Prototypes4.InvalidTimingAndShapeLinksRejected",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Prototypes4.InvalidTimingAndShapeLinksRejected 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPrototypeFourthValidation::RunTest(const FString&)
{
    FWorld F;
    auto* D = F.Config(static_cast<EAbilityPrototypeKind>(15));
    TestFalse(TEXT("Delay requires child"), D->IsValidConfiguration());
    D->OnExpire = F.Config(EAbilityPrototypeKind::Burst);
    TestTrue(TEXT("Valid delay"), D->IsValidConfiguration());
    D->ArmDelay = 4;
    TestFalse(TEXT("Delay cannot exceed lifetime"), D->IsValidConfiguration());
    D->ArmDelay = 0;
    TestFalse(TEXT("Zero delay rejected"), D->IsValidConfiguration());
    D->ArmDelay = .5;
    D->OnHit = D->OnExpire;
    TestFalse(TEXT("Delay hit link rejected"), D->IsValidConfiguration());
    for (int32 Kind : {16, 17})
    {
        auto* C = F.Config(static_cast<EAbilityPrototypeKind>(Kind));
        TestTrue(TEXT("Valid shape"), C->IsValidConfiguration());
        C->OnPeriod = F.Config(EAbilityPrototypeKind::Burst);
        TestFalse(TEXT("Shape period rejected"), C->IsValidConfiguration());
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeFourthAssets, "test_25d.Prototypes4.SavedAbilitiesDescriptionsAndSingleCost",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Prototypes4.SavedAbilitiesDescriptionsAndSingleCost 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPrototypeFourthAssets::RunTest(const FString&)
{
    for (const TCHAR* Name : {TEXT("Delay"), TEXT("Ring"), TEXT("Line"), TEXT("Aftershock"), TEXT("FrostFault"),
                              TEXT("RippleMine"), TEXT("HunterSentry")})
    {
        FWorld F;
        const bool bPrototype =
            FString(Name) == TEXT("Delay") || FString(Name) == TEXT("Ring") || FString(Name) == TEXT("Line");
        const FString Package = FString(TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/")) +
                                (bPrototype ? TEXT("Prototypes/GA_Prototype_") : TEXT("Compositions/GA_")) + Name;
        auto* Class = LoadClass<UPrototypeAbility>(
            nullptr, *(Package + TEXT(".") + FPackageName::GetShortName(Package) + TEXT("_C")));
        if (!TestNotNull(Name, Class))
            continue;
        auto* Defaults = Class->GetDefaultObject<UPrototypeAbility>();
        auto* C = Defaults->Prototype.Get();
        if (!TestNotNull(TEXT("Configuration"), C))
            continue;
        TestTrue(TEXT("Saved config valid"), C->IsValidConfiguration());
        TestFalse(TEXT("Not assigned by default"), Defaults->StartupInputTag.IsValid());
        TArray<FString> Lines;
        Defaults->GetDescription(2).ParseIntoArrayLines(Lines);
        TestEqual(TEXT("Exactly three description lines"), Lines.Num(), 3);
        TestTrue(TEXT("Short description"), Defaults->GetDescription(2).Len() < 240);
        if (C->bTargetSelf)
        {
            const auto Handle = F.ASC->GiveAbility(FGameplayAbilitySpec(Class, 1));
            TestTrue(TEXT("Saved self-cast GA activated"), F.ASC->TryActivateAbility(Handle));
            TestEqual(TEXT("Single root fee"), F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()),
                      100.f - C->ManaCost);
            TestFalse(TEXT("Cooldown prevents repeated cast"), F.ASC->TryActivateAbility(Handle));
            TestEqual(TEXT("No repeated fee"), F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()),
                      100.f - C->ManaCost);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeFourthCompositions,
                                 "test_25d.Prototypes4.SavedCompositionsRealChildrenAndDamage",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Prototypes4.SavedCompositionsRealChildrenAndDamage 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPrototypeFourthCompositions::RunTest(const FString&)
{
    for (const TCHAR* Name : {TEXT("Aftershock"), TEXT("FrostFault"), TEXT("RippleMine"), TEXT("HunterSentry")})
    {
        FWorld F;
        auto* Enemy = F.Character(FVector(200, 0, 0), TEXT("Enemy"));
        auto* Floor = F.World->SpawnActor<AActor>();
        auto* Box = NewObject<UBoxComponent>(Floor);
        Floor->SetRootComponent(Box);
        Box->SetBoxExtent(FVector(1000, 1000, 10));
        Box->SetWorldLocation(FVector(0, 0, -100));
        Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Box->SetCollisionObjectType(ECC_WorldStatic);
        Box->SetCollisionResponseToAllChannels(ECR_Block);
        Box->RegisterComponent();
        const FString Package = FString(TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Compositions/DA_")) + Name;
        auto* C = LoadObject<UAbilityPrototype>(nullptr, *(Package + TEXT(".DA_") + Name));
        if (!TestNotNull(Name, C))
            continue;
        auto* Context = F.Context();
        auto* Root = F.Start(C, Context, Enemy);
        if (!TestNotNull(TEXT("Composition root"), Root))
            continue;
        TSet<UAbilityPrototype*> Seen;
        for (int32 Step = 0; Step < 120; ++Step)
        {
            TArray<APrototypeRuntime*> Nodes;
            for (TActorIterator<APrototypeRuntime> It(F.World); It; ++It)
                Nodes.Add(*It);
            for (auto* Node : Nodes)
            {
                Seen.Add(Node->Prototype);
                if (!Node->HasActorBegunPlay())
                    Node->DispatchBeginPlay();
                if (!Node->IsFinished())
                    Node->Tick(.05f);
            }
        }
        const FString Skill(Name);
        const float Expected = Skill == TEXT("Aftershock")   ? 36.f
                               : Skill == TEXT("FrostFault") ? 22.f
                               : Skill == TEXT("RippleMine") ? 28.f
                                                             : 84.f;
        TestEqual(*FString::Printf(TEXT("%s real GE damage"), Name), F.Health(Enemy), 1000.f - Expected);
        TestEqual(*FString::Printf(TEXT("%s child count"), Name), Context->Spawned,
                  Skill == TEXT("HunterSentry") ? 7
                  : Skill == TEXT("FrostFault") ? 2
                                                : 3);
        TestEqual(TEXT("No child MP charge"), F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 100.f);
        TestTrue(TEXT("Composition finishes"), Root->IsFinished());
        if (Skill == TEXT("FrostFault"))
            TestTrue(TEXT("Real terrain config used"), Seen.Contains(C->OnHit));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeLineAim, "test_25d.Prototypes4.LineCursorOnlySelectsDirection",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Prototypes4.LineCursorOnlySelectsDirection 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPrototypeLineAim::RunTest(const FString&)
{
    for (const FVector Cursor : {FVector(2000, 0, 0), FVector(645, 0, 100)})
    {
        FWorld F;
        F.Caster->SetActorLocation(FVector::ZeroVector);
        auto* Enemy = F.Character(FVector(200, 0, 0), TEXT("Enemy"));
        auto* Far = F.Character(FVector(800, 0, 0), TEXT("Enemy"));
        // A distant wall along the mouse ray must not suppress near valid targets.
        auto* Wall = F.World->SpawnActor<AActor>();
        auto* Box = NewObject<UBoxComponent>(Wall);
        Wall->SetRootComponent(Box);
        Box->SetBoxExtent(FVector(5, 500, 500));
        Box->SetWorldLocation(FVector(700, 0, 0));
        Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Box->SetCollisionObjectType(ECC_WorldStatic);
        Box->SetCollisionResponseToAllChannels(ECR_Block);
        Box->RegisterComponent();
        UClass* Class = LoadClass<UPrototypeAbility>(
            nullptr,
            TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/GA_Prototype_Burst.GA_Prototype_Burst_C"));
        if (!TestNotNull(TEXT("Server-only fixture GA"), Class))
            return false;
        const auto Handle = F.ASC->GiveAbility(FGameplayAbilitySpec(Class, 1));
        auto* GA = CastChecked<UPrototypeAbility>(F.ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
        GA->Prototype = F.Config(EAbilityPrototypeKind::Line);
        GA->Prototype->bTargetSelf = false;
        GA->Prototype->Range = 650;
        GA->Prototype->ManaCost = 14;
        // This fixture has a controller but no ULocalPlayer/NetDriver, so it waits for remote data.
        if (!TestFalse(TEXT("Fixture waits for supplied target data"), F.ASC->AbilityActorInfo->IsLocallyControlled()))
            continue;
        TestTrue(TEXT("Activation reaches target-data wait"), F.ASC->TryActivateAbility(Handle));
        if (!TestTrue(TEXT("GA still waiting"), GA->IsActive()))
            continue;
        FHitResult Hit;
        Hit.bBlockingHit = true;
        Hit.ImpactPoint = Cursor;
        struct FParams
        {
            FGameplayAbilityTargetDataHandle Data;
        } Params;
        Params.Data.Add(new FGameplayAbilityTargetData_SingleTargetHit(Hit));
        GA->ProcessEvent(GA->FindFunctionChecked(TEXT("OnTargetData")), &Params);
        for (TActorIterator<APrototypeRuntime> It(F.World); It; ++It)
            if (!It->HasActorBegunPlay())
                It->DispatchBeginPlay();
        TestEqual(TEXT("Far/high cursor still casts and pays once"),
                  F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 86.f);
        TestEqual(TEXT("Near target hit along mouse direction"), F.Health(Enemy), 975.f);
        TestEqual(TEXT("Far target excluded by shape"), F.Health(Far), 1000.f);
        TestFalse(TEXT("Instant GA ends"), GA->IsActive());
    }
    return true;
}
#endif
