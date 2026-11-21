// 调用链：Scope -> World.Surface.Acquired -> Phase -> World.ReleaseSurface -> 正常到期payload。
#pragma once
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilityPrototypeSurfaceNodes.generated.h"
class FAbilityNodeRegistry;
class UAbilityPrototypeParameterFrame;
class ATerrainSurface;

UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityPrototypeSurfaceNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
};
UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityPrototypeSurfaceReleaseNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
};
UCLASS(Transient)
class TEST_25D_API UAbilityPrototypeSurfaceNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 创建原配置地表并发布准确弱槽；真实接触/状态定时器继续属于TerrainActor。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
    /** 取消/世界结束只销毁本次准确实例，不扫描或移除其它施法、关卡地表。 */
    virtual void Stop(EAbilityGraphEndReason Reason) override;
    /** 正常到期先释放地表和本资源调用；调用者随后才发布独立OnExpire。 */
    void Release();

private:
    UPROPERTY(Transient)
    TObjectPtr<UAbilityPrototypeParameterFrame> Frame;
    UPROPERTY(Transient)
    TObjectPtr<ATerrainSurface> Surface;
};
UCLASS(Transient)
class TEST_25D_API UAbilityPrototypeSurfaceReleaseNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 只接受当前根/帧的资源所有者，释放成功后提供正常到期出口。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};
namespace AbilityPrototypeSurfaceNodes
{
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
}
