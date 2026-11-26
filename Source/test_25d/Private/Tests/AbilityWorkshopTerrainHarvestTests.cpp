// 测试链：原接触地表 -> 原生技能目录与卡片 -> 玩家 T 键和目标数据 -> 租约清理、护盾、恢复及取消。
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AbilityActualMageFrontendFixture.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/Nodes/AbilityWorkshopTerrainNodes.h"
#include "AbilitySystem/Unified/WorkshopTerrainAbilities.h"
#include "PureMass/PureMassIncomingDamageEffect.h"
#include "Terrain/TerrainSurface.h"

namespace
{
    const FVector Aim(200.f, 0.f, 0.f);

    /** 用真实世界地表节点布置用例，保留原蓝图资源与租约。 */
    TWeakObjectPtr<ATerrainSurface> PlaceTerrain(FAbilityActualMageFrontendFixture& Fixture, ETerrainSurfaceType Kind,
                                                 const FVector& Point, FString& Error)
    {
        auto* Graph = DuplicateObject(GetDefault<UWorkshopVenomMire>()->Graph.Get(), Fixture.World);
        auto* Config = CastChecked<UAbilityElementSurfaceNodeConfig>(Graph->Nodes[0].Config);
        Config->Kind = Kind;
        Config->Duration = 20.f;
        Config->Radius = 30.f;

        FAbilityNodeInput Input;
        Input.Origin = Input.EventCenter = Point;
        if (!AbilityExecutionIdentity::CaptureSource(Fixture.Pawn, Input.SourceToken, Error))
        {
            return nullptr;
        }
        auto* Instance = NewObject<UAbilityGraphInstance>(Fixture.World);
        if (!Instance->StartWithInput(Graph, Input, {}, Error))
        {
            return nullptr;
        }
        const auto Tiles = AbilityWorkshopTerrainNodes::FindOwnedSurfaces(*Fixture.Pawn, Point, 25.f, Kind, 1);
        return Tiles.IsEmpty() ? TWeakObjectPtr<ATerrainSurface>() : Tiles[0];
    }

    /** 通过第四页的日常会话和草稿入口逐张提交，保留准确服务器快照。 */
    bool BeginHarvest(FAbilityActualMageFrontendFixture& Fixture, const TCHAR* Path, TArrayView<const FName> Cards,
                      FString& Error)
    {
        const auto* Definition = Fixture.Definition(Path, Error);
        if (!Definition)
        {
            Error = FString::Printf(TEXT("No harvest definition for %s. %s"), Path, *Error);
            return false;
        }
        auto& Developer = *Fixture.Developer;
        const FGuid Session = FGuid::NewGuid();
        Developer.ServerBeginCardPreview(Definition->DefinitionId, Session);
        if (!Developer.GetPreviewHandle().IsValid())
        {
            Error = TEXT("The harvest trial did not grant a temporary spec.");
            return false;
        }
        for (const FName Card : Cards)
        {
            const auto& State = Developer.GetDeveloperState();
            if (!Developer.PreviewCard(Card, Session, State.Revision, Error) ||
                !Developer.ApplyGraphDraft(State.DraftToken, State.Revision, Error))
            {
                return false;
            }
        }
        return true;
    }

    /** 无视口夹具经原目标任务的 RPC 提交落点。服务器继续执行真实校验和 Commit。 */
    bool CastHarvest(FAbilityActualMageFrontendFixture& Fixture)
    {
        Fixture.Key(IE_Pressed);
        const auto Handle = Fixture.Developer->GetPreviewHandle();
        const auto* Spec = Fixture.PlayerASC->FindAbilitySpecFromHandle(Handle);
        const auto* Ability = Spec ? Cast<UMageGraphAbility>(Spec->GetPrimaryInstance()) : nullptr;
        if (!Spec || !Spec->InputPressed || !Ability)
        {
            return false;
        }
        if (!Ability->IsActive() &&
            Ability->GetNetExecutionPolicy() == EGameplayAbilityNetExecutionPolicy::LocalPredicted)
        {
            FPredictionKey Prediction;
            Prediction.Current = 1401;
            Fixture.PlayerASC->CallServerTryActivateAbility(Handle, Spec->InputPressed, Prediction);
        }
        if (!Ability->IsActive())
        {
            return false;
        }

        auto* Hit = new FGameplayAbilityTargetData_SingleTargetHit();
        Hit->HitResult.bBlockingHit = true;
        Hit->HitResult.Location = Hit->HitResult.ImpactPoint = Aim;
        Hit->HitResult.Item = INDEX_NONE;
        FGameplayAbilityTargetDataHandle Data;
        Data.Add(Hit);
        Fixture.PlayerASC->ServerSetReplicatedTargetData(
            Handle, Ability->GetCurrentActivationInfo().GetActivationPredictionKey(), Data, FGameplayTag(),
            FPredictionKey());
        Fixture.Key(IE_Released);
        Fixture.Step(.05f);
        return true;
    }

    /** 权威伤害 GE 读取玩家护盾实际拦下的伤害。 */
    void DamageCaster(FAbilityActualMageFrontendFixture& Fixture, float Amount)
    {
        auto* EnemyASC = Fixture.Target->GetAbilitySystemComponent();
        auto Spec = EnemyASC->MakeOutgoingSpec(UPureMassIncomingDamageEffect::StaticClass(), 1.f,
                                               EnemyASC->MakeEffectContext());
        Spec.Data->SetSetByCallerMagnitude(UPureMassIncomingDamageEffect::GetDamageTag(), Amount);
        EnemyASC->ApplyGameplayEffectSpecToTarget(*Spec.Data, Fixture.PlayerASC);
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopFrostHarvestCast, "test_25d.UnifiedGA.WorkshopTerrainHarvest.FrostShield",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 空放仍按前端提交费用，只有实际回收的一个同层地块才授予护盾。 */
bool FWorkshopFrostHarvestCast::RunTest(const FString&)
{
    FAbilityActualMageFrontendFixture Fixture;
    FString Error;
    const auto Near = PlaceTerrain(Fixture, ETerrainSurfaceType::Frost, Aim + FVector(420.f, 0.f, 0.f), Error);
    const auto Far = PlaceTerrain(Fixture, ETerrainSurfaceType::Frost, Aim + FVector(490.f, 0.f, 0.f), Error);
    const auto AnotherFloor = PlaceTerrain(Fixture, ETerrainSurfaceType::Frost, Aim + FVector(0.f, 0.f, 300.f), Error);
    const auto WrongKind = PlaceTerrain(Fixture, ETerrainSurfaceType::Toxic, Aim + FVector(75.f, 0.f, 0.f), Error);
    if (!TestTrue(TEXT("All four real surface leases exist"),
                  Near.IsValid() && Far.IsValid() && AnotherFloor.IsValid() && WrongKind.IsValid()) ||
        !TestTrue(TEXT("The default harvest casts through its actual ground input"),
                  BeginHarvest(Fixture, TEXT("/Script/test_25d.WorkshopFrostHarvest"), {}, Error) &&
                      CastHarvest(Fixture)))
    {
        AddInfo(Error);
        return false;
    }
    const auto Health = UMyAttributeSet::GetHealthAttribute();
    const auto Mana = UMyAttributeSet::GetManaAttribute();
    Fixture.PlayerASC->SetNumericAttributeBase(Health, 100.f);
    DamageCaster(Fixture, 10.f);
    TestEqual(TEXT("No frost in radius on the same floor gives no shield"),
              Fixture.PlayerASC->GetNumericAttribute(Health), 90.f);
    TestEqual(TEXT("A missed harvest still commits once"), Fixture.PlayerASC->GetNumericAttribute(Mana), 9986.f);
    TestTrue(TEXT("The miss did not remove any terrain"),
             Near.IsValid() && Far.IsValid() && AnotherFloor.IsValid() && WrongKind.IsValid());
    if (!TestTrue(TEXT("Close the first recipe before trying new cards"), Fixture.Developer->EndPreview(Error)))
    {
        AddInfo(Error);
        return false;
    }
    Fixture.Step(7.1f);
    const FName Cards[] = {TEXT("WideHarvest"), TEXT("HeavyBarrier")};
    if (!TestTrue(TEXT("The harvest range and shield independently accept live cards"),
                  BeginHarvest(Fixture, TEXT("/Script/test_25d.WorkshopFrostHarvest"), Cards, Error) &&
                      CastHarvest(Fixture)))
    {
        AddInfo(Error);
        return false;
    }
    TestTrue(TEXT("Only the nearest same-floor frost lease is retired"),
             (!Near.IsValid() || Near->IsActorBeingDestroyed()) && Far.IsValid() && AnotherFloor.IsValid() &&
                 WrongKind.IsValid());
    DamageCaster(Fixture, 70.f);
    TestEqual(TEXT("The adjusted output uses the real sixty-point shield"),
              Fixture.PlayerASC->GetNumericAttribute(Health), 80.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopBogHarvestCast, "test_25d.UnifiedGA.WorkshopTerrainHarvest.BogHealing",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 回收同类地块后才治疗；编辑后的运行期节拍随本次试装取消。 */
bool FWorkshopBogHarvestCast::RunTest(const FString&)
{
    FAbilityActualMageFrontendFixture Fixture;
    FString Error;
    const auto Toxic = PlaceTerrain(Fixture, ETerrainSurfaceType::Toxic, Aim, Error);
    if (!TestTrue(TEXT("A genuine Toxic lease is in reach"), Toxic.IsValid()))
    {
        AddInfo(Error);
        return false;
    }
    const auto Health = UMyAttributeSet::GetHealthAttribute();
    const auto Mana = UMyAttributeSet::GetManaAttribute();
    Fixture.PlayerASC->SetNumericAttributeBase(Health, 100.f);
    const FName Cards[] = {TEXT("ExtraTides"), TEXT("DeepMend")};
    if (!TestTrue(TEXT("The actual trial applies cadence and heal amount independently"),
                  BeginHarvest(Fixture, TEXT("/Script/test_25d.WorkshopBogHarvest"), Cards, Error) &&
                      CastHarvest(Fixture)))
    {
        AddInfo(Error);
        return false;
    }
    TestTrue(TEXT("The Toxic is consumed by the real dispel path"), !Toxic.IsValid() || Toxic->IsActorBeingDestroyed());
    TestEqual(TEXT("The adjusted first heal is immediate"), Fixture.PlayerASC->GetNumericAttribute(Health), 115.f);
    TestEqual(TEXT("The graph pays only the ability cost"), Fixture.PlayerASC->GetNumericAttribute(Mana), 9988.f);
    Fixture.Step(.13f);
    TestEqual(TEXT("No second heal before the modified interval"), Fixture.PlayerASC->GetNumericAttribute(Health),
              115.f);
    Fixture.Step(.24f);
    TestEqual(TEXT("A second pulse happens before the old interval"), Fixture.PlayerASC->GetNumericAttribute(Health),
              130.f);
    TestTrue(TEXT("Closing the trial cancels the remaining healing pulses"), Fixture.Developer->EndPreview(Error));
    Fixture.Step(1.f);
    TestEqual(TEXT("The cancelled graph leaves no pending heal"), Fixture.PlayerASC->GetNumericAttribute(Health),
              130.f);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
