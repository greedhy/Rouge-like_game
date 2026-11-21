// 编辑器验收调用链：BloodFX.Debug 命令 → 临时 PIE 敌人/真实共享近战 → 血液报告；仅修改游戏世界。
#if WITH_EDITOR
#include "Combat/BloodFXComponent.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"
#include "AbilitySystem/Unified/Nodes/AbilityCharacterNodeExecutions.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "BrainComponent.h"
#include "AIController.h"
#include "Character/EnemyCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "PureMass/PureMassCrowd.h"
#include "PureMass/PureMassIncomingDamageEffect.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassTypes.h"
#include "Roguelike/DungeonRunDirector.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraSpriteRendererProperties.h"

namespace
{
    AEnemyCharacter* FindProbe(UWorld& World)
    {
        for (TActorIterator<AEnemyCharacter> It(&World); It; ++It)
        {
            if (It->ActorHasTag(TEXT("BloodFXProbe")))
            {
                return *It;
            }
        }
        return nullptr;
    }

    // 将测试角色放到玩家附近，血量和池设置仅作用于这次临时实例。
    AEnemyCharacter* PrepareProbe(UWorld& World, APawn& Player)
    {
        if (FindProbe(World))
        {
            return FindProbe(World);
        }
        UClass* Class = LoadClass<AEnemyCharacter>(
            nullptr, TEXT("/Game/BluePrints/Characters/Goblin/BP_GoblinSpear.BP_GoblinSpear_C"));
        if (!Class)
        {
            return nullptr;
        }
        FActorSpawnParameters Spawn;
        Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
        auto* Enemy = World.SpawnActor<AEnemyCharacter>(
            Class, Player.GetActorLocation() + Player.GetActorForwardVector() * 170.f, FRotator::ZeroRotator, Spawn);
        if (Enemy)
        {
            Enemy->Tags.AddUnique(TEXT("BloodFXProbe"));
            if (const auto* Run = ADungeonRunDirector::Find(&World))
            {
                const int32 Room = Run->GetRoomIndex(Player.GetActorLocation());
                if (Run->Rooms.IsValidIndex(Room))
                {
                    Enemy->bDungeonEncounter = true;
                    Enemy->MonsterRoomId = Run->Rooms[Room].RoomId;
                }
            }
            if (auto* PlayerASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(&Player))
            {
                PlayerASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 10000.f);
                PlayerASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 10000.f);
            }
            Enemy->BloodFX->Style.bGreenBlood = true;
            Enemy->GetCharacterMovement()->StopMovementImmediately();
            if (auto* AI = Cast<AAIController>(Enemy->GetController()))
            {
                AI->StopMovement();
                if (UBrainComponent* Brain = AI->GetBrainComponent())
                {
                    Brain->StopLogic(TEXT("BloodFX visual probe"));
                }
            }
            auto* ASC = Enemy->GetAbilitySystemComponent();
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
            Enemy->EnableDungeonPooling(.5f);
        }
        return Enemy;
    }

    // 命中使用正式共享动作，保留目标筛选、实际扣血与死亡流程。
    void StrikeProbe(APawn& Player, float Damage)
    {
        FAbilitySourceToken Source;
        FString Error;
        if (!AbilityExecutionIdentity::CaptureSource(&Player, Source, Error))
        {
            UE_LOG(LogTemp, Warning, TEXT("BloodFX probe source failed: %s"), *Error);
            return;
        }
        auto* Settings = NewObject<UAbilityCharacterStrikeNodeConfig>();
        Settings->Profile = EAbilityCharacterStrikeProfile::WeaponMixed;
        Settings->Strike.Radius = 350.f;
        Settings->Strike.Angle = 360.f;
        Settings->Strike.Damage = Damage;
        FDamageEffectParams Params;
        Params.DamageGameplayEffectClass = UPureMassIncomingDamageEffect::StaticClass();
        Params.DamageType = UPureMassIncomingDamageEffect::GetDamageTag();
        AbilityCharacterActions::ExecuteStrike(Source, Params, *Settings, Player.GetActorLocation(),
                                               Player.GetActorForwardVector(), {});
    }

    void BloodDebug(const TArray<FString>& Args, UWorld* World)
    {
        if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_Client)
        {
            return;
        }
        APlayerController* PC = GEngine->GetFirstLocalPlayerController(World);
        APawn* Player = PC ? PC->GetPawn() : nullptr;
        if (!Player)
        {
            return;
        }
        const FString Action = Args.IsEmpty() ? TEXT("Report") : Args[0];
        AEnemyCharacter* Enemy = FindProbe(*World);
        if (Action == TEXT("Assets"))
        {
            FBloodFXStyle Style;
            UNiagaraSystem* System = Style.SmallPool.LoadSynchronous();
            if (System)
            {
                for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
                {
                    if (const FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData())
                    {
                        for (const UNiagaraRendererProperties* Renderer : Data->GetRenderers())
                        {
                            if (const auto* Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer))
                            {
                                UE_LOG(LogTemp, Display,
                                       TEXT("BloodFX renderer path=%s facing=%d alignment=%d material=%s"),
                                       *Sprite->GetPathName(), static_cast<int32>(Sprite->FacingMode),
                                       static_cast<int32>(Sprite->Alignment), *GetPathNameSafe(Sprite->Material));
                            }
                        }
                    }
                }
            }
        }
        else if (Action == TEXT("Budget"))
        {
            if (auto* FX = World->GetSubsystem<UBloodFXSubsystem>())
            {
                const FBloodFXStyle Style = Enemy ? Enemy->BloodFX->Style : FBloodFXStyle();
                for (int32 Index = 0; Index < 25; ++Index)
                {
                    FX->ShowDeath(Style, Player->GetActorLocation(), Player->GetActorForwardVector(), 42.f, false);
                }
            }
        }
        else if (Action == TEXT("Prepare"))
        {
            Enemy = PrepareProbe(*World, *Player);
        }
        else if (Action == TEXT("Hit"))
        {
            StrikeProbe(*Player, Args.Num() > 1 ? FMath::Clamp(FCString::Atof(*Args[1]), 0.f, 1000.f) : 10.f);
        }
        else if (Action == TEXT("Kill"))
        {
            StrikeProbe(*Player, 1000.f);
        }
        else if (Action == TEXT("Mass"))
        {
            auto* Mass = World->GetSubsystem<UPureMassSubsystem>();
            auto* Type = LoadObject<UPureMassMonsterType>(nullptr, TEXT("/Game/PureMass/DA_MassGoblin.DA_MassGoblin"));
            if (Mass && Type)
            {
                Mass->Enable();
                if (!TActorIterator<APureMassCrowd>(World))
                {
                    World->SpawnActor<APureMassCrowd>();
                }
                Mass->SpawnMonster(Type, Player->GetActorLocation() + Player->GetActorForwardVector() * 240.f);
            }
        }
        else if (Enemy)
        {
            if (Action == TEXT("Bleed"))
            {
                Enemy->BloodFX->SetBleeding(Args.Num() < 2 || Args[1] != TEXT("0"));
            }
            else if (Action == TEXT("Red") || Action == TEXT("Green"))
            {
                Enemy->BloodFX->Style.bGreenBlood = Action == TEXT("Green");
            }
            else if (Action == TEXT("Wake"))
            {
                Enemy->SleepForDungeonPool();
                Enemy->WakeFromDungeonPool(FTransform(Enemy->GetActorLocation()), NAME_None);
            }
        }
        const auto* FX = World->GetSubsystem<UBloodFXSubsystem>();
        const float Health =
            Enemy ? Enemy->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute())
                  : -1.f;
        UE_LOG(LogTemp, Display,
               TEXT("BloodFX probe action=%s player=%s target=%s health=%.2f %s pools=%d transient=%d spawned=%d"),
               *Action, *GetNameSafe(Player->GetClass()), *GetNameSafe(Enemy), Health,
               Enemy ? *Enemy->BloodFX->GetBloodFXReport() : TEXT("no-target"), FX ? FX->GetPoolCount() : 0,
               FX ? FX->GetTransientCount() : 0, FX ? FX->GetTotalSpawned() : 0);
    }

    FAutoConsoleCommandWithWorldAndArgs BloodProbe(
        TEXT("BloodFX.Debug"),
        TEXT("PIE blood QA: Prepare, Hit [Damage], Kill, Bleed [0|1], Red, Green, Wake, Mass, Report."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BloodDebug));
} // namespace
#endif
