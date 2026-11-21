// 测试链：真实Emitter目录/预览输入 -> 可信端口分支替换 -> 新Beam私有帧 -> 独立查询/伤害与纯表现宿主。
// 静态宿主对照先证明数量/取消，再允许审过的fragment进入runtime-finite合同；不更改任何原GA/DA。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityActualMageFrontendFixture.h"
#include "AbilityArcaneShardTestTypes.h"
#include "AbilityPrototypeNiagaraTestFixture.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityPrototypeFineCompiler.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "PureMass/PureMassRuntime.h"
#include "PureMass/PureMassAbility.h"
#include "UObject/UnrealType.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"

namespace AbilityEmitterBranchReplacementTests
{
    /** 使用当前正式目录实际存在的根Emitter；不手工RegisterBase替换目录身份。 */
    const FAbilityRuntimeDeveloperDefinition* Find(FAbilityActualMageFrontendFixture& F, FString& Error)
    {
        if (!F.Developer->InitializeCatalog(nullptr, Error))
        {
            return nullptr;
        }
        for (const bool bMixed : {true, false})
        {
            for (const auto& Row : F.Developer->GetDefinitions())
            {
                if (!Row.Base.bSelectable || !Row.Base.DefaultGraph || !Row.Base.AbilityClass ||
                    !Row.Base.AbilityClass->IsChildOf<UPrototypeAbility>() ||
                    Row.Base.AbilityClass->IsChildOf<UPureMassAbility>() != bMixed)
                {
                    continue;
                }
                const auto* Entry = Row.Base.DefaultGraph->Nodes.FindByPredicate(
                    [&](const auto& Node)
                    {
                        return Node.NodeId == Row.Base.DefaultGraph->EntryNodeId;
                    });
                const auto* Scope = Entry ? Cast<UAbilityPrototypeScopeNodeConfig>(Entry->Config) : nullptr;
                if (Scope && Scope->Parameters && Scope->Parameters->Kind == EAbilityPrototypeKind::Emitter &&
                    (Scope->Profile == TEXT("PrototypeFine.Actor.v1") ||
                     Scope->Profile == TEXT("PrototypeFine.Mixed.v1")))
                {
                    return &Row;
                }
            }
        }
        Error = TEXT("Current real catalogs contain no selectable root PrototypeEmitter");
        return nullptr;
    }

    /** 测试只读反射复制快照，按真实宿主Frozen.Kind计数；不用transition统计代替Actor数量。 */
    const FAbilityPrototypeVisualSnapshot* Snapshot(AActor& Actor)
    {
        const auto* Property = FindFProperty<FStructProperty>(Actor.GetClass(), TEXT("PresentationSnapshot"));
        return Property ? Property->ContainerPtrToValuePtr<FAbilityPrototypeVisualSnapshot>(&Actor) : nullptr;
    }

    /** 一次步进后枚举实际Runtime Actor，记录历史出生和当前活跃；来源的Driver必须已被纯模式隔离。 */
    int32 Observe(FAbilityActualMageFrontendFixture& F, TSet<TWeakObjectPtr<AActor>>& Beams, bool& bAllPure,
                  int32& OutUnexpectedFlights)
    {
        int32 Active = 0;
        for (TActorIterator<AActor> It(F.World); It; ++It)
        {
            const auto* Values = Snapshot(**It);
            if (!Values || !Values->bReady)
            {
                continue;
            }
            const auto Kind = Values->Kind;
            if (Kind == EAbilityPrototypeKind::Projectile || Kind == EAbilityPrototypeKind::Multishot ||
                Kind == EAbilityPrototypeKind::Homing || Kind == EAbilityPrototypeKind::Piercing ||
                Kind == EAbilityPrototypeKind::Returning)
            {
                ++OutUnexpectedFlights;
            }
            if (Kind != EAbilityPrototypeKind::Beam)
            {
                continue;
            }
            Beams.Add(*It);
            if (const auto* Host = Cast<APrototypeRuntime>(*It))
            {
                bAllPure &= !Host->GetDriver().IsValid();
                Active += !Host->IsFinished() ? 1 : 0;
            }
            else if (const auto* MixedHost = Cast<APureMassRuntime>(*It))
            {
                bAllPure &= !MixedHost->GetDriver().IsValid();
                Active += !MixedHost->IsFinished() ? 1 : 0;
            }
        }
        return Active;
    }

    /** 新片段是私有冻结Beam数据，原Emitter及其原孩子不被改种类或写回。 */
    UAbilityPrototype* Beam(UObject* Outer)
    {
        auto* Values = NewObject<UAbilityPrototype>(Outer);
        Values->Kind = EAbilityPrototypeKind::Beam;
        Values->Damage = 100.f;
        Values->DamagePerLevel = 0.f;
        Values->DamageType = FMyGameplayTags::Get().Damage_Fire;
        Values->Duration = .2f;
        Values->Period = .1f;
        Values->Range = 400.f;
        Values->Radius = 50.f;
        Values->Width = 100.f;
        Values->ShapeHalfHeight = 100.f;
        Values->Cooldown = Values->ManaCost = Values->ManaPerSecond = 0.f;
        return Values;
    }

    /** 所有请求仍走开发事务；失败保持上一Revision/Hash，而不是直接替换共享图。 */
    bool Edit(FAbilityActualMageFrontendFixture& F, const FAbilityRuntimeDeveloperDefinition& Row, int32 NodeIndex,
              FName Modifier, float Value, FString& Error, FName Fragment = NAME_None, FName Port = NAME_None)
    {
        FAbilityRuntimeDeveloperEdit Request;
        Request.DefinitionId = Row.DefinitionId;
        Request.NodeIndex = NodeIndex;
        Request.ModifierId = Modifier;
        Request.FragmentId = Fragment;
        Request.OutputPortId = Port;
        Request.Value = Value;
        Request.Interval = .1f;
        Request.Radius = 400.f;
        Request.ExpectedRevision = F.Developer->GetDeveloperState().Revision;
        Request.ExpectedBaseHash = Row.BaseHash;
        return F.Developer->ApplyEdit(Request, Error);
    }

    /** 原输入/原网络策略激活；目标包提交给GAS缓存，不调用私有Execute或直接启动展示图。 */
    bool CastPreview(FAbilityActualMageFrontendFixture& F, UPrototypeAbility& Ability)
    {
        F.Key(IE_Pressed);
        auto* Spec = F.PlayerASC->FindAbilitySpecFromHandle(F.Handle);
        if (!Spec)
        {
            return false;
        }
        if (!Ability.IsActive() &&
            Ability.GetNetExecutionPolicy() == EGameplayAbilityNetExecutionPolicy::LocalPredicted)
        {
            FPredictionKey Prediction;
            Prediction.Current = 3141;
            F.PlayerASC->CallServerTryActivateAbility(F.Handle, Spec->InputPressed, Prediction);
        }
        if (!Ability.Prototype->bTargetSelf && !Ability.IsInputToggleAbility())
        {
            auto* Hit = new FGameplayAbilityTargetData_SingleTargetHit();
            Hit->HitResult.bBlockingHit = true;
            Hit->HitResult.Item = INDEX_NONE;
            Hit->HitResult.HitObjectHandle = FActorInstanceHandle(F.Target);
            Hit->HitResult.Location = Hit->HitResult.ImpactPoint = F.Target->GetActorLocation();
            FGameplayAbilityTargetDataHandle Data;
            Data.Add(Hit);
            F.PlayerASC->ServerSetReplicatedTargetData(F.Handle,
                                                       Ability.GetCurrentActivationInfo().GetActivationPredictionKey(),
                                                       Data, FGameplayTag(), FPredictionKey());
        }
        return Ability.IsActive() ||
               F.World->GetSubsystem<UAbilityGraphWorldSubsystem>()->GetActiveExecutionCount() > 0;
    }
} // namespace AbilityEmitterBranchReplacementTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEmitterBeamHostControl,
                                 "test_25d.DynamicMage.EmitterReplacement.BoundedBeamHostsAndCancellationControl",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEmitterBeamHostControl::RunTest(const FString&)
{
    using namespace AbilityEmitterBranchReplacementTests;
    for (const bool bCancel : {false, true})
    {
        FAbilityActualMageFrontendFixture F;
        FString Error;
        const auto* Row = Find(F, Error);
        if (!TestNotNull(TEXT("Real current root Emitter directory is available"), Row))
        {
            AddError(Error);
            return false;
        }
        const auto* Original = Row->Base.AbilityClass->GetDefaultObject<UPrototypeAbility>()->Prototype.Get();
        auto* RootCopy = DuplicateObject<UAbilityPrototype>(Original, F.World);
        RootCopy->Count = 3;
        RootCopy->Period = .1f;
        RootCopy->Duration = 1.f;
        RootCopy->OnPeriod = Beam(RootCopy);
        RootCopy->OnHit = RootCopy->OnExpire = nullptr;
        const auto Profile = Row->Base.AbilityClass->IsChildOf<UPureMassAbility>() ? FName(TEXT("LegacyMixed.v1"))
                                                                                   : FName(TEXT("LegacyActor.v1"));
        auto* Definition = AbilityPrototypeFineCompiler::Compile(F.World, RootCopy, Profile, Error);
        if (!TestNotNull(TEXT("Fine compiler builds the actual same-context Emitter/shortBeam composition"),
                         Definition))
        {
            AddError(Error);
            return false;
        }
        auto* Context = NewObject<UPrototypeCastContext>(F.World);
        Context->Caster = F.Pawn;
        Context->CastId = FGuid::NewGuid();
        Context->Level = 1;
        Context->DamageParams.SourceAbilitySystemComponent = F.PlayerASC;
        Context->DamageParams.DamageGameplayEffectClass = UArcaneShardTestDamageEffect::StaticClass();
        if (!TestTrue(TEXT("One original prototype context captures the actual PlayerState source life"),
                      Context->CaptureSourceLife(Error)))
        {
            AddError(Error);
            return false;
        }
        FAbilityNodeInput Input;
        Input.SourceToken = Context->GetSourceLife();
        Input.Context = Context;
        Input.Direction = FVector::ForwardVector;
        Input.DamageParams = Context->DamageParams;
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        FAbilityExecutionOptions Options;
        Options.CompatibilityProfile = Definition->CompatibilityProfile;
        Options.BudgetPolicy = Definition->BudgetPolicy;
        if (!TestTrue(TEXT("Host-count control starts the same fine primitives before any new finite-safe optin"),
                      Run->StartWithInput(Definition, Input, Options, Error)) ||
            (Run->IsFinished() && Run->GetEndReason() == EAbilityGraphEndReason::Failed))
        {
            AddError(Error);
            return false;
        }
        TSet<TWeakObjectPtr<AActor>> Beams;
        bool bAllPure = true;
        int32 Unexpected = 0;
        int32 Peak = 0;
        for (int32 Step = 0; Step < (bCancel ? 11 : 100); ++Step)
        {
            ++GFrameCounter;
            Run->Advance(.01f);
            Peak = FMath::Max(Peak, Observe(F, Beams, bAllPure, Unexpected));
        }
        TestEqual(TEXT("Each admitted short Beam creates exactly one actual pure host"), Beams.Num(), bCancel ? 1 : 3);
        TestTrue(TEXT("Every real Beam host is isolated from the legacy gameplay driver"), bAllPure);
        TestEqual(TEXT("The replaced private branch never creates an old projectile host"), Unexpected, 0);
        TestTrue(TEXT("Three finite launches stay within the measured concurrent host bound"), Peak >= 1 && Peak <= 3);
        TestEqual(TEXT("The original same-context logical count includes one root and each admitted Beam"),
                  Context->Spawned, bCancel ? 2 : 4);
        if (bCancel)
        {
            Run->Cancel();
        }
        else
        {
            TestTrue(TEXT("The finite same-context graph completes naturally"), Run->IsFinished());
            TestTrue(TEXT("Real target receives the independent Beam payload"),
                     F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()) <
                         10000.f);
        }
        const auto Events = Run->GetAcceptedEventCount();
        ++GFrameCounter;
        Run->Advance(1.f);
        int32 LateUnexpected = 0;
        TestEqual(TEXT("Normal/cancel teardown closes all actual Beam hosts"),
                  Observe(F, Beams, bAllPure, LateUnexpected), 0);
        TestEqual(TEXT("No cancelled Beam or emitter normal event is emitted late"), Run->GetAcceptedEventCount(),
                  Events);
        TestEqual(TEXT("Original source DA remains an Emitter"), Original->Kind, EAbilityPrototypeKind::Emitter);
        TestTrue(TEXT("The original source child was never replaced or mutated"),
                 Original->OnPeriod != RootCopy->OnPeriod);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FEmitterTrustedBeamReplacement,
    "test_25d.DynamicMage.EmitterReplacement.ActualPreviewReplacesLaunchBranchWithTrustedBeam",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)
bool FEmitterTrustedBeamReplacement::RunTest(const FString&)
{
    using namespace AbilityEmitterBranchReplacementTests;
    for (const bool bCancel : {false, true})
    {
        FAbilityActualMageFrontendFixture F;
        FString Error;
        const auto* Row = Find(F, Error);
        if (!TestNotNull(TEXT("Actual root Emitter is selected from its real directory"), Row) ||
            !TestTrue(TEXT("Actual zero-edit preview begins"), F.Developer->BeginPreview(Row->DefinitionId, Error)))
        {
            AddError(Error);
            return false;
        }
        F.Handle = F.Developer->GetPreviewHandle();
        const int32 RootIndex = Row->Base.DefaultGraph->Nodes.IndexOfByPredicate(
            [&](const auto& Node)
            {
                return Node.NodeId == Row->Base.DefaultGraph->EntryNodeId;
            });
        const int32 EmitterIndex = Row->Base.DefaultGraph->Nodes.IndexOfByPredicate(
            [](const auto& Node)
            {
                return Node.TypeId == TEXT("Control.PrototypeEmitter");
            });
        if (!TestTrue(TEXT("Actual default exposes separate root parameters and Emitter Launch"),
                      RootIndex != INDEX_NONE && EmitterIndex != INDEX_NONE) ||
            !TestTrue(TEXT("Actual parent count config is accepted"),
                      Edit(F, *Row, RootIndex, TEXT("Set.Count"), 3.f, Error)) ||
            !TestTrue(TEXT("Actual parent cadence config is accepted"),
                      Edit(F, *Row, RootIndex, TEXT("Set.Period"), .1f, Error)))
        {
            AddError(Error);
            return false;
        }
        const auto Before = F.Developer->GetDeveloperState();
        if (!TestTrue(TEXT("Trusted runtime operation replaces the old Launch branch with a new Beam fragment"),
                      Edit(F, *Row, EmitterIndex, TEXT("Replace.RegisteredBranch"), .2f, Error, TEXT("Prototype.Beam"),
                           TEXT("Launch"))))
        {
            AddError(Error);
            return false;
        }
        TestTrue(TEXT("Replacement commits a new accepted immutable recipe version"),
                 F.Developer->GetDeveloperState().Revision > Before.Revision &&
                     F.Developer->GetDeveloperState().ContentHash != Before.ContentHash);
        FAbilityRuntimeRecipeSnapshot Accepted;
        auto* Canonical = LoadObject<UAbilityPrototype>(
            nullptr,
            TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/DA_Prototype_Beam.DA_Prototype_Beam"));
        if (!TestNotNull(TEXT("Trusted canonical Beam asset is present"), Canonical) ||
            !TestTrue(TEXT("Actual accepted version exposes its frozen replacement graph"),
                      F.Developer->CreateCastSnapshot(Row->Base.AbilityTag, F.World, Accepted, Error)))
        {
            AddError(Error);
            return false;
        }
        if (!AbilityPrototypeNiagaraTestFixture::Probe(
                *this, *F.World, Canonical->LoopVFX ? Canonical->LoopVFX.Get() : Canonical->ImpactVFX.Get()))
        {
            return false;
        }
        int32 NewBeamScopes = 0;
        for (const auto& Node : Accepted.Graph->Nodes)
        {
            const auto* Scope = Cast<UAbilityPrototypeScopeNodeConfig>(Node.Config);
            if (Scope && Scope->Parameters && Scope->Parameters->Kind == EAbilityPrototypeKind::Beam)
            {
                ++NewBeamScopes;
                TestEqual(TEXT("Beam keeps canonical Range rather than relabeling Edit.Radius"),
                          Scope->Parameters->Range, Canonical->Range);
                TestTrue(TEXT("Beam preserves both canonical presentation resources"),
                         Scope->Parameters->ImpactVFX == Canonical->ImpactVFX &&
                             Scope->Parameters->LoopVFX == Canonical->LoopVFX);
                TestTrue(TEXT("Replacement uses private values and a stable original presentation identity"),
                         Scope->Parameters != Canonical && Scope->StableReference == Canonical);
            }
        }
        TestEqual(TEXT("Exactly one new parameter fragment replaces the old Launch branch"), NewBeamScopes, 1);
        auto* Spec = F.PlayerASC->FindAbilitySpecFromHandle(F.Handle);
        auto* Ability = Spec ? Cast<UPrototypeAbility>(Spec->GetPrimaryInstance()) : nullptr;
        if (!TestNotNull(TEXT("Exact original GA instance is used for real input"), Ability) ||
            !TestTrue(TEXT("Actual preview T/TargetData starts the accepted runtime branch"), CastPreview(F, *Ability)))
        {
            return false;
        }
        TSet<TWeakObjectPtr<AActor>> Beams;
        TSet<TWeakObjectPtr<UNiagaraComponent>> OwnedComponents;
        bool bAllPure = true;
        int32 Unexpected = 0;
        for (int32 Step = 0; Step < (bCancel ? 11 : 100); ++Step)
        {
            F.Step(.01f);
            Observe(F, Beams, bAllPure, Unexpected);
            for (const auto& Weak : Beams)
            {
                if (auto* Host = Weak.Get())
                {
                    TArray<UNiagaraComponent*> Components;
                    Host->GetComponents(Components);
                    for (auto* Component : Components)
                    {
                        OwnedComponents.Add(Component);
                    }
                }
            }
        }
        TestEqual(TEXT("Real frontend produces only the admitted Beam fragments"), Beams.Num(), bCancel ? 1 : 3);
        TestEqual(TEXT("Replacement removed the old projectile branch rather than appending a second attack"),
                  Unexpected, 0);
        TestTrue(TEXT("Actual runtime hosts remain presentation-only"), bAllPure);
        TestTrue(TEXT("Actual canonical Beam resources created registered Niagara components"),
                 !OwnedComponents.IsEmpty());
        if (bCancel)
        {
            TestTrue(TEXT("Public preview close cancels its exact in-flight replacement"),
                     F.Developer->EndPreview(Error));
        }
        F.Step(.5f);
        int32 LateUnexpected = 0;
        TestEqual(TEXT("Natural/preview-close completion leaves no active Beam host"),
                  Observe(F, Beams, bAllPure, LateUnexpected), 0);
        TestEqual(TEXT("Cancellation cannot launch a new late Beam cue"), Beams.Num(), bCancel ? 1 : 3);
        for (const auto& Component : OwnedComponents)
        {
            TestFalse(TEXT("Exact finished/cancelled Beam host reclaimed its registered Niagara components"),
                      Component.IsValid() && Component->IsRegistered());
        }
        TestEqual(TEXT("Finite replaced graph releases world ownership"),
                  F.World->GetSubsystem<UAbilityGraphWorldSubsystem>()->GetActiveExecutionCount(), 0);
        if (!bCancel)
        {
            TestTrue(TEXT("Actual replacement Beam damage reaches the real target"),
                     F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()) <
                         10000.f);
        }
    }
    return true;
}
#endif
