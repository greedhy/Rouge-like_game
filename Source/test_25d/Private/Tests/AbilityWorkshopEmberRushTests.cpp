// 测试链：真实目录与 T 键 → 目标 RPC → 位移/地表图 → 实际胶囊位置、地块和移动模式。
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AbilityActualMageFrontendFixture.h"
#include "AbilitySystem/Unified/Nodes/AbilityMovementNodeExecutions.h"
#include "AbilitySystem/Unified/WorkshopUtilityAbilities.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Terrain/TerrainSurface.h"

namespace
{
    /** 创建真实碰撞体。墙只拦胶囊扫掠，以便测试不会先因鼠标目标视线被拒绝。 */
    void AddRushCollision(UWorld& World, const FVector& Position, const FVector& Extent, bool bWall)
    {
        auto* Actor = World.SpawnActor<AActor>();
        auto* Box = NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Box);
        Actor->AddInstanceComponent(Box);
        Box->SetBoxExtent(Extent);
        Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Box->SetCollisionObjectType(ECC_WorldStatic);
        Box->SetCollisionResponseToAllChannels(ECR_Block);
        if (bWall)
        {
            Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
        }
        Box->RegisterComponent();
        Actor->SetActorLocation(Position);
    }

    /** 使用真实角色胶囊、常态移动模式和正式试装目录启动。 */
    bool BeginRushPreview(FAbilityActualMageFrontendFixture& Fixture, FString& Error)
    {
        Fixture.Target->SetActorLocation(FVector(0, 1000, 0));
        const float GroundZ = Fixture.Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f;
        Fixture.Pawn->SetActorLocation(FVector(0, 0, GroundZ));
        Fixture.Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        AddRushCollision(*Fixture.World, FVector(0, 0, -10), FVector(1500, 1500, 10), false);
        const auto* Definition = Fixture.Definition(TEXT("/Script/test_25d.WorkshopEmberRush"), Error);
        if (!Definition)
        {
            return false;
        }
        Fixture.Developer->ServerBeginCardPreview(Definition->DefinitionId, FGuid::NewGuid());
        return Fixture.Developer->GetPreviewHandle().IsValid();
    }

    /** 用真实服务器草稿事务同时安装距离、时间和地表类型。 */
    bool ApplyRushCards(FAbilityActualMageFrontendFixture& Fixture, FString& Error)
    {
        auto& Developer = *Fixture.Developer;
        const auto& State = Developer.GetDeveloperState();
        for (const FName Card :
             {FName(TEXT("LongEmberRush")), FName(TEXT("QuickEmberRush")), FName(TEXT("FrostRushFloor"))})
        {
            if (!Developer.PreviewCard(Card, State.PreviewSessionId, State.Revision, Error) ||
                !Developer.ApplyGraphDraft(State.DraftToken, State.Revision, Error))
            {
                Error = Card.ToString() + TEXT(": ") + Error;
                return false;
            }
        }
        return true;
    }

    /** 无视口测试仍走 T 输入和真实权威 TargetData 接收，客户端预测仅保留原入口。 */
    bool CastRush(FAbilityActualMageFrontendFixture& Fixture, const FVector& Ground)
    {
        Fixture.Key(IE_Pressed);
        const auto Handle = Fixture.Developer->GetPreviewHandle();
        const auto* Spec = Fixture.PlayerASC->FindAbilitySpecFromHandle(Handle);
        const auto* Ability = Spec ? Cast<UMageGraphAbility>(Spec->GetPrimaryInstance()) : nullptr;
        if (!Spec || !Spec->InputPressed || !Ability)
        {
            return false;
        }
        if (!Ability->IsActive())
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
        Hit->HitResult.Location = Hit->HitResult.ImpactPoint = Ground;
        Hit->HitResult.Item = INDEX_NONE;
        FGameplayAbilityTargetDataHandle Data;
        Data.Add(Hit);
        Fixture.PlayerASC->ServerSetReplicatedTargetData(
            Handle, Ability->GetCurrentActivationInfo().GetActivationPredictionKey(), Data, FGameplayTag(),
            FPredictionKey());
        Fixture.Key(IE_Released);
        return true;
    }

    /** 只读取本次测试角色实际拥有的临时地表。 */
    ATerrainSurface* FindRushSurface(const FAbilityActualMageFrontendFixture& Fixture)
    {
        for (TActorIterator<ATerrainSurface> It(Fixture.World); It; ++It)
        {
            if (!It->IsActorBeingDestroyed() && It->GetOwner() == Fixture.Pawn)
            {
                return *It;
            }
        }
        return nullptr;
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopEmberRushInput, "test_25d.UnifiedGA.DynamicMage.PlayerCards.EmberRush",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 验证近端约束、即时受阻取消、部分移动落点和试装退出释放移动。 */
bool FWorkshopEmberRushInput::RunTest(const FString&)
{
    FString Error;
    {
        FAbilityActualMageFrontendFixture Fixture;
        if (!TestTrue(TEXT("Open the real rush and inspect its authored movement edge"),
                      BeginRushPreview(Fixture, Error)))
        {
            AddInfo(Error);
            return false;
        }
        const auto* Definition = Fixture.Definition(TEXT("/Script/test_25d.WorkshopEmberRush"), Error);
        auto* Graph = DuplicateObject<UAbilityGraphDefinition>(Definition->Base.DefaultGraph.Get(), Fixture.World);
        auto* Move = CastChecked<UAbilityCharacterMoveNodeConfig>(Graph->Nodes[0].Config);
        Move->bUseGroundTarget = false;
        TArray<FString> Errors;
        TestFalse(TEXT("Authoring rejects a Landed edge on the old direction-only movement profile"),
                  Graph->ValidateForAuthoring(Errors));

        Fixture.Pawn->GetCharacterMovement()->DisableMovement();
        const auto Mana = UMyAttributeSet::GetManaAttribute();
        const float StartingMana = Fixture.PlayerASC->GetNumericAttribute(Mana);
        TestFalse(TEXT("Externally disabled movement makes the real input ineligible"),
                  CastRush(Fixture, FVector(240, 0, 0)));
        TestEqual(TEXT("Ineligible movement does not spend mana"), Fixture.PlayerASC->GetNumericAttribute(Mana),
                  StartingMana);
        TestNull(TEXT("Ineligible movement creates no ground"), FindRushSurface(Fixture));
    }
    {
        FAbilityActualMageFrontendFixture Fixture;
        if (!TestTrue(TEXT("The real T binding starts Ember Rush"),
                      BeginRushPreview(Fixture, Error) && CastRush(Fixture, FVector(240, 0, 0))))
        {
            AddInfo(Error);
            return false;
        }
        const auto Mana = UMyAttributeSet::GetManaAttribute();
        TestEqual(TEXT("The cast commits the default mana once"), Fixture.PlayerASC->GetNumericAttribute(Mana), 9984.f);
        TestTrue(TEXT("Movement owns the actual avatar"),
                 UAbilityMovementOwnershipComponent::HasActiveClaim(Fixture.Pawn));
        Fixture.Step(.08f);
        TestNull(TEXT("The landing effect waits for the actual movement to finish"), FindRushSurface(Fixture));
        Fixture.Step(.25f);
        const FVector End = Fixture.Pawn->GetActorLocation();
        TestTrue(TEXT("A nearer cursor clamps the dash"), FMath::IsNearlyEqual(End.X, 240.0, 1.0));
        if (const auto* Surface = FindRushSurface(Fixture))
        {
            TestTrue(TEXT("The landed Lava is projected below the actual capsule"),
                     Surface->Config && Surface->Config->SurfaceType == ETerrainSurfaceType::Lava &&
                         Surface->GetActorLocation().Equals(FVector(End.X, End.Y, 0), 1.f));
        }
        else
        {
            AddError(TEXT("The unobstructed rush should create a real Lava patch."));
        }
        TestFalse(TEXT("Normal finish returns movement ownership"),
                  UAbilityMovementOwnershipComponent::HasActiveClaim(Fixture.Pawn));
    }
    {
        FAbilityActualMageFrontendFixture Fixture;
        if (!BeginRushPreview(Fixture, Error))
        {
            AddError(Error);
            return false;
        }
        AddRushCollision(*Fixture.World, FVector(200, 0, 100), FVector(10, 300, 100), true);
        TestTrue(TEXT("Real input reaches the sweep even through a non-occluding blocker"),
                 CastRush(Fixture, FVector(700, 0, 0)));
        Fixture.Step(.35f);
        const FVector End = Fixture.Pawn->GetActorLocation();
        TestTrue(TEXT("The capsule dashes partially and never crosses the wall"), End.X > 0. && End.X < 190.);
        const auto* Surface = FindRushSurface(Fixture);
        TestTrue(TEXT("The effect is at the wall-limited stopping point"),
                 Surface && Surface->GetActorLocation().Equals(FVector(End.X, End.Y, 0), 1.f));
        TestEqual(TEXT("The movement mode is restored after the blocker"),
                  Fixture.Pawn->GetCharacterMovement()->MovementMode.GetValue(), MOVE_Walking);
    }
    {
        FAbilityActualMageFrontendFixture Fixture;
        if (!BeginRushPreview(Fixture, Error))
        {
            AddError(Error);
            return false;
        }
        TestTrue(TEXT("The input can aim directly beneath the player"), CastRush(Fixture, FVector::ZeroVector));
        Fixture.Step(.35f);
        TestTrue(TEXT("A zero cursor displacement never moves"),
                 FMath::IsNearlyZero(Fixture.Pawn->GetActorLocation().X));
        TestNull(TEXT("No ground is generated when the player did not move"), FindRushSurface(Fixture));
    }
    {
        FAbilityActualMageFrontendFixture Fixture;
        if (!BeginRushPreview(Fixture, Error) || !CastRush(Fixture, FVector(600, 0, 0)) ||
            !Fixture.Developer->EndPreview(Error))
        {
            AddError(TEXT("Should be able to stop Ember Rush through its owning preview. ") + Error);
            return false;
        }
        Fixture.Step(.35f);
        TestFalse(TEXT("Cancellation releases the movement ownership"),
                  UAbilityMovementOwnershipComponent::HasActiveClaim(Fixture.Pawn));
        TestNull(TEXT("Cancellation cannot publish a late landing"), FindRushSurface(Fixture));
    }
    {
        FAbilityActualMageFrontendFixture Fixture;
        if (!BeginRushPreview(Fixture, Error) || !ApplyRushCards(Fixture, Error) ||
            !CastRush(Fixture, FVector(1000, 0, 0)))
        {
            AddError(TEXT("Install the distance, speed and frost-floor cards together. ") + Error);
            return false;
        }
        Fixture.Step(.22f);
        const auto* Surface = FindRushSurface(Fixture);
        TestTrue(TEXT("The longer move ends in the shorter edited duration"),
                 FMath::IsNearlyEqual(Fixture.Pawn->GetActorLocation().X, 850.0, 1.0) &&
                     !UAbilityMovementOwnershipComponent::HasActiveClaim(Fixture.Pawn));
        TestTrue(TEXT("The same frozen graph creates the independent edited Frost patch"),
                 Surface && Surface->Config && Surface->Config->SurfaceType == ETerrainSurfaceType::Frost);
    }
    return true;
}

#endif
