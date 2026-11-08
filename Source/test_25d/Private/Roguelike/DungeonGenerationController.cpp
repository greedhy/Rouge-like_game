// 主要调用链：生成布局 → 本地装配与导航检查 → 控制器上报 → 屏障确认 → 放行移动；失败关闭门禁。
#include "Roguelike/DungeonGenerationController.h"
#include "Roguelike/DungeonGenerationRuntime.h"
#include "EngineUtils.h"

namespace
{
    // 遍历当前世界查找生成宿主。
    ADungeonGenerationRuntime* FindDungeon(UWorld* World)
    {
        if (World)
            for (TActorIterator<ADungeonGenerationRuntime> It(World); It; ++It)
                return *It;
        return nullptr;
    }
} // namespace

// 本地检查移动门禁，并按生成编号去重上报装配就绪或失败。
void ADungeonGenerationController::PlayerTick(float DeltaTime)
{
    if (IsLocalController())
    {
        auto* Dungeon = FindDungeon(GetWorld());
        const bool Hold = !Dungeon || !Dungeon->CanLocalPlayerMove();
        if (Hold != bHoldingMovement)
        {
            SetIgnoreMoveInput(Hold);
            bHoldingMovement = Hold;
        }
        if (Dungeon && Dungeon->Manifest.GenerationId.IsValid())
        {
            const auto Generation = Dungeon->Manifest.GenerationId;
            if (!Dungeon->GetLocalFailure().IsEmpty() && FailedGeneration != Generation)
            {
                FailedGeneration = Generation;
                ServerReportDungeonFailure(Generation, Dungeon->GetLocalFailure().Left(200));
            }
            else if (Dungeon->IsLocalReady() && ReportedGeneration != Generation)
            {
                ReportedGeneration = Generation;
                ServerReportDungeonReady(Generation, Dungeon->Manifest.Digest);
            }
        }
    }
    Super::PlayerTick(DeltaTime);
}

// 服务器转交控制器身份、生成编号及摘要给宿主验证。
void ADungeonGenerationController::ServerReportDungeonReady_Implementation(FGuid Generation, const FString& Digest)
{
    if (auto* Dungeon = FindDungeon(GetWorld()))
        Dungeon->ReportReady(this, Generation, Digest);
}

// 服务器转交受长度限制的客户端装配错误。
void ADungeonGenerationController::ServerReportDungeonFailure_Implementation(FGuid Generation, const FString& Reason)
{
    if (auto* Dungeon = FindDungeon(GetWorld()))
        Dungeon->ReportFailure(this, Generation, Reason.Left(200));
}

// 退出时释放本对象持有的资源与状态，并调用父类结束生命周期。
void ADungeonGenerationController::EndPlay(EEndPlayReason::Type Reason)
{
    if (bHoldingMovement)
    {
        SetIgnoreMoveInput(false);
        bHoldingMovement = false;
    }
    Super::EndPlay(Reason);
}
