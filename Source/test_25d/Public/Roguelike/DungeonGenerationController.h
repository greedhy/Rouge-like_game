// 主要调用链：生成布局 → 本地装配与导航检查 → 控制器上报 → 屏障确认 → 放行移动；失败关闭门禁。
#pragma once

#include "CoreMinimal.h"
#include "PureMass/PureMassPlayerController.h"
#include "DungeonGenerationController.generated.h"

/** Copied blueprint retains existing movement, GAS and UI configuration. */
UCLASS()
class TEST_25D_API ADungeonGenerationController : public APureMassPlayerController
{
    GENERATED_BODY()
public:
    // 本地检查移动门禁，并按生成编号去重上报装配就绪或失败。
    virtual void PlayerTick(float DeltaTime) override;
    // 退出时释放本对象持有的资源与状态，并调用父类结束生命周期。
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    // 可靠上报本机装配编号与摘要，由服务器验证后计入就绪人数。
    UFUNCTION(Server, Reliable)
    void ServerReportDungeonReady(FGuid Generation, const FString& Digest);
    // 可靠上报本次装配失败，编号用于拒绝过期报告。
    UFUNCTION(Server, Reliable)
    void ServerReportDungeonFailure(FGuid Generation, const FString& Reason);

private:
    bool bHoldingMovement = false;
    FGuid ReportedGeneration;
    FGuid FailedGeneration;
};
