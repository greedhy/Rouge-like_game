// 调用链：GA/RunAbilityTimeline -> Resolve -> 施法快照 -> PhaseAt/ElapsedAt/AcceptEvent。
// DataAsset/DataTable 为共享只读配置；快照是每次任务独占的值，不复制、不持有运行世界。
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "AbilityTimelineDefinition.generated.h"

class UAnimMontage;

/** 数据表每行管理一个阶段的实际秒数；激活之后改表不影响当前施法。 */
USTRUCT(BlueprintType)
struct TEST_25D_API FAbilityTimelineTimingRow : public FTableRowBase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.001"))
    float Duration = 0.3f;
};

USTRUCT(BlueprintType)
struct TEST_25D_API FAbilityTimelinePhase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName Name;
    /** 源蒙太奇时间；起点是前一个阶段的终点，第一个阶段从零开始。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float SourceEnd = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.001"))
    float Duration = 0.3f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (RowType = "/Script/test_25d.AbilityTimelineTimingRow"))
    FDataTableRowHandle Timing;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bInterruptible = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTagContainer CancelInto;
};

/** 每个标签是独立的单次事件，多次命中必须使用不同标签。位置窗口为闭区间。 */
USTRUCT(BlueprintType)
struct TEST_25D_API FAbilityTimelineEventWindow
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag EventTag;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float SourceStart = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float SourceEnd = 0;
};

/** 解析后的数值快照。任务结束时 Close 使所有迟到事件无效；实例 ID 来自 AnimInstance。 */
struct TEST_25D_API FAbilityTimelineSnapshot
{
    TArray<FAbilityTimelinePhase> Phases;
    TArray<FAbilityTimelineEventWindow> Events;
    FGuid CastId;
    int32 MontageInstanceId = INDEX_NONE;
    bool bClosed = false;
    TSet<FGameplayTag> ConsumedEvents;

    /** 返回当前位置阶段，边界属于下一阶段，动画终点属于最后阶段。 */
    int32 PhaseAt(float Position) const;
    /** 将源位置映射为理想实际秒数，用于 UI 和跨阶段窗口长度。 */
    float ElapsedAt(float Position) const;
    /** 取得该阶段的蒙太奇播放倍率。 */
    float RateAt(float Position) const;
    /** 只有当前施法、当前播放实例、配置窗口内的首次事件才接受。 */
    bool AcceptEvent(FGameplayTag Tag, const FGuid& InCastId, int32 InstanceId, float SourcePosition);
    /** 关闭事件入口；重复关闭安全。 */
    void Close();
};

// One GA can select among multiple montages, such as combo steps or guard/counter.
USTRUCT(BlueprintType)
struct FAbilityTimelineMontagePhases
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FAbilityTimelinePhase> Phases;
};

UCLASS(BlueprintType)
class TEST_25D_API UAbilityTimelineDefinition : public UDataAsset
{
    GENERATED_BODY()
public:
    /** 蓝图/资产脚本在Commit前检查配置，返回可读原因，不创建运行任务或播放动画。 */
    UFUNCTION(BlueprintCallable, Category = "Ability|Timeline")
    bool ValidateForMontage(UAnimMontage* Montage, FString& Error, bool bAllowEndBlendOut = false) const;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FAbilityTimelinePhase> Phases;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FAbilityTimelineEventWindow> Events;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TMap<TObjectPtr<UAnimMontage>, FAbilityTimelineMontagePhases> MontageVariants;

    /** 校验连续覆盖并解析数据表，成功时生成独立施法 ID；失败不返回半份配置。 */
    bool Resolve(float MontageLength, FAbilityTimelineSnapshot& Out, FString& Error) const;
    /** 校验首版支持的连续非根运动播放，并拒绝会在变速边界前提前混出的配置。 */
    bool ResolveMontage(const UAnimMontage* Montage, FAbilityTimelineSnapshot& Out, FString& Error,
                        bool bAllowEndBlendOut = false) const;

private:
    bool ResolvePhases(float MontageLength, const TArray<FAbilityTimelinePhase>& SourcePhases,
                       FAbilityTimelineSnapshot& Out, FString& Error) const;
};
