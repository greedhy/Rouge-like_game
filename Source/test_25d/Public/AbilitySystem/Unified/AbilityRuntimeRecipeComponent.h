// 调用链：PlayerState 所属配方组件 -> 注册基础定义/按来源修改 -> 原子编译新版本 -> GA 创建本次快照。
// 服务器持有配置，不复制任意图；EndPlay 清引用。图执行与在途弹体持有各自快照，不随之后修改变化。
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilityRuntimeRecipeComponent.generated.h"

USTRUCT()
struct FAbilityRuntimeRecipeEntry
{
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<UAbilityGraphDefinition> Base;
    UPROPERTY()
    TObjectPtr<UAbilityGraphDefinition> Compiled;
    UPROPERTY()
    TArray<FAbilityRuntimeRecipeContribution> Contributions;
    int32 Revision = 0;
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FAbilityRuntimeRecipeChanged, FGameplayTag, int32);

/** 组件属于现有 ASC 的 Owner（通常 PlayerState），不是独立技能系统或全局单例。 */
UCLASS(ClassGroup = (Abilities), meta = (BlueprintSpawnableComponent))
class TEST_25D_API UAbilityRuntimeRecipeComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 不启用 Tick，修改由服务器事务驱动；原有技能授予/费用不属于本组件。 */
    UAbilityRuntimeRecipeComponent();
    /** 成功事务后通知服务器本地观察者；参数只含身份/版本，客户端显示由所属组件另行同步。 */
    FAbilityRuntimeRecipeChanged OnRecipeChanged;
    /** 注册基础图并重新验证原贡献；失败保留旧基础、版本与有效图。 */
    bool RegisterBase(FGameplayTag AbilityTag, const UAbilityGraphDefinition* Base, FString& Error);
    /** 服务器按 SourceId 新增或替换贡献，只有完整编译成功才提交新版本。 */
    bool SetContribution(FGameplayTag AbilityTag, const FAbilityRuntimeRecipeContribution& Contribution,
                         FString& Error);
    /** 仅撤销指定来源；不存在来源是幂等成功，不产生额外版本。 */
    bool RemoveContribution(FGameplayTag AbilityTag, FName SourceId, FString& Error);
    /** 为本次激活复制独立定义，调用者须以 UPROPERTY 保留 Snapshot.Graph。 */
    bool CreateCastSnapshot(FGameplayTag AbilityTag, UObject* Outer, FAbilityRuntimeRecipeSnapshot& Out,
                            FString& Error) const;
    /** 无动态来源时让 GA 继续原兼容入口；基础注册本身不改变技能行为。 */
    bool HasContributions(FGameplayTag AbilityTag) const;
    /** 当前成功提交版本；未注册返回 0，失败编辑不递增。 */
    int32 GetRevision(FGameplayTag AbilityTag) const;
    /** 最近修改失败诊断，供开发面板读取；不作为网络权威凭据。 */
    FString GetLastError() const;
    /** 清理组件拥有的基础/编译图，不操作仍由别的施法快照持有的对象。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
    /** 所有者可在发布外部规则前补充隔离校验；默认不增加生产限制，不执行游戏动作。 */
    virtual bool ValidateContributionForCurrentOwner(FGameplayTag AbilityTag,
                                                     const FAbilityRuntimeRecipeContribution& Contribution,
                                                     FString& Error) const;

private:
    // 开发预览仅保存/恢复自己暂改的基础定义；合并外部来源，不允许整表回滚覆盖合法修改。
    friend class UAbilityRuntimeRecipeDeveloperComponent;
    UPROPERTY(Transient)
    TMap<FGameplayTag, FAbilityRuntimeRecipeEntry> Entries;
    FString LastError;
    /** 临时预览建立基础映射时不发布生产变化回调；只由友元开发组件的局部事务使用。 */
    bool bSuppressRecipeNotifications = false;
};
