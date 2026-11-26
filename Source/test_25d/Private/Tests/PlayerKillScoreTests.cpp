// 验证链：真实 GAS 伤害和 PureMass 移除 → 服务端玩家计分 Lua → 读回房间和世界快照。
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystem/MyAttributeSet.h"
#include "Character/EnemyCharacter.h"
#include "DungeonPlayerRunTestTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "Misc/ScopeExit.h"
#include "PureMass/PureMassSubsystem.h"
#include "UnLuaModule.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerKillScoreCombatTest, "test_25d.Roguelike.KillScore.ActorAndMass",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 两个真实死亡通路各记一次，房间切换仅更改小计，回收、重复伤害和首领均不会增加读数。
bool FPlayerKillScoreCombatTest::RunTest(const FString&)
{
    auto& UnLua = IUnLuaModule::Get();
    const bool bWasActive = UnLua.IsActive();
    UnLua.SetActive(true);
    ON_SCOPE_EXIT { UnLua.SetActive(bWasActive); };

    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    };

    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Player = World->SpawnActor<AMyPlayerState>();
    auto* Other = World->SpawnActor<AMyPlayerState>();
    auto* Pawn = World->SpawnActor<ADungeonPlayerRunTestPawn>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
    auto* Elite = World->SpawnActor<AEnemyCharacter>(FVector(250.f, 0.f, 0.f), FRotator::ZeroRotator, Spawn);
    auto* Boss = World->SpawnActor<AEnemyCharacter>(FVector(400.f, 0.f, 0.f), FRotator::ZeroRotator, Spawn);
    Pawn->AttachState(Player);
    const FName FirstRoom(TEXT("KillScore_A"));
    const FName SecondRoom(TEXT("KillScore_B"));
    Player->SetKillScoreRoom(FirstRoom);
    Elite->LootRank = EEnemyLootRank::Elite;
    Elite->MonsterRoomId = FirstRoom;
    Boss->LootRank = EEnemyLootRank::Boss;
    Boss->MonsterRoomId = FirstRoom;

    auto* Damage = NewObject<UGameplayEffect>(World);
    auto& Modifier = Damage->Modifiers.AddDefaulted_GetRef();
    Modifier.Attribute = UMyAttributeSet::GetIncomingDamageAttribute();
    Modifier.ModifierMagnitude = FScalableFloat(100.f);
    auto* SourceASC = Player->GetAbilitySystemComponent();
    const FGameplayEffectSpec Spec(Damage, SourceASC->MakeEffectContext(), 1.f);
    auto PrepareEnemy = [](AEnemyCharacter& Enemy)
    {
        auto* ASC = Enemy.GetAbilitySystemComponent();
        ASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(&Enemy));
        ASC->InitAbilityActorInfo(&Enemy, &Enemy);
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 50.f);
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 50.f);
        return ASC;
    };
    auto* EliteASC = PrepareEnemy(*Elite);
    SourceASC->ApplyGameplayEffectSpecToTarget(Spec, EliteASC);
    SourceASC->ApplyGameplayEffectSpecToTarget(Spec, EliteASC);
    SourceASC->ApplyGameplayEffectSpecToTarget(Spec, PrepareEnemy(*Boss));
    TestTrue(TEXT("Actor damage actually defeated both enemies"),
             Elite->IsDead_Implementation() && Boss->IsDead_Implementation());

    auto* Mass = World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(World);
    Type->Health = 50.f;
    Type->XPReward = 0;
    const int32 MonsterId = Mass->SpawnMonster(Type, FVector(300.f, 0.f, 0.f), SecondRoom);
    const int32 RecycledId = Mass->SpawnMonster(Type, FVector(600.f, 0.f, 0.f), SecondRoom);
    FPureMassDamage MassDamage;
    MassDamage.Source = Pawn;
    MassDamage.Amount = 100.f;
    TestEqual(TEXT("Mass damage actually defeated a normal monster"), Mass->ApplyDamage(MonsterId, MassDamage), 50.f);
    TestEqual(TEXT("Repeated damage cannot kill Mass twice"), Mass->ApplyDamage(MonsterId, MassDamage), 0.f);
    TestTrue(TEXT("Cleanup really removed another monster"), Mass->RemoveMonster(RecycledId));

    const auto& Score = Player->GetKillScore();
    TestEqual(TEXT("Both paths published world counts"), Score.WorldNormal, 1);
    TestEqual(TEXT("Elite counted once and boss excluded"), Score.WorldElite, 1);
    TestEqual(TEXT("Mass kill belongs to the victim room"), Score.RoomNormal, 0);
    TestEqual(TEXT("First room retains its elite"), Score.RoomElite, 1);
    Player->SetKillScoreRoom(SecondRoom);
    TestEqual(TEXT("Second room shows its earlier kill"), Score.RoomNormal, 1);
    TestEqual(TEXT("Second room has no elites"), Score.RoomElite, 0);
    Player->SetKillScoreRoom(FirstRoom);
    TestEqual(TEXT("Returning restores the first room's count"), Score.RoomElite, 1);
    Other->SetKillScoreRoom(FirstRoom);
    TestTrue(TEXT("Another player has no kills"),
             Other->GetKillScore().WorldNormal == 0 && Other->GetKillScore().WorldElite == 0);
    return true;
}
#endif
