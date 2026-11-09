/**
 * 调用链：自动化 -> 真实 GA_ListenForEvent 资产 -> GameplayEvent -> GE_EventBasedEffect ->
 * UMyAttributeSet::HandleIncomingXP -> 玩家接口 -> AMyPlayerState 的经验、等级、奖励与 UI 委托。
 * Actor 用例从 IncomingDamage GE 的真实死亡结算进入，Mass 用例从 ApplyDamage 的真实实体死亡进入。
 * 临时世界不 BeginPlay、不读取存档、不保存资产；仅模拟导演先存在、RunId 后初始化的实际启动顺序。
 */
#include "Misc/AutomationTest.h"

#include "DungeonPlayerRunTestTypes.h"
#include "AbilitySystem/Ability/MyAttributeEventAbility.h"
#include "AbilitySystem/Data/AbilityInfo.h"
#include "AbilitySystem/Data/CharacterClassInfo.h"
#include "AbilitySystem/Data/LevelUpInfo.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Character/EnemyCharacter.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/MyGameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CombatInterface.h"
#include "MyGameplayTags.h"
#include "PureMass/PureMassIncomingDamageEffect.h"
#include "PureMass/PureMassSubsystem.h"
#include "Roguelike/DungeonRunDirector.h"
#include "UObject/Script.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace ExperienceProgressionTests
{
    /** 复用正式玩家接口与 PlayerState，仅隔离存档、地图加载和表现资产。 */
    struct FFixture
    {
        FEditorScriptExecutionGuard ScriptExecutionGuard;
        UWorld* World = nullptr;
        AMyPlayerState* State = nullptr;
        ADungeonPlayerRunTestPawn* Pawn = nullptr;
        UAbilitySystemComponent* ASC = nullptr;
        ADungeonRunDirector* Director = nullptr;
        FGameplayAbilitySpecHandle ListenerHandle;
        FDelegateHandle EventObserver;
        FDelegateHandle XPObserver;
        FDelegateHandle LevelObserver;
        int32 ReceivedXPEvents = 0;
        int32 XPNotifications = 0;
        int32 LevelUpNotifications = 0;
        bool bReady = false;

        /** 建立有效控制器/Avatar/ASC 关系，让生产 AttributeSet 能从效果上下文找到经验接收玩家。 */
        explicit FFixture(FAutomationTestBase& Test)
        {
            World = UWorld::CreateWorld(
                EWorldType::Game, false,
                MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("ExperienceProgressionTest")));
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->SetGameInstance(NewObject<UGameInstance>(GEngine));
            FURL URL;
            URL.AddOption(TEXT("game=/Script/test_25d.MyGameModeBase"));
            World->SetGameMode(URL);
            auto* Mode = Cast<AMyGameModeBase>(World->GetAuthGameMode());
            if (!Test.TestNotNull(TEXT("Progression fixture game mode"), Mode))
            {
                return;
            }
            // 配置仅在内存中存在；升级仍执行生产玩家方法，但不额外授予目录中的战斗技能。
            Mode->AbilityInfo = NewObject<UAbilityInfo>(World);
            Mode->CharacterClassInfo = NewObject<UCharacterClassInfo>(World);
            for (const auto CharacterClass :
                 {ECharacterClass::Elementalist, ECharacterClass::Warrior, ECharacterClass::Ranger})
            {
                Mode->CharacterClassInfo->CharacterClassInformation.FindOrAdd(CharacterClass).XPReward =
                    FScalableFloat(13.f);
            }

            State = World->SpawnActor<AMyPlayerState>();
            Pawn = World->SpawnActor<ADungeonPlayerRunTestPawn>();
            auto* Controller = World->SpawnActor<APlayerController>();
            Director = World->SpawnActor<ADungeonRunDirector>();
            if (!Test.TestNotNull(TEXT("Real player state"), State) ||
                !Test.TestNotNull(TEXT("Player fixture"), Pawn) ||
                !Test.TestNotNull(TEXT("Player controller"), Controller) ||
                !Test.TestNotNull(TEXT("Director exists before run becomes ready"), Director))
            {
                return;
            }
            // 不调用 Possess，避免正式 PossessedBy 读取存档并启动生成；双向关系仍与正常控制一致。
            auto* ControllerProperty = FindFProperty<FObjectPropertyBase>(APawn::StaticClass(), TEXT("Controller"));
            if (!Test.TestNotNull(TEXT("Pawn controller property"), ControllerProperty))
            {
                return;
            }
            ControllerProperty->SetObjectPropertyValue_InContainer(Pawn, Controller);
            Controller->SetPawn(Pawn);
            Pawn->AttachState(State);
            ASC = State->GetAbilitySystemComponent();
            ASC->AddAttributeSetSubobject(CastChecked<UMyAttributeSet>(State->GetAttributeSet()));
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 100.f);

            State->LevelUpInfo = NewObject<ULevelUpInfo>(World);
            State->LevelUpInfo->LevelUpInformation.SetNum(4);
            State->LevelUpInfo->LevelUpInformation[1].LevelUpRequirement = 10;
            State->LevelUpInfo->LevelUpInformation[1].AttributePointAward = 2;
            State->LevelUpInfo->LevelUpInformation[1].SpellPointAward = 3;
            State->LevelUpInfo->LevelUpInformation[2].LevelUpRequirement = 1000;
            State->LevelUpInfo->LevelUpInformation[3].LevelUpRequirement = 2000;
            State->SetXP(0);
            State->SetLevel(1);
            State->SetAttributePoints(0);
            State->SetSpellPoints(0);
            XPObserver = State->OnXPChangedDelegate.AddLambda(
                [this](int32)
                {
                    ++XPNotifications;
                });
            LevelObserver = State->OnLevelChangedDelegate.AddLambda(
                [this](int32, bool bLevelUp)
                {
                    if (bLevelUp)
                    {
                        ++LevelUpNotifications;
                    }
                });
            EventObserver =
                ASC->GenericGameplayEventCallbacks.FindOrAdd(FMyGameplayTags::Get().Attributes_Meta_IncomingXP)
                    .AddLambda(
                        [this](const FGameplayEventData*)
                        {
                            ++ReceivedXPEvents;
                        });

            auto* ListenerClass = LoadClass<UMyAttributeEventAbility>(
                nullptr,
                TEXT(
                    "/Game/BluePrints/AbilitySystem/Player/Ability/Passive_Startup/GA_ListenForEvent.GA_ListenForEvent_C"));
            if (!Test.TestNotNull(TEXT("Authored persistent attribute listener asset"), ListenerClass))
            {
                return;
            }
            ListenerHandle = ASC->GiveAbility(FGameplayAbilitySpec(ListenerClass, 1));
            bReady = Test.TestTrue(TEXT("Real listener activates before RunId initialization"),
                                   ASC->TryActivateAbility(ListenerHandle));
        }

        /** 先解绑夹具捕获，再结束 GAS 任务；世界销毁负责其子系统、组件及实体生命周期。 */
        ~FFixture()
        {
            if (ASC)
            {
                ASC->GenericGameplayEventCallbacks.FindOrAdd(FMyGameplayTags::Get().Attributes_Meta_IncomingXP)
                    .Remove(EventObserver);
                ASC->CancelAllAbilities();
            }
            if (State)
            {
                State->OnXPChangedDelegate.Remove(XPObserver);
                State->OnLevelChangedDelegate.Remove(LevelObserver);
            }
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }

        /** 不直接写 XP；通过真实 GameplayEvent、监听器与其资产配置的 GE 发放经验。 */
        void SendXPEvent(float Amount)
        {
            FGameplayEventData Payload;
            Payload.EventTag = FMyGameplayTags::Get().Attributes_Meta_IncomingXP;
            Payload.EventMagnitude = Amount;
            UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Pawn, Payload.EventTag, Payload);
        }

        /** 同一 RunId 的对照必须先通过，排除资产缺失或夹具不完整造成的假阳性。 */
        bool EstablishControlAndInitializeRun(FAutomationTestBase& Test)
        {
            if (!bReady)
            {
                return false;
            }
            SendXPEvent(5.f);
            if (!Test.TestEqual(TEXT("Control: actual event GE awards 5 XP before run ready"), State->GetXP(), 5) ||
                !Test.TestEqual(TEXT("Control: level remains 1 below threshold"), State->GetPlayerLevel(), 1))
            {
                return false;
            }
            Test.TestEqual(TEXT("Control: UI XP notification is raised"), XPNotifications, 1);
            Test.TestFalse(TEXT("New director initially has no valid generation RunId"), Director->RunId.IsValid());
            // 对应 InitializeRun 接受 GenerationId；不伪造击杀、不修改监听器，也不重新授予技能。
            Director->RunId = FGuid::NewGuid();
            return true;
        }

        /** 断言完整成长结果和 UI 通知，同时保证常驻监听器没有被一次地图就绪变化永久关闭。 */
        void CheckAwardAfterRunInitialization(FAutomationTestBase& Test) const
        {
            Test.TestEqual(TEXT("Both pre-ready and post-ready XP events reach ASC"), ReceivedXPEvents, 2);
            Test.TestEqual(TEXT("Player receives total XP through actual listener GE"), State->GetXP(), 18);
            Test.TestEqual(TEXT("Crossing XP threshold upgrades real player state"), State->GetPlayerLevel(), 2);
            Test.TestEqual(TEXT("Level grants configured attribute points"), State->GetAttributePoints(), 2);
            Test.TestEqual(TEXT("Level grants configured spell points"), State->GetSpellPoints(), 3);
            Test.TestEqual(TEXT("UI XP delegate receives both awards"), XPNotifications, 2);
            Test.TestEqual(TEXT("UI level-up delegate fires once"), LevelUpNotifications, 1);
            const auto* ListenerSpec = ASC->FindAbilitySpecFromHandle(ListenerHandle);
            Test.TestTrue(TEXT("Infrastructure listener remains active after run initialization"),
                          ListenerSpec && ListenerSpec->IsActive());
            Test.TestEqual(TEXT("Incoming XP meta attribute is consumed"),
                           ASC->GetNumericAttribute(UMyAttributeSet::GetIncomingXPAttribute()), 0.f);
        }
    };
} // namespace ExperienceProgressionTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExperienceListenerRunBoundaryTest,
                                 "test_25d.Progression.ListenerSurvivesRunInitialization",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 首先隔离事件与 RunId 边界，避免用伤害通路的问题掩盖监听器生命周期错误。 */
bool FExperienceListenerRunBoundaryTest::RunTest(const FString&)
{
    ExperienceProgressionTests::FFixture Fixture(*this);
    FAbilitySourceToken OldActionSource;
    FString CaptureError;
    if (!Fixture.bReady ||
        !TestTrue(TEXT("Ordinary action captures strict source token before run ready"),
                  AbilityExecutionIdentity::CaptureSource(Fixture.Pawn, OldActionSource, CaptureError)))
    {
        return false;
    }
    if (!Fixture.EstablishControlAndInitializeRun(*this))
    {
        return false;
    }
    AActor* Resolved = nullptr;
    TestFalse(TEXT("Ordinary action source still rejects the previous RunId"),
              AbilityExecutionIdentity::ResolveSource(OldActionSource, Resolved));
    Fixture.SendXPEvent(13.f);
    Fixture.CheckAwardAfterRunInitialization(*this);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExperienceListenerAvatarBoundaryTest,
                                 "test_25d.Progression.ListenerRejectsReplacedAvatar",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 放宽 Run 边界不能让旧 Avatar 的任务继续控制复用 PlayerState ASC 的新 Pawn。 */
bool FExperienceListenerAvatarBoundaryTest::RunTest(const FString&)
{
    ExperienceProgressionTests::FFixture Fixture(*this);
    if (!Fixture.bReady)
    {
        return false;
    }
    Fixture.SendXPEvent(5.f);
    if (!TestEqual(TEXT("Control: real listener awards XP before Avatar change"), Fixture.State->GetXP(), 5))
    {
        return false;
    }
    auto* Replacement = Fixture.World->SpawnActor<ADungeonPlayerRunTestPawn>();
    if (!TestNotNull(TEXT("Replacement Avatar"), Replacement))
    {
        return false;
    }
    Replacement->AttachState(Fixture.State);
    FGameplayEventData Payload;
    Payload.EventTag = FMyGameplayTags::Get().Attributes_Meta_IncomingXP;
    Payload.EventMagnitude = 13.f;
    Fixture.ASC->HandleGameplayEvent(Payload.EventTag, &Payload);
    TestEqual(TEXT("Old listener must not apply XP after its Avatar was replaced"), Fixture.State->GetXP(), 5);
    const auto* Spec = Fixture.ASC->FindAbilitySpecFromHandle(Fixture.ListenerHandle);
    TestTrue(TEXT("Old listener ends and unbinds after Avatar replacement"), Spec && !Spec->IsActive());
    TestEqual(TEXT("Only original award reaches UI delegate"), Fixture.XPNotifications, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExperienceListenerDirectorLifecycleTest,
                                 "test_25d.Progression.ListenerSurvivesDirectorReplacement",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 导演的出现/消失只改变局身份，不能永久关闭仍属于同一个存活玩家的基础属性订阅。 */
bool FExperienceListenerDirectorLifecycleTest::RunTest(const FString&)
{
    ExperienceProgressionTests::FFixture Fixture(*this);
    if (!Fixture.bReady)
    {
        return false;
    }
    Fixture.SendXPEvent(5.f);
    if (!TestEqual(TEXT("Control: actual listener is functional before director changes"), Fixture.State->GetXP(), 5))
    {
        return false;
    }
    Fixture.Director->Destroy();
    TestNull(TEXT("Destroyed director is no longer the current run"), ADungeonRunDirector::Find(Fixture.World));
    Fixture.SendXPEvent(13.f);
    Fixture.CheckAwardAfterRunInitialization(*this);
    Fixture.Director = Fixture.World->SpawnActor<ADungeonRunDirector>();
    if (!TestNotNull(TEXT("New director"), Fixture.Director))
    {
        return false;
    }
    Fixture.Director->RunId = FGuid::NewGuid();
    Fixture.SendXPEvent(7.f);
    TestEqual(TEXT("Director appearing again preserves XP subscription"), Fixture.State->GetXP(), 25);
    TestEqual(TEXT("Director changes never duplicate a level reward"), Fixture.State->GetPlayerLevel(), 2);
    TestEqual(TEXT("All three legitimate awards notify the UI"), Fixture.XPNotifications, 3);
    const auto* Spec = Fixture.ASC->FindAbilitySpecFromHandle(Fixture.ListenerHandle);
    TestTrue(TEXT("Listener remains active with replacement director"), Spec && Spec->IsActive());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExperienceListenerReentrantActivationTest,
                                 "test_25d.Progression.ListenerReentrantActivationAndCancellation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** GE 同步回调结束并重启 GA 时，旧回调不得结束新会话；取消后不能留下额外经验监听器。 */
bool FExperienceListenerReentrantActivationTest::RunTest(const FString&)
{
    ExperienceProgressionTests::FFixture Fixture(*this);
    if (!Fixture.bReady)
    {
        return false;
    }
    auto* Spec = Fixture.ASC->FindAbilitySpecFromHandle(Fixture.ListenerHandle);
    auto* Ability = Spec ? Cast<UMyAttributeEventAbility>(Spec->GetPrimaryInstance()) : nullptr;
    if (!TestNotNull(TEXT("Real persistent listener instance"), Ability))
    {
        return false;
    }
    const auto OldSession = Ability->GetExecutionSession();
    bool bReentered = false;
    bool bReactivated = false;
    const FDelegateHandle Reentry = Fixture.ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
        [&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle)
        {
            if (!bReentered)
            {
                bReentered = true;
                Fixture.ASC->CancelAbilityHandle(Fixture.ListenerHandle);
                Fixture.Director->RunId = FGuid::NewGuid();
                bReactivated = Fixture.ASC->TryActivateAbility(Fixture.ListenerHandle);
            }
        });
    Fixture.SendXPEvent(5.f);
    Fixture.ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Reentry);
    TestTrue(TEXT("Actual GE application entered synchronous reactivation"), bReentered && bReactivated);
    TestTrue(TEXT("Reactivation creates a distinct source/session snapshot"),
             OldSession && Ability->GetExecutionSession() != OldSession);
    TestTrue(TEXT("Previous activation's session remains closed"), OldSession && !OldSession->IsOpen());
    TestTrue(TEXT("Old event tail preserves new active listener"), Ability->IsActive());
    TestEqual(TEXT("Reentrant activation does not duplicate current XP award"), Fixture.State->GetXP(), 5);
    Fixture.SendXPEvent(13.f);
    Fixture.CheckAwardAfterRunInitialization(*this);
    Fixture.ASC->CancelAbilityHandle(Fixture.ListenerHandle);
    Fixture.SendXPEvent(7.f);
    TestEqual(TEXT("Cancelled listener has no residual XP callback"), Fixture.State->GetXP(), 18);
    TestTrue(TEXT("Cancelled listener can explicitly activate again"),
             Fixture.ASC->TryActivateAbility(Fixture.ListenerHandle));
    Fixture.SendXPEvent(4.f);
    TestEqual(TEXT("Explicit restart awards new event exactly once"), Fixture.State->GetXP(), 22);
    TestEqual(TEXT("Only actual awards reach XP UI delegate"), Fixture.XPNotifications, 3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExperienceListenerDeathBoundaryTest,
                                 "test_25d.Progression.ListenerStopsWhenPlayerDies",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Avatar 生命周期放宽不能让死亡玩家的旧启动被动继续结算新事件。 */
bool FExperienceListenerDeathBoundaryTest::RunTest(const FString&)
{
    ExperienceProgressionTests::FFixture Fixture(*this);
    if (!Fixture.bReady)
    {
        return false;
    }
    Fixture.SendXPEvent(5.f);
    if (!TestEqual(TEXT("Control: living player receives actual XP"), Fixture.State->GetXP(), 5))
    {
        return false;
    }
    Fixture.Pawn->Die(FVector::ZeroVector);
    TestTrue(TEXT("Production dungeon death marks player dead"), ICombatInterface::Execute_IsDead(Fixture.Pawn));
    Fixture.SendXPEvent(13.f);
    TestEqual(TEXT("Dead player's old subscription cannot grant new XP"), Fixture.State->GetXP(), 5);
    const auto* Spec = Fixture.ASC->FindAbilitySpecFromHandle(Fixture.ListenerHandle);
    TestTrue(TEXT("Death ends persistent listener"), Spec && !Spec->IsActive());
    TestEqual(TEXT("Death does not publish a false XP update"), Fixture.XPNotifications, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExperienceActorKillRunBoundaryTest,
                                 "test_25d.Progression.ActorKillAwardsXPAfterRunInitialization",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 真实 Actor IncomingDamage -> Die -> SendXPEvent；不直接调用 SendXPEvent 或写入 PlayerState。 */
bool FExperienceActorKillRunBoundaryTest::RunTest(const FString&)
{
    ExperienceProgressionTests::FFixture Fixture(*this);
    if (!Fixture.EstablishControlAndInitializeRun(*this))
    {
        return false;
    }
    auto* Enemy = Fixture.World->SpawnActor<AEnemyCharacter>();
    if (!TestNotNull(TEXT("Actor enemy"), Enemy))
    {
        return false;
    }
    // 避开资源包掉落图；池内死亡仍走正式死亡、租约失效和 XP 结算。
    Enemy->EnableDungeonPooling(1.f);
    auto* EnemyASC = Enemy->GetAbilitySystemComponent();
    EnemyASC->InitAbilityActorInfo(Enemy, Enemy);
    EnemyASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Enemy));
    EnemyASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
    EnemyASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
    auto Context = Fixture.ASC->MakeEffectContext();
    Context.AddSourceObject(Fixture.Pawn);
    auto Spec = Fixture.ASC->MakeOutgoingSpec(UPureMassIncomingDamageEffect::StaticClass(), 1.f, Context);
    if (!TestTrue(TEXT("Production incoming-damage GE spec is valid"), Spec.IsValid()))
    {
        return false;
    }
    Spec.Data->SetSetByCallerMagnitude(UPureMassIncomingDamageEffect::GetDamageTag(), 100.f);
    EnemyASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
    TestTrue(TEXT("Real lethal GE marks Actor enemy dead"), ICombatInterface::Execute_IsDead(Enemy));
    Fixture.CheckAwardAfterRunInitialization(*this);
    EnemyASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
    TestEqual(TEXT("A repeated hit on the corpse sends no extra XP event"), Fixture.ReceivedXPEvents, 2);
    TestEqual(TEXT("A repeated hit on the corpse cannot award XP twice"), Fixture.State->GetXP(), 18);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExperienceMassKillRunBoundaryTest,
                                 "test_25d.Progression.MassKillAwardsXPAfterRunInitialization",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 真实 Mass ApplyDamage -> DamageFragment -> 删除实体 -> XP 事件，验证纯 Mass 模式相同的成长出口。 */
bool FExperienceMassKillRunBoundaryTest::RunTest(const FString&)
{
    ExperienceProgressionTests::FFixture Fixture(*this);
    if (!Fixture.EstablishControlAndInitializeRun(*this))
    {
        return false;
    }
    auto* Mass = Fixture.World->GetSubsystem<UPureMassSubsystem>();
    if (!TestNotNull(TEXT("Real Mass subsystem"), Mass))
    {
        return false;
    }
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(Fixture.World);
    Type->Health = 100.f;
    Type->XPReward = 13;
    const int32 Id = Mass->SpawnMonster(Type, FVector(150.f, 0.f, 0.f));
    if (!TestTrue(TEXT("Real Mass monster spawned"), Id > 0))
    {
        return false;
    }
    FPureMassDamage Damage;
    Damage.Source = Fixture.Pawn;
    Damage.Amount = 100.f;
    TestEqual(TEXT("Mass lethal damage consumes health"), Mass->ApplyDamage(Id, Damage), 100.f);
    FPureMassView View;
    TestFalse(TEXT("Killed Mass target is removed before XP callbacks"), Mass->Resolve(Id, View));
    Fixture.CheckAwardAfterRunInitialization(*this);
    TestEqual(TEXT("Repeated damage to removed Mass ID is rejected"), Mass->ApplyDamage(Id, Damage), 0.f);
    TestEqual(TEXT("A repeated Mass hit sends no extra XP event"), Fixture.ReceivedXPEvents, 2);
    TestEqual(TEXT("A repeated Mass hit cannot award XP twice"), Fixture.State->GetXP(), 18);
    return true;
}
#endif
