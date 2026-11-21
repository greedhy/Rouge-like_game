// 调用链：真实 ASC 激活/目标数据 -> 基类配方冻结 -> UnifiedGraphAbility -> Mass 实际扣血与费用。
// 测试不保存 GA/DA；切换配方发生在等待鼠标目标期间，必须保留这一激活开始时的版本。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilityRuntimeRecipeFrontendTestTypes.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeComponent.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "PureMass/PureMassSubsystem.h"
#include "MyGameplayTags.h"
#include "UObject/Script.h"
#include "Player/MyPlayerState.h"

namespace AbilityRuntimeRecipeFrontendTests
{
    /** 使用远端目标任务分支隔离鼠标，仍运行实际服务器目标接收、Commit 和生产图任务。 */
    struct FFixture : FAbilityGraphTestFixture
    {
        FEditorScriptExecutionGuard Guard;
        UAbilityRuntimeRecipeFrontendTestAbility* Ability = nullptr;
        UAbilityRuntimeRecipeComponent* Recipes = nullptr;
        FGameplayAbilitySpecHandle Handle;
        UPureMassSubsystem* Mass = nullptr;
        int32 Id = INDEX_NONE;

        FFixture()
        {
            Target->Destroy();
            ASC->AbilityActorInfo->PlayerController = World->SpawnActor<AAbilityGraphRemoteTestController>();
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 100.f);
            Recipes = NewObject<UAbilityRuntimeRecipeComponent>(Source);
            Source->AddInstanceComponent(Recipes);
            Recipes->RegisterComponent();
            Mass = World->GetSubsystem<UPureMassSubsystem>();
            Mass->Enable();
            auto* Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = 100.f;
            Type->XPReward = 0;
            Id = Mass->SpawnMonster(Type, FVector(600, 0, 0));
            Handle = ASC->GiveAbility(FGameplayAbilitySpec(UAbilityRuntimeRecipeFrontendTestAbility::StaticClass(), 1));
            Ability = CastChecked<UAbilityRuntimeRecipeFrontendTestAbility>(
                ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
            auto* Base = NewObject<UAbilityGraphDefinition>(World);
            Base->SchemaVersion = 2;
            Base->CompatibilityProfile = TEXT("UnifiedV2");
            FAbilityGraphNode Query;
            Query.NodeId = FGuid::NewGuid();
            Query.TypeId = TEXT("Query.Shape");
            auto* Shape = NewObject<UAbilityQueryNodeConfig>(Base);
            Shape->Query.Radius = 150.f;
            Query.Config = Shape;
            FAbilityGraphNode Damage;
            Damage.NodeId = FGuid::NewGuid();
            Damage.TypeId = TEXT("Effect.Damage");
            auto* Effect = NewObject<UAbilityDamageNodeConfig>(Base);
            Effect->Damage = 10.f;
            Effect->DamagePerLevel = 0.f;
            Effect->DamageType = FMyGameplayTags::Get().Damage_Magic;
            Damage.Config = Effect;
            Base->Nodes = {Query, Damage};
            Base->EntryNodeId = Query.NodeId;
            FAbilityGraphEdge Edge;
            Edge.From = Query.NodeId;
            Edge.To = Damage.NodeId;
            Edge.OutputPort = TEXT("TargetFound");
            Base->Edges.Add(Edge);
            Ability->Graph = Base;
            Ability->TargetMode = EUnifiedAbilityTargetMode::Ground;
        }

        ~FFixture()
        {
            ASC->CancelAllAbilities();
            ASC->ClearAllAbilities();
        }

        /** 更新组件的本来源贡献；不直接写运行图或属性。 */
        bool SetScale(float Scale, FString& Error)
        {
            FAbilityRuntimeRecipeContribution Contribution;
            Contribution.SourceId = TEXT("FrontendTest");
            FAbilityRuntimeRecipePatch Patch;
            Patch.TargetTypeId = TEXT("Effect.Damage");
            Patch.Operation = EAbilityRecipePatchOperation::DamageScale;
            Patch.Scalar = Scale;
            Contribution.Patches.Add(Patch);
            return Recipes->SetContribution(FGameplayTag::RequestGameplayTag(TEXT("Abilities.Prototype.Burst")),
                                            Contribution, Error);
        }

        /** 沿用正式 GAS TargetData 协议，服务器检查位置后才执行施法。 */
        bool Activate()
        {
            // 该夹具显式模拟远端控制器；直接 TryActivateAbility 会只请求远端激活而不进入服务器实例。
            FPredictionKey Key;
            Key.Current = 211;
            ASC->CallServerTryActivateAbility(Handle, false, Key);
            return Ability->IsActive();
        }

        /** 向已进入等待状态的真实服务器实例提交目标数据。 */
        void SendTarget()
        {
            auto* Hit = new FGameplayAbilityTargetData_SingleTargetHit();
            Hit->HitResult.bBlockingHit = true;
            Hit->HitResult.ImpactPoint = FVector(600, 0, 0);
            // FHitResult 默认 Item=0；协议以 INDEX_NONE 表示纯地面，0 会被当作非法实体 ID 拒绝。
            Hit->HitResult.Item = INDEX_NONE;
            FGameplayAbilityTargetDataHandle Data;
            Data.Add(Hit);
            ASC->ServerSetReplicatedTargetData(Handle, Ability->GetCurrentActivationInfo().GetActivationPredictionKey(),
                                               Data, FGameplayTag(), FPredictionKey());
            Ability->StepGraph(0.f);
        }

        float Health() const
        {
            FPureMassView View;
            return Mass->Resolve(Id, View) ? View.Health : 0.f;
        }
    };
} // namespace AbilityRuntimeRecipeFrontendTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeFrontendFreeze,
                                 "test_25d.UnifiedGA.DynamicMage.Frontend.FreezesBeforeTargetData",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipeFrontendFreeze::RunTest(const FString&)
{
    AbilityRuntimeRecipeFrontendTests::FFixture F;
    FString Error;
    const auto Identity = FGameplayTag::RequestGameplayTag(TEXT("Abilities.Prototype.Burst"));
    if (!TestTrue(TEXT("Register actual graph base"), F.Recipes->RegisterBase(Identity, F.Ability->Graph, Error)) ||
        !TestTrue(TEXT("Set first runtime contribution"), F.SetScale(2.f, Error)))
    {
        return false;
    }
    TestTrue(TEXT("Actual frontend begins activation"), F.Activate());
    const auto Frozen = F.Ability->GetRuntimeRecipeSnapshot();
    TestNotNull(TEXT("Activation freezes recipe before target returns"), Frozen.Graph.Get());
    TestEqual(TEXT("Frozen revision is the accepted component revision"), Frozen.Revision,
              F.Recipes->GetRevision(Identity));
    TestTrue(TEXT("Edit future recipe while waiting"), F.SetScale(4.f, Error));
    TestEqual(TEXT("Waiting activation does not adopt new revision"), F.Ability->GetRuntimeRecipeSnapshot().Revision,
              Frozen.Revision);
    F.SendTarget();
    TestEqual(TEXT("Current cast uses frozen x2 damage, not future x4 or original x1"), F.Health(), 80.f);
    TestEqual(TEXT("Real frontend commits once"), F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()),
              90.f);
    TestFalse(TEXT("Immediate graph finishes its ability"), F.Ability->IsActive());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeFrontendOriginal,
                                 "test_25d.UnifiedGA.DynamicMage.Frontend.NoContributionUsesOriginal",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipeFrontendOriginal::RunTest(const FString&)
{
    AbilityRuntimeRecipeFrontendTests::FFixture F;
    TestTrue(TEXT("Original frontend activates with no recipes"), F.Activate());
    TestNull(TEXT("No contribution retains the original path"), F.Ability->GetRuntimeRecipeSnapshot().Graph.Get());
    F.SendTarget();
    TestEqual(TEXT("Original graph damage remains unchanged"), F.Health(), 90.f);
    TestEqual(TEXT("Original cost remains unchanged"), F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()),
              90.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipePlayerStateOwnership,
                                 "test_25d.UnifiedGA.DynamicMage.Frontend.PlayerStateOwnsRecipeComponent",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipePlayerStateOwnership::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* State = F.World->SpawnActor<AMyPlayerState>();
    const auto* Component = State->FindComponentByClass<UAbilityRuntimeRecipeComponent>();
    if (!TestNotNull(TEXT("Existing persistent PlayerState owns recipe configuration"), Component))
    {
        return false;
    }
    TestEqual(TEXT("Recipe component belongs to ASC owner, not one attack actor"), Component->GetOwner(),
              static_cast<AActor*>(State));
    TestFalse(TEXT("Recipe compilation requires no component tick"), Component->PrimaryComponentTick.bCanEverTick);
    return true;
}
#endif
