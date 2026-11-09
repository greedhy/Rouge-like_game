// 调用链：统一 GA 入口 CaptureSource/CaptureTarget -> 图运行实例持有弱令牌 -> 延迟节点 ResolveSource/ResolveTarget。
// 常驻基础监听器仅用 ResolveSourceAvatarLifetime 验证订阅寿命；每个新事件再捕获严格来源交给实际动作。
#pragma once

#include "CoreMinimal.h"
#include "Mass/EntityHandle.h"
#include "PureMass/PureMassCombatTarget.h"

class UAbilitySystemComponent;

/** 服务器捕获的来源生命；不保活 World、Avatar 或 ASC，也不修改旧 GA 的目标结构。 */
struct TEST_25D_API FAbilitySourceToken
{
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<AActor> Avatar;
    TWeakObjectPtr<UAbilitySystemComponent> ASC;
    TOptional<FGuid> DungeonRunId;
    TOptional<int32> EnemyLease;
};

/** Actor 使用弱身份和实际池租约；Mass 同时绑定所属世界、查询 ID 与实际 EntityHandle。 */
struct TEST_25D_API FAbilityTargetToken
{
    TWeakObjectPtr<UWorld> World;
    FPureMassCombatTarget Target;
    TOptional<int32> ActorLease;
    FMassEntityHandle MassEntity;
    TOptional<FGuid> DungeonRunId;
};

namespace AbilityExecutionIdentity
{
    /** 仅服务器捕获存活 Avatar 当前 ASC 绑定；失败清空令牌并返回可诊断原因。 */
    TEST_25D_API bool CaptureSource(AActor* Source, FAbilitySourceToken& OutToken, FString& OutError);
    /** 从有效来源捕获当前敌对活目标；旧池生命、跨世界或无实际 Mass 句柄失败关闭。 */
    TEST_25D_API bool CaptureTarget(const FAbilitySourceToken& Source, const FPureMassCombatTarget& Target,
                                    FAbilityTargetToken& OutToken, FString& OutError);
    /** 重新核对世界、局、ASC Avatar 和源生命；失败把输出设为空。 */
    TEST_25D_API bool ResolveSource(const FAbilitySourceToken& Source, AActor*& OutAvatar);
    /**
     * 仅供常驻订阅的生命周期检查：保留世界、权威、ASC/Avatar、存活与池租约校验，允许 Run 初始化或切换。
     * 不能作为伤害/效果动作的许可；实际动作仍须使用 CaptureSource/ResolveSource 的当前 Run 严格令牌。
     */
    TEST_25D_API bool ResolveSourceAvatarLifetime(const FAbilitySourceToken& Source, AActor*& OutAvatar);
    /** 每次效果应用前重新核对生命，并沿用 PureMassCombat 的存活、敌对与房间校验。 */
    TEST_25D_API bool ResolveTarget(const FAbilitySourceToken& Source, const FAbilityTargetToken& Target,
                                    FVector& OutLocation);
} // namespace AbilityExecutionIdentity
