/**
 * 世界子系统 Tick 写入 Marks -> 服务器本地 Refresh；Marks 复制到客户端后 RepNotify Refresh -> 创建/更新文本组件；本地 Tick 面向相机。Actor 不参与碰撞，不结算效果；标签组件由 Actor 持有，子系统 Deinitialize 销毁宿主。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExtensionPresentation.generated.h"

USTRUCT(BlueprintType)
struct FExtensionMarkView
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly)
    int32 Id = 0;
    UPROPERTY(BlueprintReadOnly)
    FVector Position;
    UPROPERTY(BlueprintReadOnly)
    int32 Layers = 0;
    UPROPERTY(BlueprintReadOnly)
    bool bPending = false;
};

/** One presentation actor per world, with capped pooled mark labels. */
UCLASS()
class TEST_25D_API AExtensionPresentation : public AActor
{
    GENERATED_BODY()
public:
    /** 构造始终相关的复制表现 Actor，设置 15Hz 网络频率和无碰撞根组件。 */
    AExtensionPresentation();
    UPROPERTY(ReplicatedUsing = Refresh, BlueprintReadOnly)
    TArray<FExtensionMarkView> Marks;
    /** 本地逐帧令可见标签朝向第一玩家相机；不修改服务器叠层状态。 */
    virtual void Tick(float Delta) override;
    /** 只复制 Marks 快照；客户端组件由 Refresh 重建，不复制组件数组。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UFUNCTION()
    /** 专用服务器跳过；最多创建 256 个文本组件，复用标签并隐藏多余条目，显示层数及待引爆标记。 */
    void Refresh();

private:
    UPROPERTY()
    TArray<TObjectPtr<class UTextRenderComponent>> Labels;
};
