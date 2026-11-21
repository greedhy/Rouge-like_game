// 调用链：细节点表现所有者 -> Begin/Update/Impact/Finish -> Runtime 的仅表现模式 -> Niagara/复制快照。
// 快照仅保存冻结表现数据和稳定资源引用，不保存执行图、伤害参数或共享施法 Context。
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionNode.h"
#include "AbilityPrototypePresentationService.generated.h"

class UMaterialInterface;
class UNiagaraSystem;
class UNiagaraComponent;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;

/** 原 Actor/Mixed/Extension 表现约定显式选择；不切换伤害或目标后端。 */
UENUM()
enum class EAbilityPrototypePresentationProfile : uint8
{
    Actor,
    Mixed,
    Extension
};

/** 由权威端冻结并复制的纯表现值；原 DA 后续调参不会改变已生成的表现。 */
USTRUCT()
struct TEST_25D_API FAbilityPrototypeVisualSnapshot
{
    GENERATED_BODY()

    UPROPERTY()
    bool bReady = false;
    UPROPERTY()
    EAbilityPrototypePresentationProfile Profile = EAbilityPrototypePresentationProfile::Actor;
    UPROPERTY()
    EAbilityPrototypeKind Kind = EAbilityPrototypeKind::Projectile;
    UPROPERTY()
    EExtensionKind ExtensionBehavior = EExtensionKind::Burst;
    /** 仅用于稳定资产身份；渲染器不得从此对象回读几何或缩放。临时 DA 不可跨网络复制。 */
    UPROPERTY()
    TObjectPtr<UAbilityPrototype> StableReference;
    UPROPERTY()
    TObjectPtr<UNiagaraSystem> LoopVFX;
    UPROPERTY()
    TObjectPtr<UNiagaraSystem> ImpactVFX;
    /** 区分有意不配置资源与 NetGUID 尚未解析，避免先播放不完整首帧。 */
    UPROPERTY()
    bool bExpectsLoopVFX = false;
    UPROPERTY()
    bool bExpectsImpactVFX = false;
    UPROPERTY()
    TObjectPtr<UMaterialInterface> AreaMaterial;
    UPROPERTY()
    FVector LoopScale = FVector::OneVector;
    UPROPERTY()
    FVector ImpactScale = FVector::OneVector;
    UPROPERTY()
    float Radius = 250.f;
    UPROPERTY()
    float InnerRadius = 100.f;
    UPROPERTY()
    float Range = 1200.f;
    UPROPERTY()
    float SpreadDegrees = 45.f;
    UPROPERTY()
    bool bGroundPresentation = false;
    UPROPERTY()
    bool bSingleProjectile = false;
    UPROPERTY()
    bool bUseAuthoredScale = false;
    UPROPERTY()
    bool bShowAreaDisc = false;
    UPROPERTY()
    bool bSnowfall = false;
};

/** 一次表现更新；Revision 单调递增，结束是吸收状态，晚到旧帧不能恢复循环效果。 */
USTRUCT()
struct TEST_25D_API FAbilityPrototypePresentationFrame
{
    GENERATED_BODY()

    UPROPERTY()
    bool bReady = false;
    UPROPERTY()
    uint32 Revision = 1;
    /** 首帧将本次 Scope 的单弹身份冻结进 visual snapshot；更新帧不重解释容器身份。 */
    UPROPERTY()
    bool bSingleProjectile = false;
    UPROPERTY()
    FTransform Transform = FTransform::Identity;
    /** 首粒子生成前设置端点，不依赖 Owner/Source 的复制到达顺序。 */
    UPROPERTY()
    FVector BeamStart = FVector::ZeroVector;
    UPROPERTY()
    FVector BeamEnd = FVector::ZeroVector;
    UPROPERTY()
    float WaveRadius = 0.f;
    UPROPERTY()
    TArray<FVector> OrbitPositions;
    UPROPERTY()
    bool bReturning = false;
    UPROPERTY()
    bool bFinished = false;
    UPROPERTY()
    bool bCancelled = false;
    UPROPERTY()
    bool bImpactOnFinish = false;
    /** 原Mixed的Finish在正常/取消时均播放一次Impact；仅兼容入口设置，不改变通用取消合同。 */
    UPROPERTY()
    bool bOriginalMixedFinish = false;
};

/** 诊断实际表现转换；计数不能替代真实组件和业务无副作用测试。 */
struct TEST_25D_API FAbilityPrototypePresentationStats
{
    int32 Begins = 0;
    int32 Updates = 0;
    int32 Impacts = 0;
    int32 Finishes = 0;
};

/** 每个 Runtime 的本地表现状态；弱组件引用不延长 Actor/世界寿命，资源由 Actor 组件所有权管理。 */
struct TEST_25D_API FAbilityPrototypePresentationLocalState
{
    FAbilityPrototypePresentationFrame AcceptedFrame;
    bool bBegan = false;
    bool bTerminal = false;
    /** 旧结束位先于完整结束帧到达时，先关循环，等待携带取消/命中语义的帧。 */
    bool bAwaitingFinishFrame = false;
    bool bFinishPresented = false;
    /** 本宿主递增序列的最近身份；0表示尚未接收。模2^32比较允许跨零及窗口内乱序。 */
    uint32 LatestImpactEvent = 0;
    /** 仅保留最近256个序列位置。过期的不可靠特效不在资源晚到后重播；不限制任何游戏命中。 */
    TSet<uint32> ImpactEvents;
    TMap<uint32, FVector> PendingImpacts;
    TWeakObjectPtr<UStaticMeshComponent> Area;
    TWeakObjectPtr<UInstancedStaticMeshComponent> Snow;
    TArray<TWeakObjectPtr<UNiagaraComponent>> OrbitVisuals;
};

namespace AbilityPrototypePresentationService
{
    /** 不可靠命中RPC的同一有界接收合同，供权威发送前和本地实际呈现共同校验；不修改状态。 */
    TEST_25D_API bool CanAcceptImpact(const FAbilityPrototypePresentationLocalState& State, uint32 EventId);
    /** 冻结本次可视值；StableReference 只接受可跨网定位的资产，实际数值来自 Frozen。 */
    TEST_25D_API FAbilityPrototypeVisualSnapshot Capture(const UAbilityPrototype& Frozen,
                                                         UAbilityPrototype* StableReference,
                                                         EAbilityPrototypePresentationProfile Profile);
    /** 只生成表现宿主，不启动 Driver、不预留 logical spawn、不创建原型子节点。 */
    TEST_25D_API AActor* Begin(const FAbilitySourceToken& Source, const UAbilityPrototype& Frozen,
                               UAbilityPrototype* StableReference, const FAbilityPrototypePresentationFrame& Frame,
                               EAbilityPrototypePresentationProfile Profile, FString& Error);
    /** 更新现有纯表现宿主；来源原生命失效则关闭本表现，不能重新绑定新 Avatar。 */
    TEST_25D_API bool Update(AActor* Host, const FAbilityPrototypePresentationFrame& Frame);
    /** EventId 在该宿主内去重；结束后的晚到命中不会重开表现。 */
    TEST_25D_API bool Impact(AActor* Host, uint32 EventId, const FVector& Position);
    /** 幂等结束；Actor/Mixed 取消抑制结束特效，Extension 可显式请求兼容结束表现；均不触发业务 OnExpire。 */
    TEST_25D_API bool Finish(AActor* Host, bool bCancelled, bool bImpactOnFinish = false);
    /** 原型重建的结束入口：保留Mixed原有取消特效，其他宿主继续使用显式结束位。无业务OnExpire。 */
    TEST_25D_API bool FinishOriginal(AActor* Host, bool bCancelled, bool bImpactOnFinish = false);

    /** 两种宿主共用的帧校验；只验证表现值，不调用原型业务配置/伤害校验。 */
    TEST_25D_API bool IsValidFrame(const FAbilityPrototypePresentationFrame& Frame);
    /** 合并复制帧；终止不可逆，重复/倒序帧不能重播或回滚表现。返回是否接受新的帧。 */
    TEST_25D_API bool AcceptFrame(const FAbilityPrototypePresentationFrame& Incoming,
                                  FAbilityPrototypePresentationLocalState& State,
                                  FAbilityPrototypePresentationStats& Stats);
    /** 共用本地渲染入口；组件在此 Actor 下注册，首次设置端点后才激活 Niagara。 */
    TEST_25D_API void Refresh(AActor& Host, const FAbilityPrototypeVisualSnapshot& Snapshot,
                              FAbilityPrototypePresentationLocalState& State, FAbilityPrototypePresentationStats& Stats,
                              UNiagaraComponent* Visual, UNiagaraComponent* InnerVisual = nullptr);
    /** 记录/呈现可靠身份的一次命中；资源未解析时暂存，取消/结束时丢弃晚到事件。 */
    TEST_25D_API bool PresentImpact(AActor& Host, const FAbilityPrototypeVisualSnapshot& Snapshot,
                                    FAbilityPrototypePresentationLocalState& State,
                                    FAbilityPrototypePresentationStats& Stats, uint32 EventId, const FVector& Position);
    /** EndPlay 与终止共用的可视清理；只关组件，绝不产生业务或结束命中。 */
    TEST_25D_API void StopVisuals(FAbilityPrototypePresentationLocalState& State, UNiagaraComponent* Visual,
                                  UNiagaraComponent* InnerVisual = nullptr);
} // namespace AbilityPrototypePresentationService
