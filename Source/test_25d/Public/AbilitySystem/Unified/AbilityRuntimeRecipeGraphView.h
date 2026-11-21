// 调用链：服务器已接受施法快照 -> 纯值图投影 -> 所属玩家复制 -> 运行时画布与精确编辑请求。
// 本文件只描述GUI数据，不持有图/配置UObject或施法权限；客户端不能用显示数据替代服务器校验。
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityGraphTypes.h"
#include "AbilityRuntimeRecipeGraphView.generated.h"

struct FAbilityRuntimeRecipeSnapshot;

USTRUCT(BlueprintType)
struct TEST_25D_API FAbilityRuntimeGraphPortView
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FName Name;
    UPROPERTY(BlueprintReadOnly)
    EAbilityPortPayload Payload = EAbilityPortPayload::Origin;
};

USTRUCT(BlueprintType)
struct TEST_25D_API FAbilityRuntimeGraphNodeView
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FGuid NodeId;
    UPROPERTY(BlueprintReadOnly)
    FName TypeId;
    UPROPERTY(BlueprintReadOnly)
    FString Label;
    UPROPERTY(BlueprintReadOnly)
    FString ParameterSummary;
    UPROPERTY(BlueprintReadOnly)
    TArray<FAbilityRuntimeGraphPortView> Ports;
    UPROPERTY(BlueprintReadOnly)
    TArray<FName> SupportedModifiers;
};

USTRUCT(BlueprintType)
struct TEST_25D_API FAbilityRuntimeGraphEdgeView
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FGuid From;
    UPROPERTY(BlueprintReadOnly)
    FGuid To;
    UPROPERTY(BlueprintReadOnly)
    FName OutputPort;
    UPROPERTY(BlueprintReadOnly)
    EAbilityPortPayload Payload = EAbilityPortPayload::Origin;
};

/** 下一次测试施法的准确拓扑；草稿使用同一值结构，但不具有已接受版本的执行资格。 */
USTRUCT(BlueprintType)
struct TEST_25D_API FAbilityRuntimeGraphView
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    bool bReady = false;
    UPROPERTY(BlueprintReadOnly)
    int32 Revision = 0;
    UPROPERTY(BlueprintReadOnly)
    FString ContentHash;
    UPROPERTY(BlueprintReadOnly)
    FString Error;
    UPROPERTY(BlueprintReadOnly)
    TArray<FAbilityRuntimeGraphNodeView> Nodes;
    UPROPERTY(BlueprintReadOnly)
    TArray<FAbilityRuntimeGraphEdgeView> Edges;
    UPROPERTY(BlueprintReadOnly)
    TArray<FName> AppliedSources;
};

namespace AbilityRuntimeGraphViews
{
    /** 只读真实已编译快照；节点/边数量有界，未知注册或不完整拓扑失败，不在客户端修补。 */
    TEST_25D_API bool Build(const FAbilityRuntimeRecipeSnapshot& Snapshot, FAbilityRuntimeGraphView& Out,
                            FString& Error);
} // namespace AbilityRuntimeGraphViews
