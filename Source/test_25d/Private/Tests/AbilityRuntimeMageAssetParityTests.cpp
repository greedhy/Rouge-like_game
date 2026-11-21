// 调用链：三个真实职业目录 -> 每个原型GA的实际DA -> 旧默认图/细默认编译器 -> 同一执行器和真实GAS/Mass结算。
// 本文件验证动作层逐资产重建；费用/TargetData/Notify由前端专项测试验证，不将直接图调用冒充真实玩家输入。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Data/AbilityInfo.h"
#include "AbilitySystem/Data/CharacterClassInfo.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionAbility.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionNode.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionWorldSubsystem.h"
#include "AbilitySystem/Batch6/Batch6ChainAbility.h"
#include "AbilitySystem/Batch6/Batch6Petrify.h"
#include "AbilitySystem/Batch6/Batch6FieldAbility.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityLegacyGraphCompiler.h"
#include "AbilitySystem/Unified/AbilityPrototypeFineCompiler.h"
#include "PureMass/PureMassAbility.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassTypes.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "Game/MyGameModeBase.h"
#include "Engine/GameInstance.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Terrain/TerrainSurface.h"
#include "EngineUtils.h"
#include "UObject/Script.h"
#include "UObject/UnrealType.h"

namespace AbilityRuntimeMageAssetParityTests
{
    /** 从当前资产而非文档清单发现具体原型类；两个专用Batch6输入壳由独立前端用例验证。 */
    TArray<UClass*> ActualClasses()
    {
        TArray<UClass*> Result;
        for (const TCHAR* Path : {TEXT("/Game/BluePrints/AbilitySystem/Data/DA_AbilityInfo.DA_AbilityInfo"),
                                  TEXT("/Game/PureMass/DA_MassAbilityInfo.DA_MassAbilityInfo"),
                                  TEXT("/Game/PrototypeExtensions/Batch5/DA_AbilityInfo.DA_AbilityInfo")})
        {
            const auto* Catalog = LoadObject<UAbilityInfo>(nullptr, Path);
            if (!Catalog)
            {
                continue;
            }
            for (const auto& Row : Catalog->AbilityInformation)
            {
                if (Row.RequiredProfession == EPlayerProfession::Mage && Row.Ability &&
                    Row.Ability->IsChildOf(UPrototypeAbility::StaticClass()) &&
                    !Row.Ability->IsChildOf(UBatch6ChainAbility::StaticClass()) &&
                    !Row.Ability->IsChildOf(UBatch6PetrifyAbility::StaticClass()))
                {
                    Result.AddUnique(Row.Ability.Get());
                }
            }
        }
        Result.Sort(
            [](const UClass& A, const UClass& B)
            {
                return A.GetPathName() < B.GetPathName();
            });
        return Result;
    }

    struct FWorld : FAbilityGraphTestFixture
    {
        FEditorScriptExecutionGuard Guard;
        TArray<int32> MassTargets;
        UAbilityGraphInstance* Run = nullptr;
        UPrototypeCastContext* Context = nullptr;

        /** 真实伤害执行需要原伤害系数表；不编辑CDO/资产，世界只包含此用例的来源与目标。 */
        FWorld()
        {
            World->SetGameInstance(NewObject<UGameInstance>(GEngine));
            FURL URL;
            URL.AddOption(TEXT("game=/Script/test_25d.MyGameModeBase"));
            World->SetGameMode(URL);
            auto* Mode = CastChecked<AMyGameModeBase>(World->GetAuthGameMode());
            Mode->CharacterClassInfo = LoadObject<UCharacterClassInfo>(
                nullptr, TEXT("/Game/BluePrints/AbilitySystem/Data/DA_CharacterClassInfo.DA_CharacterClassInfo"));
            for (AEnemyCharacter* Character : {Source, Target})
            {
                for (auto* Part : TInlineComponentArray<UPrimitiveComponent*>(Character))
                {
                    Part->SetCollisionEnabled(Part == Character->GetCapsuleComponent()
                                                  ? ECollisionEnabled::QueryOnly
                                                  : ECollisionEnabled::NoCollision);
                }
                Character->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
                Character->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
                auto* Component = Character->GetAbilitySystemComponent();
                Component->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100000.f);
                Component->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100000.f);
                Component->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100000.f);
                Component->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 100000.f);
            }
            Target->SetActorLocation(FVector(300, 0, 0));
            auto* Mass = World->GetSubsystem<UPureMassSubsystem>();
            Mass->Enable();
            auto* Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = 100000.f;
            Type->Radius = 10.f;
            Type->HalfHeight = 30.f;
            Type->XPReward = 0;
            for (const FVector Position : {FVector(240, 80, 0), FVector(360, 80, 0), FVector(500, 0, 0)})
            {
                MassTargets.Add(Mass->SpawnMonster(Type, Position));
            }
            auto* Floor = World->SpawnActor<AActor>();
            auto* Box = NewObject<UBoxComponent>(Floor);
            Floor->SetRootComponent(Box);
            Box->SetBoxExtent(FVector(4000, 4000, 50));
            Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Box->SetCollisionObjectType(ECC_WorldStatic);
            Box->SetCollisionResponseToAllChannels(ECR_Block);
            Box->RegisterComponent();
            Floor->SetActorLocation(FVector(0, 0, -100));
        }

        ~FWorld()
        {
            if (Run)
            {
                Run->Cancel();
            }
        }

        /** 保留真实DA全部数值与伤害GE，仅建立与原前端相同的初始来源/目标/几何事实。 */
        bool Start(const UPrototypeAbility& Ability, bool bFine, FString& Error)
        {
            const auto* Parameters = Ability.Prototype.Get();
            const bool bExtension = Ability.IsA<UExtensionAbility>();
            const bool bMixed = Ability.IsA<UPureMassAbility>();
            const FName Profile = bExtension ? TEXT("LegacyExtension.v1")
                                  : bMixed   ? TEXT("LegacyMixed.v1")
                                             : TEXT("LegacyActor.v1");
            auto* Definition = bFine ? AbilityPrototypeFineCompiler::Compile(World, Ability.Prototype, Profile, Error)
                                     : UAbilityLegacyGraphCompiler::Compile(World, Ability.Prototype, Profile, Error);
            if (!Definition)
            {
                return false;
            }
            Context = bExtension ? NewObject<UExtensionCastContext>(World) : NewObject<UPrototypeCastContext>(World);
            Context->Caster = Source;
            Context->Level = 1;
            Context->CastId = FGuid::NewGuid();
            Context->DamageParams.SourceAbilitySystemComponent = ASC;
            Context->DamageParams.WorldContextObject = Source;
            const auto* DamageClass = FindFProperty<FClassProperty>(Ability.GetClass(), TEXT("DamageEffectClass"));
            Context->DamageParams.DamageGameplayEffectClass =
                DamageClass ? Cast<UClass>(DamageClass->GetObjectPropertyValue_InContainer(&Ability)) : nullptr;
            if (!Context->DamageParams.DamageGameplayEffectClass || !Context->CaptureSourceLife(Error))
            {
                Error = TEXT("Actual damage GE and valid original source life are required.");
                return false;
            }
            FAbilityNodeInput Input;
            Input.Context = Context;
            Input.SourceToken = SourceToken;
            Input.DamageParams = Context->DamageParams;
            Input.LiveTargetToken = TargetToken;
            Input.Direction = FVector::ForwardVector;
            Input.Origin = Target->GetActorLocation();
            using K = EAbilityPrototypeKind;
            if (!bExtension && (Parameters->Kind == K::Cone || Parameters->Kind == K::Line ||
                                Parameters->Kind == K::Chain || Parameters->Kind == K::Orbit))
            {
                Input.Origin = Source->GetActorLocation();
            }
            if (!bExtension && (Parameters->Kind == K::Projectile || Parameters->Kind == K::Homing ||
                                Parameters->Kind == K::Piercing || Parameters->Kind == K::Multishot ||
                                Parameters->Kind == K::Returning))
            {
                Input.Origin = Source->GetActorLocation() + (bMixed ? FVector(0, 0, 30) : FVector(60, 0, 30));
                Input.Direction = (Target->GetActorLocation() - Input.Origin).GetSafeNormal();
            }
            if (bExtension)
            {
                Input.Origin = FVector::ZeroVector;
            }
            Input.EventCenter = Input.Origin;
            FAbilityExecutionOptions Options;
            Options.CompatibilityProfile = Definition->CompatibilityProfile;
            Options.BudgetPolicy = Definition->BudgetPolicy;
            Options.Lifetime = EAbilityExecutionLifetime::IndependentCastEffect;
            Run = NewObject<UAbilityGraphInstance>(World);
            return Run->StartWithInput(Definition, Input, Options, Error);
        }

        /** 记录实际属性和逻辑片段数；不把细节点数量当作原GA的命中次数。 */
        void Capture(TArray<double>& Trace)
        {
            Trace.Add(Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()));
            Trace.Add(ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()));
            Trace.Add(Context->Spawned);
            Trace.Add(Context->bCancelled);
            for (int32 Id : MassTargets)
            {
                FPureMassView View;
                const bool bLive = World->GetSubsystem<UPureMassSubsystem>()->Resolve(Id, View);
                Trace.Add(bLive ? View.Health : -1.f);
                Trace.Add(bLive ? View.Location.X : 0.f);
                Trace.Add(bLive ? View.Location.Y : 0.f);
            }
            int32 Terrain = 0;
            for (TActorIterator<ATerrainSurface> It(World); It; ++It)
            {
                Terrain += It->GetOwner() == Source && !It->IsActorBeingDestroyed() ? 1 : 0;
            }
            Trace.Add(Terrain);
        }

        /** 和生产Mass处理器相同，先推进已有位移再发布本帧查询快照；不调用图的世界Tick两次。 */
        void MoveAndPublish(float Delta)
        {
            auto* Mass = World->GetSubsystem<UPureMassSubsystem>();
            auto* State = World->GetSubsystem<UExtensionWorldSubsystem>();
            auto& Entities = World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
            if (Mass->BeginUpdate(Delta))
            {
                for (int32 Id : MassTargets)
                {
                    auto& Fragment = Entities.GetFragmentDataChecked<FPureMassFragment>(Mass->GetEntity(Id));
                    State->MoveMass(Fragment, Delta);
                    Mass->UpdateMonster(Fragment);
                }
                Mass->FinishUpdate();
            }
        }
    };
} // namespace AbilityRuntimeMageAssetParityTests

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FRuntimeMageActualAssetParity, "test_25d.DynamicMage.AssetParity.OriginalVersusFine",
                                  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FRuntimeMageActualAssetParity::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
    using namespace AbilityRuntimeMageAssetParityTests;
    for (UClass* AbilityClass : ActualClasses())
    {
        const auto* Ability = AbilityClass->GetDefaultObject<UPrototypeAbility>();
        const FString Profile = Ability->IsA<UExtensionAbility>()  ? TEXT("Extension")
                                : Ability->IsA<UPureMassAbility>() ? TEXT("Mixed")
                                                                   : TEXT("Actor");
        Names.Add(Profile + TEXT(".") + AbilityClass->GetName());
        Commands.Add(AbilityClass->GetPathName());
    }
}

bool FRuntimeMageActualAssetParity::RunTest(const FString& ClassPath)
{
    using namespace AbilityRuntimeMageAssetParityTests;
    auto* AbilityClass = LoadClass<UPrototypeAbility>(nullptr, *ClassPath);
    const auto* Ability = AbilityClass ? AbilityClass->GetDefaultObject<UPrototypeAbility>() : nullptr;
    if (!TestNotNull(TEXT("Exact actual catalog class"), Ability) ||
        !TestNotNull(TEXT("Exact original prototype asset"), Ability ? Ability->Prototype.Get() : nullptr))
    {
        return false;
    }
    AddInfo(TEXT("Actual action-layer asset: ") + ClassPath + TEXT(" | ") + Ability->Prototype->GetPathName());
    AddInfo(TEXT(
        "Evidence: real original parameters/GE and Actor/Mass attributes; frontend input, costs and network are separate cases."));
    TArray<double> Original;
    EAbilityGraphEndReason OriginalReason = EAbilityGraphEndReason::Failed;
    for (const bool bFine : {false, true})
    {
        FWorld F;
        FString Error;
        FMath::RandInit(195317);
        if (!TestTrue(FString(bFine ? TEXT("Fine default starts: ") : TEXT("Original default starts: ")) + Error,
                      F.Start(*Ability, bFine, Error)))
        {
            AddInfo(Error);
            return false;
        }
        TArray<double> Trace;
        F.Capture(Trace);
        // 固定75秒上界覆盖当前有限资产及子链；原通道/开关用10秒真实运行后显式取消作对照。
        const auto* Field = Cast<UBatch6FieldPrototype>(Ability->Prototype);
        const bool bSustained = Ability->Prototype->Kind == EAbilityPrototypeKind::Beam ||
                                Ability->Prototype->Kind == EAbilityPrototypeKind::Orbit ||
                                (Field && Field->bFollowCasterToggle && Ability->IsA<UPureMassAbility>());
        const int32 Steps = bSustained ? 200 : 1500;
        for (int32 Step = 0; Step < Steps; ++Step)
        {
            F.MoveAndPublish(.05f);
            ++GFrameCounter;
            F.Run->Advance(.05f);
            F.World->GetSubsystem<UExtensionWorldSubsystem>()->Tick(.05f);
            F.Capture(Trace);
            if (F.Run->IsFinished())
            {
                break;
            }
        }
        if (bSustained && !F.Run->IsFinished())
        {
            F.Run->Cancel();
        }
        TestTrue(TEXT("Actual default closes within its explicit test lifetime"), F.Run->IsFinished());
        AddInfo(FString::Printf(
            TEXT("Observed %s: terminal=%d traceValues=%d actorDamage=%.4f logical=%d contextCancelled=%d"),
            bFine ? TEXT("Fine") : TEXT("Original"), static_cast<int32>(F.Run->GetEndReason()), Trace.Num(),
            100000.f -
                F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()),
            F.Context->Spawned, F.Context->bCancelled));
        if (!bFine)
        {
            TestFalse(TEXT("Original actual default does not fail under this fixture's valid initial conditions"),
                      F.Run->GetEndReason() == EAbilityGraphEndReason::Failed ||
                          F.Run->GetEndReason() == EAbilityGraphEndReason::BudgetExceeded);
            Original = MoveTemp(Trace);
            OriginalReason = F.Run->GetEndReason();
        }
        else
        {
            TestTrue(TEXT("Exact original terminal reason is preserved"), F.Run->GetEndReason() == OriginalReason);
            if (TestEqual(TEXT("Per-frame actual trace has the same shape"), Trace.Num(), Original.Num()))
            {
                for (int32 Index = 0; Index < Trace.Num(); ++Index)
                {
                    if (!FMath::IsNearlyEqual(Original[Index], Trace[Index], .01))
                    {
                        AddError(FString::Printf(TEXT("Actual default trace differs at value %d: old %.4f fine %.4f"),
                                                 Index, Original[Index], Trace[Index]));
                        break;
                    }
                }
            }
        }
    }
    return true;
}
#endif
