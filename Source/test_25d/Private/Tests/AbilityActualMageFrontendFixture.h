// 测试链：同一正式PlayerState ASC/玩家接口 -> 真实目录Spec/开发租约 -> 原输入和目标接口 -> 原GE/世界图。
// 临时World不启动AI/HUD/存档；安装正式伤害系数数据，保持实际GA/Graph/GE资产只读。
#pragma once
#include "AbilityGraphTestFixture.h"
#include "AbilityNativePhaseScalarTestTypes.h"
#include "DungeonPlayerRunTestTypes.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/Data/CharacterClassInfo.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeDeveloperComponent.h"
#include "AbilitySystem/Unified/AbilityGraphWorldSubsystem.h"
#include "Game/MyGameModeBase.h"
#include "Engine/GameInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "InputCoreTypes.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "MyGameplayTags.h"
#include "UObject/Script.h"

struct FAbilityActualMageFrontendFixture : FAbilityGraphTestFixture
{
    FEditorScriptExecutionGuard ScriptGuard;
    AMyPlayerState* State = nullptr;
    ADungeonPlayerRunTestPawn* Pawn = nullptr;
    ANativeRecipeRemoteInputController* Controller = nullptr;
    UMyAbilitySystemComponent* PlayerASC = nullptr;
    UAbilityRuntimeRecipeDeveloperComponent* Developer = nullptr;
    UCharacterClassInfo* DamageDefaults = nullptr;
    FGameplayAbilitySpecHandle Handle;

    /** 同一ASC用于正式基线和预览；只固定测试属性，不替换原GA/GE数值与行为定义。 */
    FAbilityActualMageFrontendFixture()
    {
        Source->Destroy();
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;
        URL.AddOption(TEXT("game=/Script/test_25d.MyGameModeBase"));
        World->SetGameMode(URL);
        DamageDefaults = LoadObject<UCharacterClassInfo>(
            nullptr, TEXT("/Game/BluePrints/AbilitySystem/Data/DA_CharacterClassInfo.DA_CharacterClassInfo"));
        if (auto* Mode = Cast<AMyGameModeBase>(World->GetAuthGameMode()))
        {
            Mode->CharacterClassInfo = DamageDefaults;
        }
        State = World->SpawnActor<AMyPlayerState>();
        State->Profession = EPlayerProfession::Mage;
        Pawn = World->SpawnActor<ADungeonPlayerRunTestPawn>();
        Pawn->Tags = {TEXT("Player")};
        Pawn->AttachState(State);
        PlayerASC = CastChecked<UMyAbilitySystemComponent>(State->GetAbilitySystemComponent());
        PlayerASC->AddAttributeSetSubobject(CastChecked<UMyAttributeSet>(State->GetAttributeSet()));
        PlayerASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 10000.f);
        PlayerASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 10000.f);
        PlayerASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 10000.f);
        PlayerASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 10000.f);
        PlayerASC->SetNumericAttributeBase(UMyAttributeSet::GetCriticalHitChanceAttribute(), 0.f);
        Controller = World->SpawnActor<ANativeRecipeRemoteInputController>();
        Controller->SetPawn(Pawn);
        PlayerASC->AbilityActorInfo->PlayerController = Controller;
        Developer = State->FindComponentByClass<UAbilityRuntimeRecipeDeveloperComponent>();
        PrepareEnemy(*Target, FVector(150, 0, 0));
    }

    /** 只收回本测试授予/租约；世界析构负责本地Actor和状态，永不写入玩家存档。 */
    ~FAbilityActualMageFrontendFixture()
    {
        FString Error;
        Developer->AbandonPreview(Error);
        PlayerASC->CancelAllAbilities();
        PlayerASC->ClearAllAbilities();
    }

    /** 原GE保持不变；将目标随机格挡/抗性/护甲归零以比较两次真实同源结算。 */
    static void PrepareEnemy(AEnemyCharacter& Enemy, FVector Position)
    {
        if (!Enemy.IsActorInitialized())
        {
            Enemy.InitializeComponents();
            Enemy.PostInitializeComponents();
        }
        Enemy.Tags = {TEXT("Enemy")};
        Enemy.SetActorLocation(Position);
        for (auto* Primitive : TInlineComponentArray<UPrimitiveComponent*>(&Enemy))
        {
            Primitive->SetCollisionEnabled(Primitive == Enemy.GetCapsuleComponent() ? ECollisionEnabled::QueryOnly
                                                                                    : ECollisionEnabled::NoCollision);
        }
        Enemy.GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
        auto* EnemyASC = Enemy.GetAbilitySystemComponent();
        EnemyASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 10000.f);
        EnemyASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 10000.f);
        EnemyASC->SetNumericAttributeBase(UMyAttributeSet::GetBlockChanceAttribute(), 0.f);
        EnemyASC->SetNumericAttributeBase(UMyAttributeSet::GetArmorAttribute(), 0.f);
        EnemyASC->SetNumericAttributeBase(UMyAttributeSet::GetMagicResistanceAttribute(), 0.f);
        EnemyASC->SetNumericAttributeBase(UMyAttributeSet::GetFireResistanceAttribute(), 0.f);
        EnemyASC->SetNumericAttributeBase(UMyAttributeSet::GetWaterResistanceAttribute(), 0.f);
        EnemyASC->SetNumericAttributeBase(UMyAttributeSet::GetThunderResistanceAttribute(), 0.f);
        EnemyASC->SetNumericAttributeBase(UMyAttributeSet::GetPhysicsResistanceAttribute(), 0.f);
    }

    /** 新增仅归本World的敌对Actor，并注册真实属性集；不启动行为树。 */
    AEnemyCharacter* EnemyAt(FVector Position)
    {
        auto* Enemy = World->SpawnActor<AEnemyCharacter>();
        auto* EnemyASC = Enemy->GetAbilitySystemComponent();
        EnemyASC->InitAbilityActorInfo(Enemy, Enemy);
        EnemyASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Enemy));
        PrepareEnemy(*Enemy, Position);
        return Enemy;
    }

    /** 精确目录路径是证据身份，不通过手工RegisterBase替换该身份的默认图。 */
    const FAbilityRuntimeDeveloperDefinition* Definition(const FString& Path, FString& Error)
    {
        if (!Developer->InitializeCatalog(nullptr, Error))
        {
            return nullptr;
        }
        return Developer->GetDefinitions().FindByPredicate(
            [&Path](const FAbilityRuntimeDeveloperDefinition& Row)
            {
                return GetPathNameSafe(Row.Base.AbilityClass.Get()) == Path;
            });
    }

    /** 输入仍走生产T入口；ServerOnly可直接在权威执行，LP病例再显式提供传输夹具。 */
    void Key(EInputEvent Event)
    {
        const auto Device =
            IPlatformInputDeviceMapper::Get().GetPrimaryInputDeviceForUser(Controller->GetPlatformUserId());
        Controller->InputKey(FInputKeyEventArgs(nullptr, Device, EKeys::T, Event, FPlatformTime::Cycles64()));
    }

    /** 同时推进原世界时钟、真实Timer、GAS任务和世界持有图，不启动AI或第二套执行器。 */
    void Step(float Seconds)
    {
        for (float Elapsed = 0.f; Elapsed + UE_SMALL_NUMBER < Seconds; Elapsed += .01f)
        {
            const float Delta = FMath::Min(.01f, Seconds - Elapsed);
            ++GFrameCounter;
            World->Tick(LEVELTICK_TimeOnly, Delta);
            World->GetTimerManager().Tick(Delta);
            PlayerASC->TickComponent(Delta, LEVELTICK_All, &PlayerASC->PrimaryComponentTick);
            World->GetSubsystem<UAbilityGraphWorldSubsystem>()->Tick(Delta);
        }
    }

    /** 原冷却真正撤去标签才允许下一施法，不直接移除GE或修改资产冷却。 */
    bool WaitReady(UMyGameplayAbility& Ability)
    {
        for (int32 Index = 0; Index < 3000; ++Index)
        {
            const auto* Tags = Ability.GetCooldownTags();
            if (!Ability.IsActive() && (!Tags || !PlayerASC->HasAnyMatchingGameplayTags(*Tags)) &&
                World->GetSubsystem<UAbilityGraphWorldSubsystem>()->GetActiveExecutionCount() == 0)
            {
                return true;
            }
            Step(.01f);
        }
        return false;
    }
};
