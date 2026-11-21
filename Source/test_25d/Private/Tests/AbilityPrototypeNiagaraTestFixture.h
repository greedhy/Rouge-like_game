// 调用链：独立Rendered测试 -> 原SpawnSystemAtLocation真实probe -> 原/细图 -> 实际注册Niagara组件。
// 只检查资源、原生创建和世界变换；不把统计或手工组件替代为旧Runtime真实粒子，也不改NullRHI全局门。
#pragma once
#include "Misc/AutomationTest.h"
#include "Misc/App.h"
#include "AbilitySystem/Data/AbilityInfo.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/UObjectIterator.h"

namespace AbilityPrototypeNiagaraTestFixture
{
    /** 从既有目录选择稳定长循环资源，避免空System/自动完成造成假粒子对照。 */
    inline UNiagaraSystem* System()
    {
        for (const TCHAR* Path : {TEXT("/Game/BluePrints/AbilitySystem/Data/DA_AbilityInfo.DA_AbilityInfo"),
                                  TEXT("/Game/PureMass/DA_MassAbilityInfo.DA_MassAbilityInfo")})
        {
            const auto* Catalog = LoadObject<UAbilityInfo>(nullptr, Path);
            if (!Catalog)
            {
                continue;
            }
            for (const auto& Row : Catalog->AbilityInformation)
            {
                const auto* Ability = Row.Ability ? Cast<UPrototypeAbility>(Row.Ability->GetDefaultObject()) : nullptr;
                if (Ability && Ability->Prototype && Ability->Prototype->LoopVFX)
                {
                    return Ability->Prototype->LoopVFX;
                }
            }
        }
        return nullptr;
    }

    /** 必须先证明同资源/同默认调用可创建真组件；环境失败不能转化成产品表现RED。 */
    inline bool Probe(FAutomationTestBase& Test, UWorld& World, UNiagaraSystem* Asset)
    {
        if (!Test.TestTrue(TEXT("Rendered fixture has a real rendering RHI"), FApp::CanEverRender()) ||
            !Test.TestNotNull(TEXT("Existing stable Niagara resource is available"), Asset))
        {
            return false;
        }
        auto* Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(&World, Asset, FVector::ZeroVector);
        if (!Test.TestNotNull(TEXT("Original native SpawnSystemAtLocation actually returns a component"), Component))
        {
            return false;
        }
        const bool bReady = Test.TestTrue(TEXT("Original native probe registers the expected real component"),
                                          Component->IsRegistered() && Component->GetAsset() == Asset);
        Component->DestroyComponent();
        return bReady;
    }

    /** 旧原生组件可能由WorldSettings持有；按世界/真实资源过滤，不能只看新宿主的私有stats。 */
    inline TArray<FTransform> Impacts(UWorld& World, UNiagaraSystem* Asset)
    {
        TArray<FTransform> Result;
        for (TObjectIterator<UNiagaraComponent> It; It; ++It)
        {
            if (IsValid(*It) && !It->IsTemplate() && It->IsRegistered() && It->GetWorld() == &World &&
                It->GetAsset() == Asset)
            {
                Result.Add(It->GetComponentTransform());
            }
        }
        return Result;
    }

    inline bool Contains(const TArray<FTransform>& Observed, FVector Position, FVector Scale)
    {
        return Observed.ContainsByPredicate(
            [&](const FTransform& Transform)
            {
                return Transform.GetLocation().Equals(Position, .1f) && Transform.GetScale3D().Equals(Scale, .01f);
            });
    }
} // namespace AbilityPrototypeNiagaraTestFixture
