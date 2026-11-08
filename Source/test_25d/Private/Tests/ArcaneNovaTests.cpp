/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Abilities.ArcaneNova.TargetSelection
 * - test_25d.Abilities.ArcaneNova.CostCooldownAndEnd
 * - test_25d.Abilities.ArcaneNova.RealDamagePipeline
 * - test_25d.Abilities.ArcaneNova.CatalogMigration
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "AbilitySystem/Ability/MyArcaneNova.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/Data/AbilityInfo.h"
#include "AbilitySystem/Data/CharacterClassInfo.h"
#include "AbilitySystemComponent.h"
#include "Character/EnemyCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Game/MyGameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "MyGameplayTags.h"
#include "UObject/UnrealType.h"
#include "UObject/Script.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    struct FNovaTestWorld
    {
        FNovaTestWorld()
        {
            World = UWorld::CreateWorld(
                EWorldType::Game, false,
                MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("NovaTestWorld")));
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        }
        ~FNovaTestWorld()
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
        AEnemyCharacter* Spawn(const FVector& Location, FName Team)
        {
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            AEnemyCharacter* Character = World->SpawnActor<AEnemyCharacter>(Location, FRotator::ZeroRotator, Params);
            Character->Tags.Add(Team);
            Character->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Character->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
            UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
            ASC->InitAbilityActorInfo(Character, Character);
            ASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Character));
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 1000.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 1000.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 100.f);
            return Character;
        }
        UWorld* World = nullptr;
        // Test worlds deliberately skip BeginPlay/AI. Permit BlueprintNativeEvent dispatch.
        FEditorScriptExecutionGuard ScriptExecutionGuard;
    };
    UClass* LoadNovaClass()
    {
        return LoadClass<UMyArcaneNova>(
            nullptr,
            TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Arcane/ArcaneNova/GA_ArcaneNova.GA_ArcaneNova_C"));
    }
    AMyGameModeBase* AddTestGameMode(UWorld* World)
    {
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;
        URL.AddOption(TEXT("game=/Script/test_25d.MyGameModeBase"));
        World->SetGameMode(URL);
        AMyGameModeBase* Mode = Cast<AMyGameModeBase>(World->GetAuthGameMode());
        if (Mode)
        {
            Mode->CharacterClassInfo = LoadObject<UCharacterClassInfo>(
                nullptr, TEXT("/Game/BluePrints/AbilitySystem/Data/DA_CharacterClassInfo.DA_CharacterClassInfo"));
            Mode->AbilityInfo = LoadObject<UAbilityInfo>(
                nullptr, TEXT("/Game/BluePrints/AbilitySystem/Data/DA_AbilityInfo.DA_AbilityInfo"));
        }
        return Mode;
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArcaneNovaTargets, "test_25d.Abilities.ArcaneNova.TargetSelection",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Abilities.ArcaneNova.TargetSelection 回归，失败信息由下方断言交给自动化框架收集。 */
bool FArcaneNovaTargets::RunTest(const FString& Parameters)
{
    FNovaTestWorld Fixture;
    AEnemyCharacter* Source = Fixture.Spawn(FVector::ZeroVector, TEXT("Player"));
    AEnemyCharacter* Enemy = Fixture.Spawn(FVector(300, 0, 0), TEXT("Enemy"));
    AEnemyCharacter* Boundary = Fixture.Spawn(FVector(-450, 0, 0), TEXT("Enemy"));
    AEnemyCharacter* Outside = Fixture.Spawn(FVector(0, 451, 0), TEXT("Enemy"));
    AEnemyCharacter* Friend = Fixture.Spawn(FVector(0, -200, 0), TEXT("Player"));
    AEnemyCharacter* Dead = Fixture.Spawn(FVector(0, 200, 0), TEXT("Enemy"));
    FindFProperty<FBoolProperty>(ABaseCharacter::StaticClass(), TEXT("bDead"))
        ->SetPropertyValue_InContainer(Dead, true);
    TestTrue(
        TEXT("Dead fixture property"),
        FindFProperty<FBoolProperty>(ABaseCharacter::StaticClass(), TEXT("bDead"))->GetPropertyValue_InContainer(Dead));
    TestTrue(TEXT("Dead fixture native getter"), Dead->IsDead_Implementation());
    TestTrue(TEXT("Dead fixture interface getter"), ICombatInterface::Execute_IsDead(Dead));
    USphereComponent* ExtraOverlap = NewObject<USphereComponent>(Enemy);
    Enemy->AddInstanceComponent(ExtraOverlap);
    ExtraOverlap->SetupAttachment(Enemy->GetRootComponent());
    ExtraOverlap->SetSphereRadius(60.f);
    ExtraOverlap->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    ExtraOverlap->SetCollisionObjectType(ECC_WorldDynamic);
    ExtraOverlap->RegisterComponent();

    const UMyArcaneNova* Nova = GetDefault<UMyArcaneNova>();
    TArray<AActor*> Targets;
    Nova->GatherNovaTargets(Source, Targets);
    TestEqual(TEXT("Exactly two distinct enemies"), Targets.Num(), 2);
    TestTrue(TEXT("In-range enemy included"), Targets.Contains(Enemy));
    TestTrue(TEXT("Centre exactly on boundary included"), Targets.Contains(Boundary));
    TestFalse(TEXT("Overlapping capsule beyond centre range excluded"), Targets.Contains(Outside));
    TestFalse(TEXT("Friend excluded"), Targets.Contains(Friend));
    TestFalse(TEXT("Dead target excluded"), Targets.Contains(Dead));
    TestFalse(TEXT("Caster excluded"), Targets.Contains(Source));
    Nova->GatherNovaTargets(nullptr, Targets);
    TestEqual(TEXT("Invalid source clears output"), Targets.Num(), 0);
    TestEqual(TEXT("Rank 1 damage"), Nova->GetNovaDamage(1), 45.f);
    TestEqual(TEXT("Rank 3 damage"), Nova->GetNovaDamage(3), 75.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArcaneNovaCommit, "test_25d.Abilities.ArcaneNova.CostCooldownAndEnd",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Abilities.ArcaneNova.CostCooldownAndEnd 回归，失败信息由下方断言交给自动化框架收集。 */
bool FArcaneNovaCommit::RunTest(const FString& Parameters)
{
    UClass* NovaClass = LoadNovaClass();
    if (!TestNotNull(TEXT("Authored GA asset loads"), NovaClass))
        return false;
    FNovaTestWorld Fixture;
    AEnemyCharacter* Source = Fixture.Spawn(FVector::ZeroVector, TEXT("Player"));
    UAbilitySystemComponent* ASC = Source->GetAbilitySystemComponent();
    const FGameplayAbilitySpecHandle Handle = ASC->GiveAbility(FGameplayAbilitySpec(NovaClass, 1));
    const FGameplayTag Cooldown = FMyGameplayTags::Get().Cooldown_Arcane_ArcaneNova;
    TestTrue(TEXT("Empty-space cast succeeds"), ASC->TryActivateAbility(Handle));
    TestEqual(TEXT("Cost charged once"), ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 80.f);
    TestTrue(TEXT("Dedicated cooldown applied"), ASC->HasMatchingGameplayTag(Cooldown));
    TestFalse(TEXT("Instant ability ends"), ASC->FindAbilitySpecFromHandle(Handle)->IsActive());
    TestFalse(TEXT("Cooldown rejects repeat"), ASC->TryActivateAbility(Handle));
    TestEqual(TEXT("Rejected repeat costs nothing"), ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()),
              80.f);
    ASC->RemoveActiveEffectsWithGrantedTags(FGameplayTagContainer(Cooldown));
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 19.f);
    TestFalse(TEXT("Insufficient mana rejects cast"), ASC->TryActivateAbility(Handle));
    TestFalse(TEXT("Rejected cost creates no cooldown"), ASC->HasMatchingGameplayTag(Cooldown));
    TestEqual(TEXT("Rejected cost preserves mana"), ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()),
              19.f);
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 100.f);
    ASC->AddLooseGameplayTag(FMyGameplayTags::Get().Debuff_Stun);
    TestFalse(TEXT("Stunned caster rejected"), ASC->TryActivateAbility(Handle));
    ASC->RemoveLooseGameplayTag(FMyGameplayTags::Get().Debuff_Stun);
    FindFProperty<FBoolProperty>(ABaseCharacter::StaticClass(), TEXT("bDead"))
        ->SetPropertyValue_InContainer(Source, true);
    TestFalse(TEXT("Dead caster rejected"), ASC->TryActivateAbility(Handle));
    TestEqual(TEXT("Rejected state preserves mana"), ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()),
              100.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArcaneNovaDamage, "test_25d.Abilities.ArcaneNova.RealDamagePipeline",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Abilities.ArcaneNova.RealDamagePipeline 回归，失败信息由下方断言交给自动化框架收集。 */
bool FArcaneNovaDamage::RunTest(const FString& Parameters)
{
    UClass* NovaClass = LoadNovaClass();
    if (!TestNotNull(TEXT("Authored GA asset loads"), NovaClass))
        return false;
    FNovaTestWorld Fixture;
    AMyGameModeBase* Mode = AddTestGameMode(Fixture.World);
    if (!TestNotNull(TEXT("Real project game mode"), Mode) ||
        !TestNotNull(TEXT("Damage coefficient data"), Mode->CharacterClassInfo.Get()))
        return false;
    AEnemyCharacter* Source = Fixture.Spawn(FVector::ZeroVector, TEXT("Player"));
    AEnemyCharacter* Enemy = Fixture.Spawn(FVector(300, 0, 0), TEXT("Enemy"));
    AEnemyCharacter* ResistantEnemy = Fixture.Spawn(FVector(-300, 0, 0), TEXT("Enemy"));
    AEnemyCharacter* Friend = Fixture.Spawn(FVector(0, 200, 0), TEXT("Player"));
    AEnemyCharacter* Outside = Fixture.Spawn(FVector(0, 600, 0), TEXT("Enemy"));
    UAbilitySystemComponent* ASC = Source->GetAbilitySystemComponent();
    // SetPawn avoids the native enemy's AI-only PossessedBy, while supplying the
    // source character required by the existing floating-damage-text pipeline.
    APlayerController* Controller = Fixture.World->SpawnActor<APlayerController>();
    Controller->SetPawn(Source);
    ASC->AbilityActorInfo->PlayerController = Controller;
    ResistantEnemy->GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetMagicResistanceAttribute(),
                                                                         50.f);
    const auto Health = [](AEnemyCharacter* Target)
    {
        return Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
    };
    const FGameplayAbilitySpecHandle Handle = ASC->GiveAbility(FGameplayAbilitySpec(NovaClass, 1));
    TestTrue(TEXT("Real damage cast activates"), ASC->TryActivateAbility(Handle));
    TestEqual(TEXT("One full GE hits enemy"), Health(Enemy), 955.f);
    TestEqual(TEXT("Magic resistance reduces damage"), Health(ResistantEnemy), 977.5f);
    TestEqual(TEXT("Friend receives no damage"), Health(Friend), 1000.f);
    TestEqual(TEXT("Outside enemy receives no damage"), Health(Outside), 1000.f);
    TestEqual(TEXT("Caster receives no damage"), Health(Source), 1000.f);
    TestEqual(TEXT("AoE pays cost once, not per enemy"), ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()),
              80.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArcaneNovaCatalog, "test_25d.Abilities.ArcaneNova.CatalogMigration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Abilities.ArcaneNova.CatalogMigration 回归，失败信息由下方断言交给自动化框架收集。 */
bool FArcaneNovaCatalog::RunTest(const FString& Parameters)
{
    FNovaTestWorld Fixture;
    AMyGameModeBase* Mode = AddTestGameMode(Fixture.World);
    if (!TestNotNull(TEXT("Game mode"), Mode) || !TestNotNull(TEXT("Catalog"), Mode->AbilityInfo.Get()))
        return false;
    AEnemyCharacter* Source = Fixture.Spawn(FVector::ZeroVector, TEXT("Player"));
    UMyAbilitySystemComponent* ASC = CastChecked<UMyAbilitySystemComponent>(Source->GetAbilitySystemComponent());
    const FMyGameplayTags& Tags = FMyGameplayTags::Get();
    UClass* FireClass = LoadClass<UGameplayAbility>(
        nullptr, TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Fire/FireBolt/GA_FireBolt.GA_FireBolt_C"));
    if (!TestNotNull(TEXT("Existing fire ability"), FireClass))
        return false;
    FGameplayAbilitySpec Existing(FireClass, 3);
    Existing.GetDynamicSpecSourceTags().AddTag(Tags.Abilities_Status_Equipped);
    Existing.GetDynamicSpecSourceTags().AddTag(Tags.InputTag_2);
    const FGameplayAbilitySpecHandle ExistingHandle = ASC->GiveAbility(Existing);
    ASC->UpdateAbilityStatuses(1);
    const FGameplayAbilitySpec* Nova = ASC->GetSpecFromAbilityTag(Tags.Abilities_Arcane_ArcaneNova);
    if (!TestNotNull(TEXT("New catalog ability discovered for old saves"), Nova))
        return false;
    TestTrue(TEXT("New ability is eligible"),
             Nova->GetDynamicSpecSourceTags().HasTagExact(Tags.Abilities_Status_Eligible));
    const int32 Count = ASC->GetActivatableAbilities().Num();
    ASC->UpdateAbilityStatuses(1);
    TestEqual(TEXT("Repeated catalog scan grants no duplicates"), ASC->GetActivatableAbilities().Num(), Count);
    const FGameplayAbilitySpec* Restored = ASC->FindAbilitySpecFromHandle(ExistingHandle);
    TestEqual(TEXT("Existing rank preserved"), Restored->Level, 3);
    TestTrue(TEXT("Existing slot preserved"), Restored->GetDynamicSpecSourceTags().HasTagExact(Tags.InputTag_2));
    return true;
}
#endif
