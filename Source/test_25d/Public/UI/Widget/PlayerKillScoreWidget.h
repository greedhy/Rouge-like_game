// 调用链：玩家 HUD / 地下城面板创建 UMG → 本地 PlayerState 快照 → 刷新数字文本。
#pragma once

#include "Blueprint/UserWidget.h"
#include "Player/PlayerKillScore.h"
#include "PlayerKillScoreWidget.generated.h"

class UTextBlock;

/** 共用的只读计分板。常规 HUD 独立摆放，地下城嵌入已有右上角面板。 */
UCLASS()
class UPlayerKillScoreWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    /** 创建不截获输入的计分板与四个读数。 */
    virtual TSharedRef<SWidget> RebuildWidget() override;
    /** 初次进入视口即显示已复制的快照。 */
    virtual void NativeConstruct() override;
    /** 后续只在快照改变时更新文本，无统计逻辑。 */
    virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;

private:
    void Refresh();

    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> Values;

    TOptional<FPlayerKillScore> Displayed;
};
