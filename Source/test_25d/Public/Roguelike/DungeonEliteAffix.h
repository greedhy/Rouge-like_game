// 调用链：楼层 Profile 抽样 → 房间生成队列 → Actor 词缀状态及显示。
#pragma once

#include "CoreMinimal.h"
#include "DungeonEliteAffix.generated.h"

UENUM(BlueprintType)
enum class EDungeonEliteAffix : uint8
{
    None,
    Fortified,
    Frenzied,
    Regenerating,
    Protective
};

USTRUCT(BlueprintType)
struct TEST_25D_API FDungeonEliteAffixWeight
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    EDungeonEliteAffix Affix = EDungeonEliteAffix::None;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "1", ClampMax = "100"))
    int32 Weight = 1;
};
