// 调用链：显式 UnifiedGA.VerifyPetrify -> 当前 PIE 玩家 ASC TryActivateAbility -> 原鼠标 TargetData -> 图 -> 状态采样。
// 仅开发探针：不保存资产、不替换玩家、不注入 TargetData；只清理自己创建的授予、冷却和测试目标。
#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/IConsoleManager.h"
#include "Containers/Ticker.h"
#include "AbilitySystem/Unified/UnifiedGraphAbility.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Batch6/Batch6Petrify.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystem/Data/CharacterClassInfo.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Character/EnemyCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameplayEffect.h"
#include "Interface/CombatInterface.h"
#include "HAL/FileManager.h"
#include "Mass/MonsterManagementSubsystem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MyGameplayTags.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassCombatTarget.h"
#include "Roguelike/DungeonRunDirector.h"
#include "Serialization/JsonSerializer.h"

namespace UnifiedPetrifyProbe
{
    struct FProbe;
    TSharedPtr<FProbe> Active;

    /** 持有弱世界/玩家身份与仅属于本次探针的资源；CoreTicker 和 WorldCleanup 均能触发收尾。 */
    struct FProbe : TSharedFromThis<FProbe>
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<APlayerController> Controller;
        TWeakObjectPtr<AActor> Avatar;
        TWeakObjectPtr<UAbilitySystemComponent> ASC;
        TWeakObjectPtr<AEnemyCharacter> Enemy;
        TWeakObjectPtr<UClass> SampleClass;
        TWeakObjectPtr<UMaterialInterface> StoneMaterial;
        TArray<TWeakObjectPtr<UMaterialInterface>> OriginalMaterials;
        TArray<FActiveGameplayEffectHandle> OwnedCooldowns;
        FGameplayAbilitySpecHandle Grant;
        FDelegateHandle CleanupHandle;
        FTSTicker::FDelegateHandle TickHandle;
        TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
        TSharedRef<FJsonObject> Checks = MakeShared<FJsonObject>();
        TArray<TSharedPtr<FJsonValue>> Samples;
        FString Directory;
        FVector InitialLocation;
        FVector FixtureLocation;
        float OriginalMouseX = 0.f;
        float OriginalMouseY = 0.f;
        float OriginalManaBase = 0.f;
        float InitialActorHealth = 0.f;
        int32 MassId = INDEX_NONE;
        int32 Stage = 0;
        double StartedReal = 0;
        double CastTime = 0;
        double AimTime = 0;
        bool bRestoreMana = false;
        bool bRestoreMouse = false;
        bool bFinished = false;
        bool bObservedEarly = false;
        bool bObservedPetrified = false;
        bool bCancelled = false;

        /** 每项检查有独立证据；失败由调用点统一结束，避免 cleanup 重入。 */
        bool Check(const FString& Name, bool bValue)
        {
            Checks->SetBoolField(Name, bValue);
            return bValue;
        }

        /** 每阶段写真实采样；latest 只用于找到本次唯一报告目录，不作为成功判据。 */
        void Write()
        {
            Report->SetObjectField(TEXT("checks"), Checks);
            Report->SetArrayField(TEXT("samples"), Samples);
            FString Text;
            FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Text));
            FFileHelper::SaveStringToFile(Text, *(Directory / TEXT("probe.json")),
                                          FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        }

        /** 只返回样例自身冷却，不收集其他技能或装备产生的 GE。 */
        TArray<FActiveGameplayEffectHandle> SampleCooldowns() const
        {
            TArray<FActiveGameplayEffectHandle> Result;
            auto* Component = ASC.Get();
            const auto* Defaults =
                SampleClass.IsValid() ? SampleClass->GetDefaultObject<UUnifiedGraphAbility>() : nullptr;
            if (Component && Defaults)
            {
                for (auto Handle : Component->GetActiveEffects(FGameplayEffectQuery()))
                {
                    const auto* Effect = Component->GetActiveGameplayEffect(Handle);
                    if (Effect && Effect->Spec.Def == Defaults->GetCooldownGameplayEffect())
                    {
                        Result.Add(Handle);
                    }
                }
            }
            return Result;
        }

        /** 结束时撤销本次临时授予与精确 GE 句柄，恢复鼠标/蓝量；世界退出也走同一收尾。 */
        void Finish(const FString& Error = FString())
        {
            if (bFinished)
            {
                return;
            }
            bFinished = true;
            FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
            FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle);
            if (auto* Component = ASC.Get())
            {
                if (Grant.IsValid())
                {
                    Component->CancelAbilityHandle(Grant);
                    Component->ClearAbility(Grant);
                    Check(TEXT("temporary_grant_removed"), Component->FindAbilitySpecFromHandle(Grant) == nullptr);
                }
                for (auto Handle : OwnedCooldowns)
                {
                    Component->RemoveActiveGameplayEffect(Handle);
                }
                if (bRestoreMana)
                {
                    Component->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), OriginalManaBase);
                    Check(TEXT("mana_base_restored"),
                          FMath::IsNearlyEqual(Component->GetNumericAttributeBase(UMyAttributeSet::GetManaAttribute()),
                                               OriginalManaBase));
                }
            }
            if (Enemy.IsValid())
            {
                Enemy->Destroy();
            }
            if (auto* CurrentWorld = World.Get(); CurrentWorld && MassId > 0)
            {
                if (auto* Mass = CurrentWorld->GetSubsystem<UPureMassSubsystem>())
                {
                    Mass->RemoveMonster(MassId);
                    FPureMassView View;
                    Check(TEXT("temporary_mass_removed"), !Mass->Resolve(MassId, View));
                }
            }
            if (Controller.IsValid() && bRestoreMouse)
            {
                Controller->SetMouseLocation(FMath::RoundToInt(OriginalMouseX), FMath::RoundToInt(OriginalMouseY));
            }
            bool bPassed = Error.IsEmpty();
            for (const auto& Pair : Checks->Values)
            {
                bPassed &= Pair.Value->AsBool();
            }
            Report->SetBoolField(TEXT("finished"), true);
            Report->SetBoolField(TEXT("passed"), bPassed);
            Report->SetStringField(TEXT("error"), Error);
            Report->SetStringField(TEXT("phase"), bPassed ? TEXT("complete") : TEXT("failed_or_rejected"));
            Write();
            UE_LOG(LogTemp, Display, TEXT("UnifiedGA PIE probe %s: %s; report %s"),
                   bPassed ? TEXT("PASS") : TEXT("NOT VERIFIED"), *Error, *(Directory / TEXT("probe.json")));
            if (Active.Get() == this)
            {
                Active.Reset();
            }
        }

        /** 所有运行域/数据检查先于状态修改；不自动切图、启用 Mass 或改变当前遭遇。 */
        bool Prepare(UWorld* InWorld)
        {
            World = InWorld;
            Directory = FPaths::ProjectSavedDir() / TEXT("GAAuthoring/UnifiedGA/PIE") /
                        FGuid::NewGuid().ToString(EGuidFormats::Digits);
            IFileManager::Get().MakeDirectory(*Directory, true);
            Report->SetStringField(TEXT("report"), FPaths::ConvertRelativePathToFull(Directory / TEXT("probe.json")));
            Report->SetStringField(TEXT("transport"),
                                   TEXT("current player ASC -> normal GA -> local mouse UPureMassTargetData"));
            Report->SetBoolField(TEXT("network_verified"), false);
            Report->SetBoolField(TEXT("finished"), false);
            Report->SetBoolField(TEXT("passed"), false);
            Report->SetStringField(TEXT("phase"), TEXT("preflight"));
            FFileHelper::SaveStringToFile(FPaths::ConvertRelativePathToFull(Directory / TEXT("probe.json")),
                                          *(FPaths::ProjectSavedDir() / TEXT("GAAuthoring/UnifiedGA/pie-latest.txt")),
                                          FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            Write();
            if (!InWorld || InWorld->WorldType != EWorldType::PIE || InWorld->GetNetMode() != NM_Standalone ||
                ADungeonRunDirector::Find(InWorld))
            {
                Finish(
                    TEXT("Requires standalone PIE in an empty QA map without a DungeonRunDirector; no map changed."));
                return false;
            }
            auto* PC = InWorld->GetFirstPlayerController();
            APawn* Pawn = PC ? PC->GetPawn().Get() : nullptr;
            auto* Component = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Pawn);
            auto* Mass = InWorld->GetSubsystem<UPureMassSubsystem>();
            if (!PC || !PC->IsLocalController() || !PC->GetLocalPlayer() || !PC->GetLocalPlayer()->ViewportClient ||
                !PC->GetLocalPlayer()->ViewportClient->Viewport ||
                !PC->GetMousePosition(OriginalMouseX, OriginalMouseY) || !Component ||
                !Component->AbilityActorInfo.IsValid() || Component->GetAvatarActor() != Pawn ||
                !Component->AbilityActorInfo->IsLocallyControlled() || !Mass || !Mass->IsEnabled())
            {
                Finish(TEXT(
                    "Requires current locally controlled player ASC, real mouse viewport, and already-enabled Mass."));
                return false;
            }
            Controller = PC;
            Avatar = Pawn;
            ASC = Component;
            InitialLocation = Pawn->GetActorLocation();
            if (Mass->GetSnapshots().Num() != 0)
            {
                Finish(TEXT("QA population must be empty; existing Mass entities were not changed."));
                return false;
            }
            for (TActorIterator<AEnemyCharacter> It(InWorld); It; ++It)
            {
                if (*It != Pawn)
                {
                    Finish(TEXT("QA population must be empty; existing Actor enemies were not changed."));
                    return false;
                }
            }
            SampleClass = LoadClass<UUnifiedGraphAbility>(
                nullptr, TEXT("/Game/UnifiedAbilities/Samples/GA_DelayedPetrify.GA_DelayedPetrify_C"));
            auto* Defaults = SampleClass.IsValid() ? SampleClass->GetDefaultObject<UUnifiedGraphAbility>() : nullptr;
            FAbilityNodeRegistry Registry;
            Registry.RegisterBuiltins();
            TArray<FString> Errors;
            if (!Defaults || !Defaults->Graph || !Defaults->Graph->Validate(Registry, Errors) ||
                !Defaults->GetCostGameplayEffect() || !Defaults->GetCooldownGameplayEffect())
            {
                Finish(TEXT("Authored delayed petrify sample/graph/standard GE assets are missing or invalid."));
                return false;
            }
            bool bDelay = false;
            bool bPetrify = false;
            for (const auto& Node : Defaults->Graph->Nodes)
            {
                if (const auto* Config = Cast<UAbilityDelayNodeConfig>(Node.Config))
                {
                    bDelay = FMath::IsNearlyEqual(Config->DelaySeconds, .25f);
                }
                if (const auto* Config = Cast<UAbilityPetrifyNodeConfig>(Node.Config))
                {
                    bPetrify = FMath::IsNearlyEqual(Config->DurationSeconds, 1.f);
                    StoneMaterial = Config->StoneMaterial;
                }
            }
            if (!bDelay || !bPetrify || Defaults->Graph->Nodes.Num() != 2 || !StoneMaterial.IsValid() ||
                Component->FindAbilitySpecFromClass(SampleClass.Get()) || SampleCooldowns().Num() != 0 ||
                Component->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()) < 30.f)
            {
                Finish(TEXT(
                    "Requires unchanged 0.25s/1s sample, stone material, >=30 Mana, and no existing sample grant/cooldown."));
                return false;
            }
            auto* ClassInfo = UMyAbilitySystemLibrary::GetCharacterClassInfo(Pawn);
            const auto* Warrior =
                ClassInfo ? ClassInfo->CharacterClassInformation.Find(ECharacterClass::Warrior) : nullptr;
            if (!Warrior || !Warrior->PrimaryAttributes || !ClassInfo->SecondaryAttributes ||
                !ClassInfo->VitalAttributes)
            {
                Finish(TEXT(
                    "QA GameMode must provide complete native enemy attribute defaults before safe fixture spawn."));
                return false;
            }
            bool bVisibleLocation = false;
            for (int32 Index = 0; Index < 8; ++Index)
            {
                const FVector Offset = FVector(400, 0, 0).RotateAngleAxis(45.f * Index, FVector::UpVector);
                const FVector Position = InitialLocation + Offset;
                FVector2D Screen;
                int32 Width = 0;
                int32 Height = 0;
                PC->GetViewportSize(Width, Height);
                if (PC->ProjectWorldLocationToScreen(Position, Screen) && Screen.X > 60 && Screen.X < Width - 60 &&
                    Screen.Y > 60 && Screen.Y < Height - 60 &&
                    PureMassCombat::GroundVisible(InWorld, Pawn, InitialLocation + FVector(0, 0, 30), Position))
                {
                    FixtureLocation = Position;
                    bVisibleLocation = true;
                    break;
                }
            }
            if (!bVisibleLocation)
            {
                Finish(TEXT("No clear on-screen fixture location within sample range; focus an empty QA play area."));
                return false;
            }
            OriginalManaBase = Component->GetNumericAttributeBase(UMyAttributeSet::GetManaAttribute());
            bRestoreMana = true;
            bRestoreMouse = true;
            Grant = Component->GiveAbility(FGameplayAbilitySpec(SampleClass.Get(), 1));
            FActorSpawnParameters Params;
            Params.ObjectFlags |= RF_Transient;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Params.bDeferConstruction = true;
            auto* Target = InWorld->SpawnActor<AEnemyCharacter>(AEnemyCharacter::StaticClass(), FixtureLocation,
                                                                FRotator::ZeroRotator, Params);
            Enemy = Target;
            if (!Target || !Grant.IsValid())
            {
                Finish(TEXT("Could not create temporary grant/Actor fixture."));
                return false;
            }
            Target->AutoPossessAI = EAutoPossessAI::Disabled;
            Target->Tags.AddUnique(TEXT("Enemy"));
            Target->Tags.AddUnique(TEXT("UnifiedGAProbe"));
            Target->FinishSpawning(FTransform(FixtureLocation));
            Target->GetCharacterMovement()->DisableMovement();
            Target->SetActorEnableCollision(false);
            // 原混合 Sweep 使用角色几何，关闭物理碰撞不会绕过目标任务；不让探针阻挡玩家。
            Target->GetAbilitySystemComponent()->CancelAllAbilities();
            Target->GetAbilitySystemComponent()->ClearAllAbilities();
            InitialActorHealth =
                Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
            if (auto* Manager = InWorld->GetSubsystem<UMonsterManagementSubsystem>())
            {
                Manager->UnregisterEnemy(Target);
            }
            if (auto* Visual = LoadClass<AEnemyCharacter>(
                    nullptr, TEXT("/Game/BluePrints/Characters/Goblin/BP_GoblinSpear.BP_GoblinSpear_C")))
            {
                auto* Mesh = Visual->GetDefaultObject<AEnemyCharacter>()->GetMesh();
                Target->GetMesh()->SetSkeletalMeshAsset(Mesh->GetSkeletalMeshAsset());
                Target->GetMesh()->SetRelativeTransform(Mesh->GetRelativeTransform());
                Target->GetMesh()->SetAnimInstanceClass(Mesh->GetAnimClass());
                for (int32 Index = 0; Index < Mesh->GetNumMaterials(); ++Index)
                {
                    Target->GetMesh()->SetMaterial(Index, Mesh->GetMaterial(Index));
                    OriginalMaterials.Add(Mesh->GetMaterial(Index));
                }
            }
            if (OriginalMaterials.IsEmpty())
            {
                Finish(TEXT("Fixture visual asset has no material slots; cannot verify actual material restoration."));
                return false;
            }
            StartedReal = FPlatformTime::Seconds();
            const TWeakPtr<FProbe> Weak = AsShared();
            CleanupHandle = FWorldDelegates::OnWorldCleanup.AddLambda(
                [Weak](UWorld* CleaningWorld, bool, bool)
                {
                    if (auto Probe = Weak.Pin(); Probe && Probe->World.Get() == CleaningWorld)
                    {
                        Probe->Finish(TEXT("PIE world ended before probe completion."));
                    }
                });
            TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
                [Weak](float)
                {
                    auto Probe = Weak.Pin();
                    return Probe && Probe->Tick();
                }));
            Report->SetStringField(TEXT("phase"), TEXT("aim_actor"));
            Report->SetStringField(TEXT("player"), Pawn->GetPathName());
            Write();
            return true;
        }

        /** 设置真实鼠标，随后反投影并走生产 Sweep；不合预期时禁止激活，避免误中其他对象。 */
        bool Aim(const FPureMassCombatTarget& Target)
        {
            FVector Location;
            FVector2D Screen;
            if (!PureMassCombat::Resolve(World.Get(), Avatar.Get(), Target, Location) ||
                !Controller->ProjectWorldLocationToScreen(Location, Screen))
            {
                return false;
            }
            Controller->SetMouseLocation(FMath::RoundToInt(Screen.X), FMath::RoundToInt(Screen.Y));
            FVector Start;
            FVector Direction;
            float Time = 1.f;
            FHitResult Wall;
            return Controller->DeprojectMousePositionToWorld(Start, Direction) &&
                   PureMassCombat::Sweep(World.Get(), Avatar.Get(), Start, Start + Direction * 100000.f, 0.f, Time,
                                         Wall) == Target;
        }

        /** 目标必须仍是原活体且未受外界伤害；缺失快照不能伪装成石化恢复。 */
        bool ReadTargetState(bool bMass, bool& bOutPetrified, float& OutHealth, FString& OutError) const
        {
            bOutPetrified = false;
            OutHealth = 0.f;
            OutError.Reset();
            if (bMass)
            {
                FPureMassView View;
                auto* Mass = World->GetSubsystem<UPureMassSubsystem>();
                if (!Mass || !Mass->Resolve(MassId, View) || View.Id != MassId || View.Health <= 0.f)
                {
                    OutError =
                        TEXT("Mass fixture disappeared or died before intentional cleanup; expiry is unverified.");
                    return false;
                }
                OutHealth = View.Health;
                if (!FMath::IsNearlyEqual(OutHealth, 100.f, .001f))
                {
                    OutError = TEXT("Mass fixture health changed outside the petrify probe; expiry is unverified.");
                    return false;
                }
                bOutPetrified = View.bPetrified;
                return true;
            }
            if (!Enemy.IsValid() || ICombatInterface::Execute_IsDead(Enemy.Get()))
            {
                OutError =
                    TEXT("Actor fixture disappeared or died before intentional cleanup; recovery is unverified.");
                return false;
            }
            OutHealth = Enemy->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
            if (OutHealth <= 0.f || !FMath::IsNearlyEqual(OutHealth, InitialActorHealth, .001f))
            {
                OutError = TEXT("Actor fixture health changed outside the petrify probe; recovery is unverified.");
                return false;
            }
            bOutPetrified = Enemy->GetMesh()->bPauseAnims && Enemy->GetAbilitySystemComponent()->HasMatchingGameplayTag(
                                                                 FMyGameplayTags::Get().Debuff_Stun);
            return true;
        }

        /** 正常 ASC 激活后要求一次扣蓝、一个冷却和活跃延迟；从不调用受保护的目标回调。 */
        bool ActivateSample(const FString& Prefix)
        {
            const float Before = ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute());
            const bool bActivated = ASC->TryActivateAbility(Grant);
            const auto Cooldowns = SampleCooldowns();
            for (auto Handle : Cooldowns)
            {
                OwnedCooldowns.AddUnique(Handle);
            }
            const auto* Cooldown = Cooldowns.Num() == 1 ? ASC->GetActiveGameplayEffect(Cooldowns[0]) : nullptr;
            const auto* Spec = ASC->FindAbilitySpecFromHandle(Grant);
            const bool bOkay =
                Check(Prefix + TEXT("_normal_activation"), bActivated && Spec && Spec->IsActive()) &
                Check(Prefix + TEXT("_one_10_mana_commit"),
                      FMath::IsNearlyEqual(Before - ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 10.f,
                                           .01f)) &
                Check(Prefix + TEXT("_one_cooldown"), Cooldowns.Num() == 1) &
                Check(Prefix + TEXT("_cooldown_duration_2s"),
                      Cooldown && FMath::IsNearlyEqual(Cooldown->Spec.GetDuration(), 2.f));
            CastTime = World->GetTimeSeconds();
            bObservedEarly = false;
            bObservedPetrified = false;
            bCancelled = false;
            return bOkay;
        }

        /** 按真实 PIE 时间采样 Actor、Mass 与延迟取消；超时/世界/Avatar 变化立即清理而非补造结果。 */
        bool Tick()
        {
            if (bFinished)
            {
                return false;
            }
            if (!World.IsValid() || !Controller.IsValid() || !Avatar.IsValid() || !ASC.IsValid() ||
                !ASC->AbilityActorInfo.IsValid() || ASC->GetAvatarActor() != Avatar.Get() ||
                FVector::DistSquared(Avatar->GetActorLocation(), InitialLocation) > FMath::Square(25.f) ||
                FPlatformTime::Seconds() - StartedReal > 20.0)
            {
                Finish(TEXT("Player/world changed, player moved, or probe exceeded 20 seconds; restored owned state."));
                return false;
            }
            const bool bMass = Stage >= 2;
            const FPureMassCombatTarget Target =
                bMass ? FPureMassCombatTarget(MassId) : FPureMassCombatTarget::FromActor(Enemy.Get());
            const FString Prefix = Stage < 2 ? TEXT("actor") : Stage < 4 ? TEXT("mass") : TEXT("cancel");
            if (Stage % 2 == 0)
            {
                if (!Aim(Target))
                {
                    Finish(TEXT(
                        "Actual mouse deprojection/Sweep did not select the intended temporary target; no bypass used."));
                    return false;
                }
                if (AimTime == 0)
                {
                    AimTime = World->GetTimeSeconds();
                    return true;
                }
                if (World->GetTimeSeconds() - AimTime < .1)
                {
                    return true;
                }
                AimTime = 0;
                if (!ActivateSample(Prefix))
                {
                    Finish(TEXT("Normal mouse-target GA activation failed its commit checks."));
                    return false;
                }
                ++Stage;
                Report->SetStringField(TEXT("phase"), Prefix + TEXT("_observing"));
                Write();
                return true;
            }
            const double Age = World->GetTimeSeconds() - CastTime;
            bool bStone = false;
            float TargetHealth = 0.f;
            FString StateError;
            const bool bTargetLive = ReadTargetState(bMass, bStone, TargetHealth, StateError);
            auto Sample = MakeShared<FJsonObject>();
            Sample->SetStringField(TEXT("phase"), Prefix);
            Sample->SetNumberField(TEXT("cast_age"), Age);
            Sample->SetBoolField(TEXT("target_live"), bTargetLive);
            Sample->SetNumberField(TEXT("target_health"), TargetHealth);
            Sample->SetStringField(TEXT("state_error"), StateError);
            Sample->SetBoolField(TEXT("petrified"), bStone);
            Samples.Add(MakeShared<FJsonValueObject>(Sample));
            if (!Check(Prefix + TEXT("_fixture_live_and_unchanged"), bTargetLive))
            {
                Finish(StateError);
                return false;
            }
            if (Age > 0 && Age < .20)
            {
                bObservedEarly = true;
                if (!Check(Prefix + TEXT("_not_before_delay"), !bStone))
                {
                    Finish(TEXT("Petrification occurred before the authored delay."));
                    return false;
                }
            }
            if (Stage == 5 && !bCancelled)
            {
                if (Age >= .20)
                {
                    Finish(TEXT("Frame hitch prevented observing/cancelling inside the pre-delay window."));
                    return false;
                }
                ASC->CancelAbilityHandle(Grant);
                bCancelled = true;
            }
            if (Stage == 5 && bStone)
            {
                Finish(TEXT("Cancelled delayed cast unexpectedly applied petrification."));
                return false;
            }
            if (Stage != 5 && bStone)
            {
                bObservedPetrified = true;
                if (!bMass)
                {
                    for (int32 Index = 0; Index < OriginalMaterials.Num(); ++Index)
                    {
                        if (!Check(TEXT("actor_stone_material"),
                                   Enemy->GetMesh()->GetMaterial(Index) == StoneMaterial.Get()))
                        {
                            Finish(TEXT("Actor control did not apply the configured stone material."));
                            return false;
                        }
                    }
                }
            }
            if (Age < 2.15)
            {
                return true;
            }
            const auto* Spec = ASC->FindAbilitySpecFromHandle(Grant);
            const auto* Ability = Spec ? Cast<UUnifiedGraphAbility>(Spec->GetPrimaryInstance()) : nullptr;
            const bool bRecovered = bMass ? !bStone
                                          : Enemy.IsValid() && !Enemy->GetMesh()->bPauseAnims &&
                                                !Enemy->GetAbilitySystemComponent()->HasMatchingGameplayTag(
                                                    FMyGameplayTags::Get().Debuff_Stun);
            const bool bOkay = Check(Prefix + TEXT("_observed_pre_delay"), bObservedEarly) &
                               Check(Prefix + TEXT("_expected_effect_observed"),
                                     Stage == 5 ? !bObservedPetrified : bObservedPetrified) &
                               Check(Prefix + TEXT("_state_recovered"), bRecovered) &
                               Check(Prefix + TEXT("_ga_and_tasks_finished"),
                                     Spec && !Spec->IsActive() && Ability && !Ability->HasPendingGraphWork()) &
                               Check(Prefix + TEXT("_cooldown_expired"), SampleCooldowns().IsEmpty());
            if (!bOkay)
            {
                Finish(TEXT("Timed state/expiry/cancellation or task cleanup check failed."));
                return false;
            }
            if (Stage == 1)
            {
                for (int32 Index = 0; Index < OriginalMaterials.Num(); ++Index)
                {
                    if (!Check(TEXT("actor_material_restored"),
                               Enemy->GetMesh()->GetMaterial(Index) == OriginalMaterials[Index].Get()))
                    {
                        Finish(TEXT("Actor original material was not restored."));
                        return false;
                    }
                }
                Enemy->Destroy();
                Enemy.Reset();
                // 仅新建实体使用无攻击/无移动的内存配置；共享资产完全只读。
                auto* Type = NewObject<UPureMassMonsterType>(World.Get());
                Type->Health = 100.f;
                Type->Speed = 0.f;
                Type->AttackDamage = 0.f;
                Type->AttackRange = 0.f;
                Type->XPReward = 0;
                MassId = World->GetSubsystem<UPureMassSubsystem>()->SpawnMonster(Type, FixtureLocation);
                if (MassId <= 0)
                {
                    Finish(TEXT("Could not create temporary Mass target."));
                    return false;
                }
            }
            if (Stage == 5)
            {
                Finish();
                return false;
            }
            ++Stage;
            Write();
            return true;
        }
    };

    /** 显式运行是唯一入口；第二次命令不会取消或覆盖仍在工作的探针。 */
    void Start(UWorld* World)
    {
        if (Active)
        {
            UE_LOG(LogTemp, Warning,
                   TEXT("UnifiedGA probe already running; use UnifiedGA.CancelProbe to restore its state."));
            return;
        }
        auto Probe = MakeShared<FProbe>();
        Active = Probe;
        Probe->Prepare(World);
    }

    static FAutoConsoleCommandWithWorld VerifyCommand(
        TEXT("UnifiedGA.VerifyPetrify"),
        TEXT("Explicit standalone PIE mouse-target Actor/Mass/cancel probe; empty QA map required, no assets saved."),
        FConsoleCommandWithWorldDelegate::CreateStatic(&Start));
    static FAutoConsoleCommand CancelCommand(
        TEXT("UnifiedGA.CancelProbe"),
        TEXT("Stop the active UnifiedGA PIE probe and restore only its temporary state."),
        FConsoleCommandDelegate::CreateLambda(
            []
            {
                if (auto Probe = Active)
                {
                    Probe->Finish(TEXT("Explicitly cancelled by user."));
                }
            }));
} // namespace UnifiedPetrifyProbe
#endif
