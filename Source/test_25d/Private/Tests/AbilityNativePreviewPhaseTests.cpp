// 测试链：真实Mage目录BeginPreview（零修正/准确租约） -> 同一ASC原TargetData/Notify/输入 -> 原费用与真实效果。
// 只为测试实例绑定可播放素材和确定性SBC伤害依赖；不RegisterBase、不注入空来源、不修改任何CDO或资产。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityGraphTestAbility.h"
#include "AbilityNativeFireBoltProfileTestTypes.h"
#include "AbilityNativePhaseScalarTestTypes.h"
#include "DungeonPlayerRunTestTypes.h"
#include "AbilitySystem/Ability/MyBeamSpell.h"
#include "AbilitySystem/Ability/MyArcaneShardAbility.h"
#include "AbilitySystem/Ability/MyOrbitingFireBoltAbility.h"
#include "AbilitySystem/Data/OrbitingFireBoltConfig.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeDeveloperComponent.h"
#include "AbilitySystem/Unified/AbilityGraphWorldSubsystem.h"
#include "AbilitySystem/Unified/Nodes/AbilityArcaneShardNode.h"
#include "Actor/MyOrbitingFireBolt.h"
#include "Actor/MyProjectile.h"
#include "Actor/PointCollection.h"
#include "Player/StanderWeaponActionConfig.h"
#include "PureMass/PureMassIncomingDamageEffect.h"
#include "PureMass/PureMassSubsystem.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "Engine/LatentActionManager.h"
#include "InputCoreTypes.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Script.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"
#include "MyGameplayTags.h"
#include "test_25d/test_25d.h"

namespace AbilityNativePreviewPhaseTests
{
    FGameplayTag InputSlot()
    {
        return FGameplayTag::RequestGameplayTag(TEXT("InputTag.DeveloperRecipe"));
    }

    /** 权威PlayerState ASC和真实玩家接口；保留原目标/动画任务，避免启动HUD/AI。 */
    struct FPlayer : FAbilityGraphTestFixture
    {
        FEditorScriptExecutionGuard Guard;
        AMyPlayerState* State = nullptr;
        ADungeonPlayerRunTestPawn* Pawn = nullptr;
        UMyAbilitySystemComponent* PlayerASC = nullptr;
        UAbilityRuntimeRecipeDeveloperComponent* Developer = nullptr;
        FGameplayAbilitySpecHandle Handle;
        FPlayer()
        {
            Source->Destroy();
            State = World->SpawnActor<AMyPlayerState>();
            State->Profession = EPlayerProfession::Mage;
            Pawn = World->SpawnActor<ADungeonPlayerRunTestPawn>();
            Pawn->Tags = {TEXT("Player")};
            Pawn->SetActorLocation(FVector::ZeroVector);
            Pawn->AttachState(State);
            PlayerASC = CastChecked<UMyAbilitySystemComponent>(State->GetAbilitySystemComponent());
            PlayerASC->AddAttributeSetSubobject(CastChecked<UMyAttributeSet>(State->GetAttributeSet()));
            PlayerASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 10000.f);
            PlayerASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 10000.f);
            PlayerASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 10000.f);
            PlayerASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 10000.f);
            auto* Controller = World->SpawnActor<AAbilityGraphRemoteTestController>();
            Controller->SetPawn(Pawn);
            PlayerASC->AbilityActorInfo->PlayerController = Controller;
            Developer = State->FindComponentByClass<UAbilityRuntimeRecipeDeveloperComponent>();
            Target->Tags = {TEXT("Enemy")};
            Target->SetActorLocation(FVector(300, 0, 0));
            if (!Target->IsActorInitialized())
            {
                Target->InitializeComponents();
                Target->PostInitializeComponents();
            }
            Target->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Target->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Projectile, ECR_Overlap);
            Target->GetCapsuleComponent()->SetGenerateOverlapEvents(true);
            Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(),
                                                                         10000.f);
            Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(),
                                                                         10000.f);
        }
        ~FPlayer()
        {
            World->SetBegunPlay(false);
            FString Error;
            Developer->AbandonPreview(Error);
            PlayerASC->CancelAllAbilities();
            PlayerASC->ClearAllAbilities();
        }
        const FAbilityRuntimeDeveloperDefinition* Definition(bool bBeam, FString& Error)
        {
            if (!Developer->InitializeCatalog(nullptr, Error))
            {
                return nullptr;
            }
            return Developer->GetDefinitions().FindByPredicate(
                [bBeam](const FAbilityRuntimeDeveloperDefinition& Row)
                {
                    return Row.Base.bSelectable && Row.Base.AbilityClass && Row.Base.DefaultGraph &&
                           Row.Base.AbilityClass->IsChildOf(bBeam ? UMyBeamSpell::StaticClass()
                                                                  : UMyOrbitingFireBoltAbility::StaticClass());
                });
        }
        UMyGameplayAbility* Grant(const FAbilityRuntimeDeveloperDefinition& Row, bool bPreview, FString& Error)
        {
            if (bPreview)
            {
                if (!Developer->BeginPreview(Row.DefinitionId, Error))
                {
                    return nullptr;
                }
                Handle = Developer->GetPreviewHandle();
            }
            else
            {
                FGameplayAbilitySpec Spec(Row.Base.AbilityClass, 1, INDEX_NONE, State);
                Spec.DynamicAbilityTags.AddTag(InputSlot());
                // 原开关输入要求正式装备状态；预览授予由BeginPreview自动加此Tag，正式对照也须真实装备。
                Spec.DynamicAbilityTags.AddTag(FMyGameplayTags::Get().Abilities_Status_Equipped);
                Handle = PlayerASC->GiveAbility(Spec);
            }
            auto* Spec = PlayerASC->FindAbilitySpecFromHandle(Handle);
            return Spec ? Cast<UMyGameplayAbility>(Spec->GetPrimaryInstance()) : nullptr;
        }
        /** 仅伤害依赖属于夹具；选中的目录图、时钟、费用和真实前端均没有被替换。 */
        bool DamageDependency(UMyGameplayAbility* Ability, UClass* EffectClass, FGameplayTag DamageTag, float Base)
        {
            auto* Effect = FindFProperty<FObjectPropertyBase>(Ability->GetClass(), TEXT("DamageEffectClass"));
            auto* Type = FindFProperty<FStructProperty>(Ability->GetClass(), TEXT("DamageType"));
            auto* Value = FindFProperty<FStructProperty>(Ability->GetClass(), TEXT("Damage"));
            auto* Chance = FindFProperty<FFloatProperty>(Ability->GetClass(), TEXT("DebuffChance"));
            if (!Effect || !Type || !Value || !Chance)
            {
                return false;
            }
            Effect->SetObjectPropertyValue_InContainer(Ability, EffectClass);
            *Type->ContainerPtrToValuePtr<FGameplayTag>(Ability) = DamageTag;
            *Value->ContainerPtrToValuePtr<FScalableFloat>(Ability) = FScalableFloat(Base);
            Chance->SetPropertyValue_InContainer(Ability, 0.f);
            return true;
        }
        UAnimMontage* MontageDependency(UMyBeamSpell* Ability)
        {
            auto* Config = LoadObject<UStanderWeaponActionConfig>(
                nullptr, TEXT("/Game/BluePrints/Characters/Stander/Weapons/Sword_Shield/DA_Attack1.DA_Attack1"));
            auto* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/PlatformAnimation/Models/Stander.Stander"));
            auto* Montage = FindFProperty<FObjectPropertyBase>(Ability->GetClass(), TEXT("ElectrocuteMontage"));
            if (!Config || !Config->Montage || !Mesh || !Montage)
            {
                return nullptr;
            }
            Pawn->GetMesh()->SetSkeletalMeshAsset(Mesh);
            Pawn->GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
            Pawn->GetMesh()->SetAnimInstanceClass(UAnimInstance::StaticClass());
            Montage->SetObjectPropertyValue_InContainer(Ability, Config->Montage);
            auto Controller = PlayerASC->AbilityActorInfo->PlayerController;
            PlayerASC->RefreshAbilityActorInfo();
            PlayerASC->AbilityActorInfo->PlayerController = Controller;
            return Config->Montage;
        }
        void Step(float Seconds, bool bPhysicalOrbit = false, bool bAdvanceWorldTime = false)
        {
            for (float Time = 0.f; Time + UE_SMALL_NUMBER < Seconds; Time += .02f)
            {
                const float Delta = FMath::Min(.02f, Seconds - Time);
                ++GFrameCounter;
                if (bAdvanceWorldTime)
                {
                    // 长程速率/状态到期读取真实World时钟；TimeOnly不启动敌人AI或额外角色物理。
                    World->Tick(LEVELTICK_TimeOnly, Delta);
                }
                World->GetTimerManager().Tick(Delta);
                if (bPhysicalOrbit)
                {
                    TArray<TWeakObjectPtr<AMyOrbitingFireBolt>> Bolts;
                    for (TActorIterator<AMyOrbitingFireBolt> It(World); It; ++It)
                    {
                        Bolts.Add(*It);
                    }
                    for (const auto& Weak : Bolts)
                    {
                        auto* Bolt = Weak.Get();
                        if (!Bolt || Bolt->IsActorBeingDestroyed())
                        {
                            continue;
                        }
                        if (!Bolt->IsActorInitialized())
                        {
                            Bolt->InitializeComponents();
                            Bolt->PostInitializeComponents();
                        }
                        if (!Bolt->HasActorBegunPlay())
                        {
                            Bolt->DispatchBeginPlay();
                        }
                        Bolt->Tick(Delta);
                        if (IsValid(Bolt) && !Bolt->IsActorBeingDestroyed() && Bolt->ProjectileMovement->IsActive())
                        {
                            Bolt->ProjectileMovement->TickComponent(Delta, LEVELTICK_All, nullptr);
                        }
                    }
                }
                World->GetSubsystem<UAbilityGraphWorldSubsystem>()->Tick(Delta);
                if (bAdvanceWorldTime)
                {
                    if (auto* StateOwner = Target->FindComponentByClass<UBatch6PetrifyComponent>())
                    {
                        StateOwner->TickComponent(Delta, LEVELTICK_All, &StateOwner->PrimaryComponentTick);
                    }
                }
            }
        }
        /** 原真实FireBolt Notify产生生产攻击信号；本测试只推进Orbit弹，避免第二种伤害污染目标。 */
        bool AttackWithFireBoltNotify()
        {
            const auto OtherHandle =
                PlayerASC->GiveAbility(FGameplayAbilitySpec(UAbilityNativeFireBoltProfileTest::StaticClass(), 1));
            if (!PlayerASC->TryActivateAbility(OtherHandle))
            {
                return false;
            }
            auto* Other = CastChecked<UAbilityNativeFireBoltProfileTest>(
                PlayerASC->FindAbilitySpecFromHandle(OtherHandle)->GetPrimaryInstance());
            if (!Other->CommitAbility(OtherHandle, PlayerASC->AbilityActorInfo.Get(),
                                      Other->GetCurrentActivationInfo()))
            {
                return false;
            }
            Other->SpawnProjectiles(Target->GetActorLocation(), {}, false, 0.f, Target);
            Other->EndAbility(OtherHandle, PlayerASC->AbilityActorInfo.Get(), Other->GetCurrentActivationInfo(), true,
                              false);
            return true;
        }
        float Health() const
        {
            return Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
        }
    };
} // namespace AbilityNativePreviewPhaseTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FNativeBeamPreviewDefaultInput,
    "test_25d.DynamicMage.NativePreview.BeamUnmodifiedPreviewUsesOriginalTargetNotifyAndRelease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeBeamPreviewDefaultInput::RunTest(const FString&)
{
    using namespace AbilityNativePreviewPhaseTests;
    int32 OriginalPeriods = 0;
    float OriginalMana = 0.f;
    for (const bool bPreview : {false, true})
    {
        FPlayer F;
        FString Error;
        const auto* Row = F.Definition(true, Error);
        if (!TestNotNull(TEXT("Actual catalog contains the original Beam frontend"), Row))
        {
            AddInfo(Error);
            return false;
        }
        const FString BeamClass = GetPathNameSafe(Row->Base.AbilityClass.Get());
        if (!TestEqual(
                TEXT("Evidence is pinned to the actual directory Electrocute class"), BeamClass,
                FString(TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Thunder/GA_Electrocute.GA_Electrocute_C"))))
        {
            return false;
        }
        AddInfo(TEXT("ActualFrontendClass=") + BeamClass +
                TEXT("; Gate=ServerTargetData/Notify/Release; Montage=fixture asset; Cursor/Network=unverified"));
        auto* Ability = Cast<UMyBeamSpell>(F.Grant(*Row, bPreview, Error));
        if (!TestNotNull(TEXT("Same exact class is granted to the real PlayerState ASC"), Ability) ||
            !TestTrue(TEXT("Fixture binds actual SBC damage without replacing the graph"),
                      F.DamageDependency(Ability, UAbilityNativeThunderScalarEffect::StaticClass(),
                                         UAbilityNativeThunderScalarEffect::GetDamageTag(), 13.f)))
        {
            AddInfo(Error);
            return false;
        }
        auto* Montage = F.MontageDependency(Ability);
        if (!TestNotNull(TEXT("A real compatible montage/mesh is bound for the original animation gate"), Montage))
        {
            return false;
        }
        TestFalse(TEXT("No synthetic modifier is used to force a default snapshot"),
                  F.Developer->HasContributions(Row->Base.AbilityTag));
        const auto Lease = F.Developer->GetPreviewOwnerLease(F.Handle);
        FPredictionKey Key;
        Key.Current = 3051;
        F.PlayerASC->CallServerTryActivateAbility(F.Handle, false, Key);
        if (!TestTrue(TEXT("Original server activation reaches its TargetData wait"), Ability->IsActive()))
        {
            AddInfo(Ability->GetRuntimeRecipeError());
            return false;
        }
        if (bPreview)
        {
            TestTrue(TEXT("Temporary preview has an exact server owner lease"), Lease.IsValid());
            TestEqual(TEXT("This same activation freezes that exact lease"), Ability->GetRuntimeExecutionOwnerLease(),
                      Lease);
            TestEqual(TEXT("Zero-edit preview freezes the actual selected default"),
                      Ability->GetRuntimeRecipeSnapshot().ContentHash, Row->BaseHash);
        }
        else
        {
            TestNull(TEXT("Formal original frontend keeps its ordinary macro route"),
                     Ability->GetRuntimeRecipeSnapshot().Graph.Get());
        }
        int32 Periods = 0;
        int32 Finals = 0;
        int32 Costs = 0;
        int32 Cooldowns = 0;
        auto* TargetASC = F.Target->GetAbilitySystemComponent();
        const auto TargetHook = TargetASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
            [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
            {
                if (!Spec.Def || !Spec.Def->IsA<UAbilityNativeThunderScalarEffect>())
                {
                    return;
                }
                const auto* Instigator = Spec.GetContext().GetOriginalInstigatorAbilitySystemComponent();
                Periods += Instigator == TargetASC ? 1 : 0;
                Finals += Instigator == F.PlayerASC ? 1 : 0;
            });
        const auto SourceHook = F.PlayerASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
            [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
            {
                Costs += Spec.Def == Ability->GetCostGameplayEffect() ? 1 : 0;
                if (Spec.Def == Ability->GetCooldownGameplayEffect())
                {
                    ++Cooldowns;
                    TestEqual(TEXT("Real cooldown GE follows the actual final damage GE"), Finals, 1);
                }
            });
        ON_SCOPE_EXIT
        {
            TargetASC->OnGameplayEffectAppliedDelegateToSelf.Remove(TargetHook);
            F.PlayerASC->OnGameplayEffectAppliedDelegateToSelf.Remove(SourceHook);
        };
        auto* Hit = new FGameplayAbilityTargetData_SingleTargetHit();
        Hit->HitResult =
            FHitResult(F.Target, F.Target->GetCapsuleComponent(), F.Target->GetActorLocation(), FVector::UpVector);
        Hit->HitResult.bBlockingHit = true;
        FGameplayAbilityTargetDataHandle Data;
        Data.Add(Hit);
        TestTrue(TEXT("The original target payload really names the Actor life"),
                 Hit->HitResult.GetActor() == F.Target);
        F.PlayerASC->ServerSetReplicatedTargetData(F.Handle,
                                                   Ability->GetCurrentActivationInfo().GetActivationPredictionKey(),
                                                   Data, FGameplayTag(), FPredictionKey());
        TestTrue(TEXT("The real production montage task is running before notification"),
                 F.Pawn->GetMesh()->GetAnimInstance()->Montage_IsPlaying(Montage));
        TestEqual(TEXT("TargetData alone cannot bypass the Notify damage gate"), F.Health(), 10000.f);
        TestEqual(TEXT("No upkeep is charged before the actual Notify"), Costs, 0);
        const auto Event = FGameplayTag::RequestGameplayTag(TEXT("Event.Montage.Electrocute"));
        FGameplayEventData Payload;
        Payload.EventTag = Event;
        F.PlayerASC->HandleGameplayEvent(Event, &Payload);
        TestTrue(TEXT("Actual Notify enters the original target shock presentation"),
                 ICombatInterface::Execute_IsBeingShocked(F.Target));
        TestEqual(TEXT("Notification starts one owned execution"),
                  F.World->GetSubsystem<UAbilityGraphWorldSubsystem>()->GetActiveExecutionCount(), 1);
        F.PlayerASC->HandleGameplayEvent(Event, &Payload);
        TestEqual(TEXT("Repeated Notify cannot create a second native host"),
                  F.World->GetSubsystem<UAbilityGraphWorldSubsystem>()->GetActiveExecutionCount(), 1);
        F.World->GetTimerManager().Tick(0.f);
        F.Step(.22f);
        TestEqual(TEXT("Two real periods pay exactly twice"), Costs, 2);
        TestEqual(TEXT("Two real target-authored periods run before release"), Periods, 2);
        TestEqual(TEXT("Actual HP is reduced by the two source parameters"), F.Health(), 9974.f);
        F.PlayerASC->AbilityInputTagReleased(InputSlot());
        const auto* Minimum = FindFProperty<FFloatProperty>(Ability->GetClass(), TEXT("MinimumElectrocuteTime"));
        const float Hold = Minimum ? Minimum->GetPropertyValue_InContainer(Ability) : 1.f;
        if (Hold > 0.f)
        {
            TestEqual(TEXT("Early actual release does not bypass the original minimum-hold timer"), Finals, 0);
        }
        for (int32 Index = 0; Index < FMath::CeilToInt((Hold + 1.f) / .02f) && Ability->IsActive(); ++Index)
        {
            F.Step(.02f);
        }
        TestFalse(TEXT("Native minimum-hold completion ends this same actual ASC activation"), Ability->IsActive());
        TestEqual(TEXT("Native release produces exactly one source-authored Final"), Finals, 1);
        TestEqual(TEXT("Native completion commits its real cooldown GE once"), Cooldowns, 1);
        TestEqual(TEXT("Every actual periodic effect has one original payment"), Costs, Periods);
        TestEqual(TEXT("Actual HP matches all original periodic and final parameters"), F.Health(),
                  10000.f - 13.f * (Periods + Finals));
        const float Mana = F.PlayerASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute());
        if (!bPreview)
        {
            OriginalPeriods = Periods;
            OriginalMana = Mana;
        }
        else
        {
            TestEqual(TEXT("Panel default retains the original minimum-duration/cadence contract"), Periods,
                      OriginalPeriods);
            TestEqual(TEXT("Panel default retains actual native mana consumption"), Mana, OriginalMana);
            TestTrue(TEXT("No modifier was silently injected while the default ran"),
                     !F.Developer->HasContributions(Row->Base.AbilityTag));
            TestTrue(TEXT("Ending preview releases only its exact temporary grant"), F.Developer->EndPreview(Error));
            TestNull(TEXT("Temporary native frontend is removed after that same session"),
                     F.PlayerASC->FindAbilitySpecFromHandle(F.Handle));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeOrbitPreviewScalar,
                                 "test_25d.DynamicMage.NativePreview.OrbitDefaultThenPanelContactScalarUsesOneASC",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeOrbitPreviewScalar::RunTest(const FString&)
{
    using namespace AbilityNativePreviewPhaseTests;
    float OriginalUpkeepMana = 0.f;
    for (const bool bPreview : {false, true})
    {
        FPlayer F;
        FString Error;
        const auto* Row = F.Definition(false, Error);
        if (!TestNotNull(TEXT("Actual catalog supplies its original Orbit frontend"), Row))
        {
            AddInfo(Error);
            return false;
        }
        const FString OrbitClass = GetPathNameSafe(Row->Base.AbilityClass.Get());
        if (!TestEqual(
                TEXT("Evidence is pinned to the actual directory Orbit class"), OrbitClass,
                FString(TEXT(
                    "/Game/BluePrints/AbilitySystem/Player/Ability/PassiveSpell/OrbitingFireBolt/GA_OrbitingFireBolt.GA_OrbitingFireBolt_C"))))
        {
            return false;
        }
        AddInfo(TEXT("ActualFrontendClass=") + OrbitClass +
                TEXT("; Gate=ASC input/original attack Notify/physical contact; Cursor/Network=unverified"));
        auto* Ability = Cast<UMyOrbitingFireBoltAbility>(F.Grant(*Row, bPreview, Error));
        if (!TestNotNull(TEXT("Actual original Orbit class is granted to this same ASC"), Ability) ||
            !TestTrue(TEXT("Fixture supplies an actual SBC consuming dependency without replacing the default graph"),
                      F.DamageDependency(Ability, UPureMassIncomingDamageEffect::StaticClass(),
                                         UPureMassIncomingDamageEffect::GetDamageTag(), 10.f)))
        {
            return false;
        }
        TestFalse(TEXT("Opening panel alone has no synthetic modifier source"),
                  F.Developer->HasContributions(Row->Base.AbilityTag));
        F.PlayerASC->AbilityInputTagPressed(InputSlot());
        if (!TestTrue(TEXT("Actual input activates the original native inventory frontend"), Ability->IsActive()))
        {
            AddInfo(Ability->GetRuntimeRecipeError());
            return false;
        }
        if (bPreview)
        {
            TestEqual(TEXT("The default preview freezes in this same activation without a contribution"),
                      Ability->GetRuntimeRecipeSnapshot().ContentHash, Row->BaseHash);
            TestEqual(TEXT("Native inventory belongs to this preview's exact lease"),
                      Ability->GetRuntimeExecutionOwnerLease(), F.Developer->GetPreviewOwnerLease(F.Handle));
        }
        const float Spawn = Ability->Config->SpawnInterval;
        F.World->GetTimerManager().Tick(0.f);
        F.Step(Spawn + .12f, true);
        TestEqual(TEXT("Real original timer replenishes one actual native inventory actor"),
                  Ability->GetLiveFireBoltCount(), 1);
        const float UpkeepMana = F.PlayerASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute());
        if (!bPreview)
        {
            OriginalUpkeepMana = UpkeepMana;
        }
        else
        {
            TestEqual(TEXT("Same-session unmodified preview keeps actual native startup/upkeep costs"), UpkeepMana,
                      OriginalUpkeepMana);
        }
        int32 Hits = 0;
        auto* TargetASC = F.Target->GetAbilitySystemComponent();
        const auto Hook = TargetASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
            [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
            {
                Hits += Spec.Def && Spec.Def->IsA<UPureMassIncomingDamageEffect>() ? 1 : 0;
            });
        ON_SCOPE_EXIT
        {
            TargetASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Hook);
        };
        F.World->SetBegunPlay(true);
        TestTrue(TEXT("Real committed FireBolt Notify emits the production attack signal"),
                 F.AttackWithFireBoltNotify());
        for (int32 Index = 0; Index < 100 && Hits == 0; ++Index)
        {
            F.Step(.02f, true);
        }
        TestEqual(TEXT("The same native inventory performs one genuine physical contact"), Hits, 1);
        TestEqual(TEXT("Unmodified default contact really removes original10 Health"), F.Health(), 9990.f);
        if (bPreview)
        {
            const FString Frozen = Ability->GetRuntimeRecipeSnapshot().ContentHash;
            int32 SinkIndex = Row->Base.DefaultGraph->Nodes.IndexOfByPredicate(
                [](const FAbilityGraphNode& Node)
                {
                    return Node.TypeId == TEXT("Effect.InputDamage");
                });
            if (!TestTrue(TEXT("Panel default exposes its independent contact sink"), SinkIndex != INDEX_NONE))
            {
                return false;
            }
            FAbilityRuntimeDeveloperEdit Edit;
            Edit.DefinitionId = Row->DefinitionId;
            Edit.ModifierId = TEXT("Scale.Damage");
            Edit.NodeIndex = SinkIndex;
            Edit.ExpectedRevision = F.Developer->GetDeveloperState().Revision;
            Edit.ExpectedBaseHash = Row->BaseHash;
            Edit.Value = 2.f;
            if (!TestTrue(TEXT("Trusted panel edit can scale only the actual native contact payload"),
                          F.Developer->ApplyEdit(Edit, Error)))
            {
                AddInfo(Error);
                return false;
            }
            TestEqual(TEXT("An already active inventory keeps its original frozen default"),
                      Ability->GetRuntimeRecipeSnapshot().ContentHash, Frozen);
            F.PlayerASC->CancelAbilityHandle(F.Handle);
            F.PlayerASC->AbilityInputTagPressed(InputSlot());
            TestTrue(TEXT("The same temporary spec can start a new edited activation"), Ability->IsActive());
            TestTrue(TEXT("New activation freezes the accepted new revision"),
                     Ability->GetRuntimeRecipeSnapshot().ContentHash != Frozen);
            F.World->GetTimerManager().Tick(0.f);
            F.Step(Spawn + .12f, true);
            const float Before = F.Health();
            const int32 PriorHits = Hits;
            TestTrue(TEXT("Original attack-notification interface still launches edited inventory"),
                     F.AttackWithFireBoltNotify());
            for (int32 Index = 0; Index < 100 && Hits == PriorHits; ++Index)
            {
                F.Step(.02f, true);
            }
            TestEqual(TEXT("Edited physical contact consumes its local multiplier exactly once"), F.Health(),
                      Before - 20.f);
            TestTrue(TEXT("Preview closes the exact active native inventory lease"), F.Developer->EndPreview(Error));
            TestNull(TEXT("Original temporary spec is removed by its own preview"),
                     F.PlayerASC->FindAbilitySpecFromHandle(F.Handle));
            TestEqual(TEXT("No late native inventory survives exact lease cleanup"), Ability->GetLiveFireBoltCount(),
                      0);
        }
        else
        {
            F.PlayerASC->CancelAbilityHandle(F.Handle);
        }
    }
    return true;
}

namespace AbilityNativeFireBoltPanelTests
{
    using AbilityNativePreviewPhaseTests::FPlayer;

    struct FTrio
    {
        int32 First = INDEX_NONE;
        int32 Third = INDEX_NONE;
        FVector Aim = FVector::ZeroVector;
    };

    TArray<AMyProjectile*> Shots(UWorld* World)
    {
        TArray<AMyProjectile*> Result;
        for (TActorIterator<AMyProjectile> It(World); It; ++It)
        {
            if (!It->IsActorBeingDestroyed())
            {
                Result.Add(*It);
            }
        }
        return Result;
    }

    /** 仅推进世界时钟、原Latent/动画、真实弹体组件和同一图；不运行敌人AI/角色BeginPlay。 */
    void Step(FPlayer& F, float Seconds)
    {
        for (float Elapsed = 0.f; Elapsed + UE_SMALL_NUMBER < Seconds; Elapsed += .01f)
        {
            const float Delta = FMath::Min(.01f, Seconds - Elapsed);
            ++GFrameCounter;
            F.World->Tick(LEVELTICK_TimeOnly, Delta);
            F.World->GetLatentActionManager().ProcessLatentActions(nullptr, Delta);
            F.World->GetTimerManager().Tick(Delta);
            if (auto* Anim = F.Pawn->GetMesh()->GetAnimInstance(); Anim && Anim->IsAnyMontagePlaying())
            {
                F.Pawn->GetMesh()->TickAnimation(Delta, false);
                F.Pawn->GetMesh()->RefreshBoneTransforms();
            }
            for (auto* Shot : Shots(F.World))
            {
                if (!Shot->IsActorBeingDestroyed())
                {
                    Shot->ProjectileMovement->TickComponent(Delta, LEVELTICK_All, nullptr);
                }
            }
            F.World->GetSubsystem<UAbilityGraphWorldSubsystem>()->Tick(Delta);
        }
    }

    void Key(ANativeRecipeRemoteInputController& PC, EInputEvent Event)
    {
        const auto Device = IPlatformInputDeviceMapper::Get().GetPrimaryInputDeviceForUser(PC.GetPlatformUserId());
        PC.InputKey(FInputKeyEventArgs(nullptr, Device, EKeys::T, Event, FPlatformTime::Cycles64()));
    }

    FTrio Targets(FPlayer& F, float Y, bool bFragileFirst)
    {
        auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
        Mass->Enable();
        auto* FirstType = NewObject<UPureMassMonsterType>(F.World);
        FirstType->Health = bFragileFirst ? 10.f : 1000.f;
        FirstType->XPReward = 0;
        auto* ThirdType = NewObject<UPureMassMonsterType>(F.World);
        ThirdType->Health = 1000.f;
        ThirdType->XPReward = 0;
        FTrio Result;
        Result.Aim = FVector(500, Y, 0);
        Result.First = Mass->SpawnMonster(FirstType, Result.Aim);
        Result.Third = Mass->SpawnMonster(ThirdType, FVector(1200, Y, 0));
        F.Target->SetActorLocation(FVector(850, Y, 0));
        auto* ASC = F.Target->GetAbilitySystemComponent();
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 1000.f);
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 1000.f);
        return Result;
    }

    bool Edit(FPlayer& F, const FAbilityRuntimeDeveloperDefinition& Row, int32 Index, FName Modifier, FString& Error)
    {
        FAbilityRuntimeDeveloperEdit Edit;
        Edit.DefinitionId = Row.DefinitionId;
        Edit.ModifierId = Modifier;
        Edit.NodeIndex = Index;
        Edit.ExpectedRevision = F.Developer->GetDeveloperState().Revision;
        Edit.ExpectedBaseHash = Row.BaseHash;
        Edit.Count = 3;
        Edit.Interval = .15f;
        Edit.Radius = 450.f;
        Edit.Falloff = .5f;
        Edit.Value = 2.f;
        return F.Developer->ApplyEdit(Edit, Error);
    }

    /**
     * 服务器传输夹具：生产T输入先路由到原Spec，再模拟客户端激活包送入正式ServerTryActivate。
     * 无网络的远端PC不能在本机生成LocalPredicted激活；合法prediction key仅代替传输，不改变GA策略。
     * 这不验证本地鼠标/真实网络。实例仍由GAS创建，后续使用原TargetData、蓝图和动画事件。
     */
    UMyFireBolt* Cast(FPlayer& F, ANativeRecipeRemoteInputController& PC, const FTrio& Trio, UAnimMontage* Montage,
                      FAutomationTestBase& Test)
    {
        const float Before = F.PlayerASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute());
        Key(PC, IE_Pressed);
        auto* Spec = F.PlayerASC->FindAbilitySpecFromHandle(F.Handle);
        if (!Test.TestTrue(TEXT("Production T input routes its pressed state to the selected original spec"),
                           Spec && Spec->InputPressed))
        {
            return nullptr;
        }
        static int16 NextClientPrediction = 801;
        FPredictionKey ClientActivation;
        ClientActivation.Current = NextClientPrediction++;
        F.PlayerASC->CallServerTryActivateAbility(F.Handle, Spec->InputPressed, ClientActivation);
        UMyFireBolt* Ability = nullptr;
        if (Spec)
        {
            for (auto* Instance : Spec->GetAbilityInstances())
            {
                if (Instance && Instance->IsActive())
                {
                    Ability = ::Cast<UMyFireBolt>(Instance);
                    break;
                }
            }
        }
        if (!Test.TestNotNull(TEXT("Explicit client-activation transport reaches the real FireBolt execution instance"),
                              Ability))
        {
            return nullptr;
        }
        Test.TestTrue(TEXT("The real Blueprint commits its cost before returning target data"),
                      F.PlayerASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()) < Before);
        Test.TestTrue(TEXT("This exact temporary spec freezes its own preview lease"),
                      Ability->GetRuntimeExecutionOwnerLease() == F.Developer->GetPreviewOwnerLease(F.Handle));
        Test.TestNotNull(TEXT("The actual frontend freezes the edited runtime definition"),
                         Ability->GetRuntimeRecipeSnapshot().Graph.Get());
        if (!F.DamageDependency(Ability, UPureMassIncomingDamageEffect::StaticClass(),
                                UPureMassIncomingDamageEffect::GetDamageTag(), 25.f))
        {
            Test.AddError(TEXT("Unable to configure deterministic damage dependency on the current test instance."));
            return nullptr;
        }
        Test.TestEqual(TEXT("Input alone cannot bypass original TargetData/animation notification"),
                       Shots(F.World).Num(), 0);
        auto* Hit = new FGameplayAbilityTargetData_SingleTargetHit();
        Hit->HitResult.bBlockingHit = true;
        Hit->HitResult.Location = Hit->HitResult.ImpactPoint = Trio.Aim;
        Hit->HitResult.Item = Trio.First;
        FGameplayAbilityTargetDataHandle Data;
        Data.Add(Hit);
        F.PlayerASC->ServerSetReplicatedTargetData(F.Handle,
                                                   Ability->GetCurrentActivationInfo().GetActivationPredictionKey(),
                                                   Data, FGameplayTag(), FPredictionKey());
        if (!Test.TestTrue(TEXT("Original hard-coded Aura montage is playing before its event notification"),
                           F.Pawn->GetMesh()->GetAnimInstance()->Montage_IsPlaying(Montage)))
        {
            return nullptr;
        }
        Test.TestEqual(TEXT("TargetData still does not spawn before actual GameplayEvent Notify"), Shots(F.World).Num(),
                       0);
        const float CommittedMana = F.PlayerASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute());
        const auto Event = FGameplayTag::RequestGameplayTag(TEXT("Event.Montage.FireBolt"));
        FGameplayEventData Payload;
        Payload.EventTag = Event;
        F.PlayerASC->HandleGameplayEvent(Event, &Payload);
        if (!Test.TestEqual(TEXT("Real original Aura Notify launches one level1 physical fireball"),
                            Shots(F.World).Num(), 1))
        {
            Test.AddInfo(Ability->GetRuntimeRecipeError());
            return nullptr;
        }
        Test.TestEqual(TEXT("Frozen graph activation does not charge the original Commit twice"),
                       F.PlayerASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), CommittedMana);
        Key(PC, IE_Released);
        return Ability;
    }

    /** 原动画自然结束，原冷却真实到期；不取消在途图、不移除GE来伪造下次施法资格。 */
    bool WaitReady(FPlayer& F, UMyGameplayAbility& Previous)
    {
        for (int32 Index = 0; Index < 2500; ++Index)
        {
            const auto* Tags = Previous.GetCooldownTags();
            const bool bCooldownStillGranted = Tags && F.PlayerASC->HasAnyMatchingGameplayTags(*Tags);
            // 剩余秒数到零时，原GE到期Timer可能尚未在严格截止点之后清Tag；真正可释放需两者都已结束。
            if (!Previous.IsActive() && !bCooldownStillGranted &&
                Previous.GetCooldownTimeRemaining(F.PlayerASC->AbilityActorInfo.Get()) <= UE_SMALL_NUMBER &&
                F.World->GetSubsystem<UAbilityGraphWorldSubsystem>()->GetActiveExecutionCount() == 0)
            {
                return true;
            }
            Step(F, .01f);
        }
        return false;
    }

    bool Exercise(FAutomationTestBase& Test, bool bAppendOnAwait)
    {
        FPlayer F;
        FString Error;
        if (!Test.TestTrue(TEXT("Real Mage panel catalog initializes"), F.Developer->InitializeCatalog(nullptr, Error)))
        {
            return false;
        }
        const auto* Row = F.Developer->GetDefinitions().FindByPredicate(
            [](const FAbilityRuntimeDeveloperDefinition& Item)
            {
                return GetPathNameSafe(Item.Base.AbilityClass.Get()) ==
                       TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Fire/FireBolt/GA_FireBolt.GA_FireBolt_C");
            });
        if (!Test.TestNotNull(TEXT("The actual original FireBolt directory row exists"), Row) ||
            !Test.TestTrue(TEXT("Panel begins its exact temporary FireBolt grant"),
                           F.Developer->BeginPreview(Row->DefinitionId, Error)))
        {
            Test.AddInfo(Error);
            return false;
        }
        F.Handle = F.Developer->GetPreviewHandle();
        Test.AddInfo(
            TEXT("ActualFrontendClass=") + GetPathNameSafe(Row->Base.AbilityClass.Get()) +
            TEXT(
                "; Gate=T attempt/explicit prediction transport/ServerTargetData/original Aura Notify/physical chain; Cursor/Network=unverified"));
        Test.TestFalse(TEXT("No manual base registration or synthetic no-op contribution selects this default"),
                       F.Developer->HasContributions(Row->Base.AbilityTag));
        auto* PC = F.World->SpawnActor<ANativeRecipeRemoteInputController>();
        PC->SetPawn(F.Pawn);
        F.PlayerASC->AbilityActorInfo->PlayerController = PC;
        auto* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Assets/Characters/Aura/SKM_Aura.SKM_Aura"));
        auto* Montage = LoadObject<UAnimMontage>(
            nullptr, TEXT("/Game/Assets/Characters/Aura/Animations/Abilities/AM_Cast_FireBolt.AM_Cast_FireBolt"));
        if (!Test.TestNotNull(TEXT("Real Aura source mesh loads for the original hard-coded montage"), Mesh) ||
            !Test.TestNotNull(TEXT("Original FireBolt montage loads"), Montage))
        {
            return false;
        }
        F.Pawn->GetMesh()->SetSkeletalMeshAsset(Mesh);
        F.Pawn->GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
        F.Pawn->GetMesh()->SetAnimInstanceClass(UAnimInstance::StaticClass());
        F.PlayerASC->RefreshAbilityActorInfo();
        F.PlayerASC->AbilityActorInfo->PlayerController = PC;
        const int32 AwaitIndex = Row->Base.DefaultGraph->Nodes.IndexOfByPredicate(
            [](const FAbilityGraphNode& Node)
            {
                return Node.TypeId == TEXT("Delivery.ProjectileAwait");
            });
        const int32 SinkIndex = Row->Base.DefaultGraph->Nodes.IndexOfByPredicate(
            [](const FAbilityGraphNode& Node)
            {
                return Node.TypeId == TEXT("Effect.InputDamage");
            });
        if (!Test.TestTrue(TEXT("Actual native default exposes delivery and payload axes"),
                           AwaitIndex != INDEX_NONE && SinkIndex != INDEX_NONE) ||
            !Test.TestTrue(TEXT("Panel server template replaces only its native delivery axis"),
                           Edit(F, *Row, AwaitIndex, TEXT("Propagation.Sequential"), Error)))
        {
            Test.AddInfo(Error);
            return false;
        }
        F.World->SetBegunPlay(true);
        auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
        const auto First = Targets(F, 0.f, true);
        const TStrongObjectPtr<UMyFireBolt> FirstAbility(Cast(F, *PC, First, Montage, Test));
        if (!FirstAbility.IsValid())
        {
            return false;
        }
        const FString FirstHash = FirstAbility->GetRuntimeRecipeSnapshot().ContentHash;
        FPureMassView View;
        bool bFirstKilled = false;
        for (int32 Index = 0; Index < 1200 && !bFirstKilled; ++Index)
        {
            Step(F, .01f);
            bFirstKilled = !Mass->Resolve(First.First, View);
        }
        if (!Test.TestTrue(TEXT("Original pitch25 native fireball really kills the first Mass life"), bFirstKilled))
        {
            for (const auto* Shot : Shots(F.World))
            {
                Test.AddInfo(
                    FString::Printf(TEXT("Unresolved physical flight: class=%s pos=%s velocity=%s active=%d query=%d"),
                                    *Shot->GetClass()->GetPathName(), *Shot->GetActorLocation().ToString(),
                                    *Shot->ProjectileMovement->Velocity.ToString(),
                                    Shot->ProjectileMovement->IsActive(), Shot->Sphere->IsQueryCollisionEnabled()));
            }
            return false;
        }
        Test.TestEqual(TEXT("B cannot be damaged before its own physical fireball"), F.Health(), 1000.f);
        Mass->Resolve(First.Third, View);
        Test.TestEqual(TEXT("C cannot be damaged by an invisible chain operation"), View.Health, 1000.f);
        if (!Test.TestTrue(
                TEXT("Panel adds a new status source while the previous cast is in flight"),
                Edit(F, *Row, bAppendOnAwait ? AwaitIndex : SinkIndex, TEXT("Append.Status.Petrify"), Error)))
        {
            Test.AddInfo(Error);
            return false;
        }
        if (FirstAbility->IsActive())
        {
            Test.TestEqual(TEXT("Existing active frontend retains its earlier frozen definition"),
                           FirstAbility->GetRuntimeRecipeSnapshot().ContentHash, FirstHash);
        }
        Step(F, .06f);
        Test.TestEqual(TEXT("Short hop delay does not launch another fireball early"), Shots(F.World).Num(), 0);
        for (int32 Index = 0; Index < 1200 && F.Health() == 1000.f; ++Index)
        {
            Step(F, .01f);
        }
        Test.TestEqual(TEXT("B receives one actual hop with one falloff"), F.Health(), 987.5f);
        Mass->Resolve(First.Third, View);
        Test.TestEqual(TEXT("C still waits for B's real physical receipt"), View.Health, 1000.f);
        Test.TestFalse(TEXT("A newly edited rule cannot alter the old in-flight B payload"),
                       F.Target->GetMesh()->bPauseAnims);
        Test.TestTrue(TEXT("Old cast and its original cooldown naturally finish"), WaitReady(F, *FirstAbility));
        Mass->Resolve(First.Third, View);
        Test.TestEqual(TEXT("Old C receives exactly two cumulative falloffs"), View.Health, 993.75f);
        Test.TestFalse(TEXT("Old C snapshot has no newly appended status"), View.bPetrified);

        const auto Second = Targets(F, 800.f, true);
        const TStrongObjectPtr<UMyFireBolt> SecondAbility(Cast(F, *PC, Second, Montage, Test));
        if (!SecondAbility.IsValid())
        {
            return false;
        }
        Test.TestTrue(TEXT("Next real activation freezes the accepted new rule"),
                      SecondAbility->GetRuntimeRecipeSnapshot().ContentHash != FirstHash);
        for (int32 Index = 0; Index < 1800; ++Index)
        {
            Step(F, .01f);
            if (Mass->Resolve(Second.Third, View) && View.Health < 1000.f)
            {
                break;
            }
        }
        Test.TestEqual(TEXT("Status append does not replace the actual B damage"), F.Health(), 987.5f);
        Test.TestTrue(bAppendOnAwait ? TEXT("Await-axis append must fire from its real confirmed Hit")
                                     : TEXT("Post-damage-axis append receives its real DamageResolved control"),
                      F.Target->GetMesh()->bPauseAnims);
        Test.TestTrue(TEXT("Final Mass life remains queryable after its actual new payload"),
                      Mass->Resolve(Second.Third, View));
        Test.TestEqual(TEXT("Appended state does not multiply final C damage"), View.Health, 993.75f);
        Test.TestTrue(TEXT("New Mass contact also receives the real appended status"), View.bPetrified);
        Test.TestTrue(TEXT("Second ordinary animation/independent cast/cooldown finish naturally"),
                      WaitReady(F, *SecondAbility));
        const auto Lease = F.Developer->GetPreviewOwnerLease(F.Handle);
        Test.TestTrue(TEXT("Public preview close withdraws exactly its own edited sources"),
                      F.Developer->EndPreview(Error));
        Test.TestTrue(TEXT("Public preview restart selects the actual unmodified default again"),
                      F.Developer->BeginPreview(Row->DefinitionId, Error));
        F.Handle = F.Developer->GetPreviewHandle();
        Test.TestFalse(TEXT("Withdrawal leaves no synthetic contribution"),
                       F.Developer->HasContributions(Row->Base.AbilityTag));
        Test.TestTrue(TEXT("Reopened preview has a new precise lease"),
                      F.Developer->GetPreviewOwnerLease(F.Handle) != Lease);
        const auto Default = Targets(F, -800.f, false);
        const TStrongObjectPtr<UMyFireBolt> DefaultAbility(Cast(F, *PC, Default, Montage, Test));
        if (!DefaultAbility.IsValid())
        {
            return false;
        }
        Test.TestEqual(TEXT("Restored activation freezes the true registered default hash"),
                       DefaultAbility->GetRuntimeRecipeSnapshot().ContentHash, Row->BaseHash);
        Test.TestTrue(TEXT("Restored ordinary cast and cooldown complete"), WaitReady(F, *DefaultAbility));
        Mass->Resolve(Default.First, View);
        Test.TestEqual(TEXT("Restored default hits only its single real first target"), View.Health, 975.f);
        Test.TestFalse(TEXT("Withdrawn status cannot affect the next default contact"), View.bPetrified);
        Test.TestEqual(TEXT("Withdrawn propagation never creates a hidden B hit"), F.Health(), 1000.f);
        Mass->Resolve(Default.Third, View);
        Test.TestEqual(TEXT("Withdrawn propagation never creates a hidden C hit"), View.Health, 1000.f);
        return true;
    }
} // namespace AbilityNativeFireBoltPanelTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FNativePanelFireBoltAwaitAppend,
    "test_25d.DynamicMage.NativePanel.ServerTransport.FireBoltPropagationAwaitAppendAndFrozenWithdrawal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativePanelFireBoltAwaitAppend::RunTest(const FString&)
{
    return AbilityNativeFireBoltPanelTests::Exercise(*this, true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FNativePanelFireBoltDamageAppend,
    "test_25d.DynamicMage.NativePanel.ServerTransport.FireBoltPropagationPostDamageAppendControl",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativePanelFireBoltDamageAppend::RunTest(const FString&)
{
    return AbilityNativeFireBoltPanelTests::Exercise(*this, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FActualShardDirectoryPreview,
    "test_25d.DynamicMage.NativePreview.ServerTransportActualShardDirectoryDefaultAndZeroEditPreview",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FActualShardDirectoryPreview::RunTest(const FString&)
{
    using AbilityNativePreviewPhaseTests::FPlayer;
    FPlayer F;
    FString Error;
    if (!TestTrue(TEXT("Actual Mage directory initializes"), F.Developer->InitializeCatalog(nullptr, Error)))
    {
        AddInfo(Error);
        return false;
    }
    const auto* Row = F.Developer->GetDefinitions().FindByPredicate(
        [](const FAbilityRuntimeDeveloperDefinition& Item)
        {
            return GetPathNameSafe(Item.Base.AbilityClass.Get()) ==
                   TEXT(
                       "/Game/BluePrints/AbilitySystem/Player/Ability/Arcane/ArcaneChard/GA_ArcaneShards.GA_ArcaneShards_C");
        });
    if (!TestNotNull(TEXT("Exact actual Shards directory class exists"), Row))
    {
        return false;
    }
    const auto* Defaults = CastChecked<UMyArcaneShardAbility>(Row->Base.AbilityClass->GetDefaultObject());
    if (!TestNotNull(TEXT("Actual original ShardMontage is configured on this asset"), Defaults->ShardMontage.Get()) ||
        !TestNotNull(TEXT("Actual original PointCollection class is configured"),
                     Defaults->ShardPointCollectionClass.Get()))
    {
        return false;
    }
    auto* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Assets/Characters/Aura/SKM_Aura.SKM_Aura"));
    if (!TestNotNull(TEXT("Actual Aura model dependency loads for the original Shards montage"), Mesh))
    {
        return false;
    }
    F.Pawn->GetMesh()->SetSkeletalMeshAsset(Mesh);
    F.Pawn->GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
    F.Pawn->GetMesh()->SetAnimInstanceClass(UAnimInstance::StaticClass());
    F.PlayerASC->RefreshAbilityActorInfo();
    auto* PC = F.World->SpawnActor<AAbilityGraphRemoteTestController>();
    PC->SetPawn(F.Pawn);
    F.PlayerASC->AbilityActorInfo->PlayerController = PC;
    F.Target->SetActorLocation(FVector(600, 0, 0));
    const auto* SameASC = F.PlayerASC;
    int32 BaselineHits = 0;
    float BaselineLoss = 0.f;
    float BaselineManaCost = 0.f;
    TArray<FVector> BaselinePoints;
    for (const bool bPreview : {false, true})
    {
        const auto Tag = AbilityNativePreviewPhaseTests::InputSlot();
        if (bPreview)
        {
            if (!TestTrue(TEXT("Same ASC opens the actual directory zero-edit preview"),
                          F.Developer->BeginPreview(Row->DefinitionId, Error)))
            {
                AddInfo(Error);
                return false;
            }
            F.Handle = F.Developer->GetPreviewHandle();
            TestFalse(TEXT("Zero-edit preview has no synthetic contribution"),
                      F.Developer->HasContributions(Row->Base.AbilityTag));
        }
        else
        {
            FGameplayAbilitySpec Spec(Row->Base.AbilityClass, 1, INDEX_NONE, F.State);
            Spec.DynamicAbilityTags.AddTag(Tag);
            Spec.DynamicAbilityTags.AddTag(FMyGameplayTags::Get().Abilities_Status_Equipped);
            F.Handle = F.PlayerASC->GiveAbility(Spec);
        }
        // 两次真实施法使用同一ASC；只统一测试起始属性，旧冷却在下方真实到期后才进入下一轮。
        F.PlayerASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 10000.f);
        F.Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 1000.f);
        AddInfo(
            TEXT("ActualFrontendClass=") + GetPathNameSafe(Row->Base.AbilityClass.Get()) +
            TEXT(
                "; Gate=ASC first/confirm press/explicit prediction transport/ServerTargetData/original montage Notify; Cursor/Network=unverified"));
        F.PlayerASC->AbilityInputTagPressed(Tag);
        F.PlayerASC->AbilityInputTagHeld(Tag);
        auto* Spec = F.PlayerASC->FindAbilitySpecFromHandle(F.Handle);
        if (!TestTrue(TEXT("Actual input enters its selected original spec"), Spec && Spec->InputPressed))
        {
            return false;
        }
        FPredictionKey Request;
        Request.Current = bPreview ? 2402 : 2401;
        F.PlayerASC->CallServerTryActivateAbility(F.Handle, Spec->InputPressed, Request);
        UMyArcaneShardAbility* Active = nullptr;
        for (auto* Instance : Spec->GetAbilityInstances())
        {
            if (Instance && Instance->IsActive())
            {
                Active = Cast<UMyArcaneShardAbility>(Instance);
                break;
            }
        }
        const TStrongObjectPtr<UMyArcaneShardAbility> Ability(Active);
        if (!TestNotNull(TEXT("Actual per-execution asset reaches its second-press wait"), Active))
        {
            return false;
        }
        TestTrue(TEXT("ASC identity stays identical between original and preview"), SameASC == F.PlayerASC);
        TestEqual(TEXT("Original activation does not pay before confirmation or first shard"),
                  F.PlayerASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 10000.f);
        if (bPreview)
        {
            TestEqual(TEXT("Zero-edit actual preview freezes the selected default without dummy patches"),
                      Active->GetRuntimeRecipeSnapshot().ContentHash, Row->BaseHash);
        }
        if (!TestTrue(TEXT("Only deterministic GE dependency is supplied to the actual instance"),
                      F.DamageDependency(Active, UPureMassIncomingDamageEffect::StaticClass(),
                                         UPureMassIncomingDamageEffect::GetDamageTag(), 10.f)))
        {
            return false;
        }
        // 原二次按键产生GAS InputPressed；不直接调用OnConfirmPress或任何GA私有业务函数。
        AddInfo(FString::Printf(TEXT("Shard confirmation keys: active=%d spec=%d"),
                                Active->GetCurrentActivationInfo().GetActivationPredictionKey().Current,
                                Spec->ActivationInfo.GetActivationPredictionKey().Current));
        F.PlayerASC->AbilityInputTagReleased(Tag);
        F.PlayerASC->AbilityInputTagPressed(Tag);
        auto* Hit = new FGameplayAbilityTargetData_SingleTargetHit();
        Hit->HitResult.bBlockingHit = true;
        Hit->HitResult.Location = Hit->HitResult.ImpactPoint = FVector(600, 0, 0);
        FGameplayAbilityTargetDataHandle Data;
        Data.Add(Hit);
        FMath::RandInit(902117);
        F.PlayerASC->ServerSetReplicatedTargetData(F.Handle,
                                                   Active->GetCurrentActivationInfo().GetActivationPredictionKey(),
                                                   Data, FGameplayTag(), FPredictionKey());
        if (!TestTrue(TEXT("The actual original Shards montage is playing before Notify"),
                      F.Pawn->GetMesh()->GetAnimInstance()->Montage_IsPlaying(Defaults->ShardMontage)))
        {
            return false;
        }
        auto* Property = FindFProperty<FObjectPropertyBase>(Active->GetClass(), TEXT("PreparedPoints"));
        auto* Prepared =
            Property ? Cast<UAbilityArcaneShardContext>(Property->GetObjectPropertyValue_InContainer(Active)) : nullptr;
        if (!TestNotNull(TEXT("Original TargetData really prepared its actual point collection"), Prepared))
        {
            return false;
        }
        TestEqual(TEXT("Original actual level1 path respects the asset point limit"), Prepared->Points.Num(),
                  FMath::Clamp(3, 1, Defaults->MaxShardPoints));
        if (!bPreview)
        {
            BaselinePoints = Prepared->Points;
        }
        else
        {
            TestTrue(TEXT("Same original PointCollection and seeded target preparation are retained"),
                     Prepared->Points == BaselinePoints);
        }
        const auto* Cost = Active->GetCostGameplayEffect();
        const auto* Cooldown = Active->GetCooldownGameplayEffect();
        if (!TestNotNull(TEXT("Actual original cost GE exists"), Cost) ||
            !TestNotNull(TEXT("Actual original cooldown GE exists"), Cooldown))
        {
            return false;
        }
        int32 Costs = 0;
        int32 Cooldowns = 0;
        int32 Hits = 0;
        float ActualCooldownDuration = 0.f;
        FActiveGameplayEffectHandle ActualCooldownHandle;
        const auto SourceHook = F.PlayerASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
            [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Effect, FActiveGameplayEffectHandle Handle)
            {
                Costs += Effect.Def == Cost ? 1 : 0;
                Cooldowns += Effect.Def == Cooldown ? 1 : 0;
                if (Effect.Def == Cooldown)
                {
                    ActualCooldownDuration = Effect.GetDuration();
                    ActualCooldownHandle = Handle;
                }
            });
        auto* TargetASC = F.Target->GetAbilitySystemComponent();
        const auto TargetHook = TargetASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
            [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Effect, FActiveGameplayEffectHandle)
            {
                if (Effect.Def && Effect.Def->IsA<UPureMassIncomingDamageEffect>())
                {
                    if (Hits++ == 0)
                    {
                        TestEqual(TEXT("Actual first shard GE precedes original cost"), Costs, 0);
                        TestEqual(TEXT("Actual first shard precedes tail cooldown"), Cooldowns, 0);
                    }
                }
            });
        ON_SCOPE_EXIT
        {
            F.PlayerASC->OnGameplayEffectAppliedDelegateToSelf.Remove(SourceHook);
            TargetASC->OnGameplayEffectAppliedDelegateToSelf.Remove(TargetHook);
        };
        const auto Notify = FGameplayTag::RequestGameplayTag(TEXT("Event.Montage.AracaneShards"));
        FGameplayEventData Payload;
        Payload.EventTag = Notify;
        TestEqual(TEXT("No real damage occurs before original Notify"), F.Health(), 1000.f);
        F.PlayerASC->HandleGameplayEvent(Notify, &Payload);
        TestTrue(TEXT("Original Notify produces actual first Actor GE"), Hits > 0);
        TestEqual(TEXT("Original cost runs once after actual first shard"), Costs, 1);
        TestEqual(TEXT("No early cooldown before loop check and tail"), Cooldowns, 0);
        const int32 AtRelease = Hits;
        F.PlayerASC->HandleGameplayEvent(Notify, &Payload);
        TestEqual(TEXT("Repeated original Notify cannot repeat first point or fee"), Hits, AtRelease);
        F.World->GetTimerManager().Tick(0.f);
        for (int32 Index = 0;
             Index < 1200 &&
             (Active->IsActive() || F.World->GetSubsystem<UAbilityGraphWorldSubsystem>()->GetActiveExecutionCount());
             ++Index)
        {
            AbilityNativeFireBoltPanelTests::Step(F, .01f);
        }
        TestFalse(TEXT("Actual point loop and tail naturally end the original GA"), Active->IsActive());
        TestEqual(TEXT("Exactly one original tail cooldown is committed"), Cooldowns, 1);
        TestEqual(TEXT("No later shard attempts a second fee"), Costs, 1);
        const float Lost = 1000.f - F.Health();
        const float ManaCost = 10000.f - F.PlayerASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute());
        TestEqual(TEXT("Every actual deterministic Actor GE contributes exactly once"), Lost, Hits * 10.f);
        TestTrue(TEXT("Original cost really consumes Mana"), ManaCost > 0.f);
        if (!bPreview)
        {
            BaselineHits = Hits;
            BaselineLoss = Lost;
            BaselineManaCost = ManaCost;
        }
        else
        {
            TestEqual(TEXT("Zero-edit preview preserves actual point contact count"), Hits, BaselineHits);
            TestEqual(TEXT("Zero-edit preview preserves actual total HP loss"), Lost, BaselineLoss);
            TestEqual(TEXT("Zero-edit preview preserves original cost"), ManaCost, BaselineManaCost);
        }
        int32 LiveCollections = 0;
        for (TActorIterator<APointCollection> It(F.World); It; ++It)
        {
            LiveCollections += !It->IsActorBeingDestroyed();
        }
        TestEqual(TEXT("Original collection is released after its owning cast"), LiveCollections, 0);
        const auto* CooldownTags = Active->GetCooldownTags();
        const auto CooldownState = [&]()
        {
            const auto* Effect = F.PlayerASC->GetActiveGameplayEffect(ActualCooldownHandle);
            return FString::Printf(
                TEXT(
                    "Shard actual cooldown: world=%.9f duration=%.9f APIremaining=%.9f activeGEremaining=%.9f activeGE=%d tags=%s blocked=%d"),
                F.World->GetTimeSeconds(), ActualCooldownDuration,
                Active->GetCooldownTimeRemaining(F.PlayerASC->AbilityActorInfo.Get()),
                Effect ? Effect->GetTimeRemaining(F.World->GetTimeSeconds()) : 0.f, Effect != nullptr,
                CooldownTags ? *CooldownTags->ToStringSimple() : TEXT("<none>"),
                CooldownTags && F.PlayerASC->HasAnyMatchingGameplayTags(*CooldownTags));
        };
        AddInfo(TEXT("Before natural wait: ") + CooldownState());
        if (!TestTrue(TEXT("Original cooldown expires naturally before leaving this session"),
                      AbilityNativeFireBoltPanelTests::WaitReady(F, *Active)))
        {
            AddInfo(TEXT("Failed natural wait: ") + CooldownState());
            return false;
        }
        AddInfo(TEXT("After natural wait: ") + CooldownState());
        F.PlayerASC->AbilityInputTagReleased(Tag);
        if (!bPreview)
        {
            F.PlayerASC->ClearAbility(F.Handle); // 只移除本夹具授予的旧正式Spec，不清理其它来源或ASC。
        }
        else
        {
            TestTrue(TEXT("Zero-edit preview closes its exact temporary original spec"),
                     F.Developer->EndPreview(Error));
        }
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FNativeOrbitPreviewRepeatedStatus,
    "test_25d.DynamicMage.NativePreview.OrbitActualAppendStatusSurvives325ContactsAndRejectsNextLife",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeOrbitPreviewRepeatedStatus::RunTest(const FString&)
{
    using namespace AbilityNativePreviewPhaseTests;
    for (const bool bAppend : {false, true})
    {
        FPlayer F;
        FString Error;
        const auto* Row = F.Definition(false, Error);
        if (!TestNotNull(TEXT("Actual native Orbit catalog row exists"), Row))
        {
            return false;
        }
        auto* Ability = Cast<UMyOrbitingFireBoltAbility>(F.Grant(*Row, true, Error));
        if (!TestNotNull(TEXT("Same real preview protocol grants the original native frontend"), Ability) ||
            !TestTrue(TEXT("The real damage sink consumes a deterministic SBC dependency"),
                      F.DamageDependency(Ability, UPureMassIncomingDamageEffect::StaticClass(),
                                         UPureMassIncomingDamageEffect::GetDamageTag(), 10.f)))
        {
            return false;
        }
        if (bAppend)
        {
            FAbilityRuntimeDeveloperEdit Edit;
            Edit.DefinitionId = Row->DefinitionId;
            Edit.NodeIndex = Row->Base.DefaultGraph->Nodes.IndexOfByPredicate(
                [](const FAbilityGraphNode& Node)
                {
                    return Node.TypeId == TEXT("Effect.InputDamage");
                });
            Edit.ModifierId = TEXT("Append.Status.Petrify");
            Edit.Value = .2f;
            Edit.ExpectedRevision = F.Developer->GetDeveloperState().Revision;
            Edit.ExpectedBaseHash = Row->BaseHash;
            if (!TestTrue(TEXT("Actual native repeated contact accepts its exact historical resolver and status"),
                          F.Developer->ApplyEdit(Edit, Error)))
            {
                AddInfo(Error);
                return false;
            }
        }
        F.PlayerASC->AbilityInputTagPressed(InputSlot());
        if (!TestTrue(TEXT("Original equipped input activates this exact native inventory"), Ability->IsActive()))
        {
            return false;
        }
        int32 Contacts = 0;
        int32 StatusFacts = 0;
        int64 LastSequence = 0;
        bool bBadEnd = false;
        auto* TargetASC = F.Target->GetAbilitySystemComponent();
        const auto Hook = TargetASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
            [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
            {
                Contacts += Spec.Def && Spec.Def->IsA<UPureMassIncomingDamageEffect>() ? 1 : 0;
            });
        ON_SCOPE_EXIT
        {
            TargetASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Hook);
        };
        auto Sample = [&]()
        {
            for (const auto& Record : F.Developer->GetObservations())
            {
                if (Record.Sequence <= LastSequence)
                {
                    continue;
                }
                LastSequence = Record.Sequence;
                StatusFacts += Record.Kind == EAbilityRuntimeObservationKind::GraphEvent &&
                                       Record.NodeType == TEXT("Status.Petrify") && Record.Stage == TEXT("Completed")
                                   ? 1
                                   : 0;
                bBadEnd |= Record.Kind == EAbilityRuntimeObservationKind::GraphFinished &&
                           (Record.EndReason == EAbilityGraphEndReason::Failed ||
                            Record.EndReason == EAbilityGraphEndReason::BudgetExceeded ||
                            Record.EndReason == EAbilityGraphEndReason::SourceInvalid);
            }
        };
        F.World->SetBegunPlay(true);
        F.World->GetTimerManager().Tick(0.f);
        F.Step(Ability->Config->SpawnInterval + .12f, true, true);
        if (!TestTrue(TEXT("Original committed FireBolt Notify selects the real target once"),
                      F.AttackWithFireBoltNotify()))
        {
            return false;
        }
        const float Limit = (Ability->Config->SpawnInterval + Ability->Config->LaunchInterval + 1.f) * 330.f;
        for (float Time = 0.f; Time < Limit && Contacts < 325 && Ability->IsActive(); Time += .02f)
        {
            F.Step(.02f, true, true);
            Sample();
        }
        Sample();
        AddInfo(FString::Printf(TEXT("Append=%d PhysicalContacts=%d StatusFacts=%d WorldTime=%.3f Mana=%.3f"), bAppend,
                                Contacts, StatusFacts, F.World->GetTimeSeconds(),
                                F.PlayerASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute())));
        TestTrue(TEXT("Original actual inventory reaches more than300 physical contacts without truncation"),
                 Contacts >= 325);
        TestEqual(TEXT("The external payload adds one actual state fact per contact, without a finite64 cutoff"),
                  StatusFacts, bAppend ? Contacts : 0);
        TestEqual(TEXT("The native damage owner still applies original10 exactly once per real contact"), F.Health(),
                  10000.f - Contacts * 10.f);
        TestFalse(TEXT("No partial success hides a failed, exhausted or invalid-source graph"), bBadEnd);
        TestTrue(TEXT("Native inventory and observed execution metadata remain bounded"),
                 Ability->GetLiveFireBoltCount() <= Ability->Config->MaxFireBolts &&
                     F.Developer->GetObservedExecutionCount() <= 4);
        const int32 PriorContacts = Contacts;
        const int32 PriorStatus = StatusFacts;
        F.Target->EnableDungeonPooling();
        F.Step(Ability->Config->MaxFlightSeconds + Ability->Config->SpawnInterval + .1f, true, true);
        Sample();
        TestEqual(TEXT("Old selected flights cannot damage the same Actor's next pool life"), Contacts, PriorContacts);
        TestEqual(TEXT("Old historical resolver facts cannot grant state to that new life"), StatusFacts, PriorStatus);
        TestTrue(TEXT("Closing the actual preview cancels its exact persistent inventory lease"),
                 F.Developer->EndPreview(Error));
        TestEqual(TEXT("No native inventory actor survives lease cleanup"), Ability->GetLiveFireBoltCount(), 0);
    }
    return true;
}
#endif
