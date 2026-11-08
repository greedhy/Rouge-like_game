/**
 * 世界子系统 Tick 写入 Marks -> 服务器本地 Refresh；Marks 复制到客户端后 RepNotify Refresh -> 创建/更新文本组件；本地 Tick 面向相机。Actor 不参与碰撞，不结算效果；标签组件由 Actor 持有，子系统 Deinitialize 销毁宿主。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
#include "AbilitySystem/PrototypeExtensions/ExtensionPresentation.h"
#include "Components/TextRenderComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
/** 构造始终相关的复制表现 Actor，设置 15Hz 网络频率和无碰撞根组件。 */
AExtensionPresentation::AExtensionPresentation()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    SetNetUpdateFrequency(15);
    PrimaryActorTick.bCanEverTick = true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    SetActorEnableCollision(false);
}
/** 专用服务器跳过；最多创建 256 个文本组件，复用标签并隐藏多余条目，显示层数及待引爆标记。 */
void AExtensionPresentation::Refresh()
{
    if (GetNetMode() == NM_DedicatedServer)
        return;
    while (Labels.Num() < Marks.Num() && Labels.Num() < 256)
    {
        auto* Label = NewObject<UTextRenderComponent>(this);
        Label->SetupAttachment(RootComponent);
        Label->SetHorizontalAlignment(EHTA_Center);
        Label->SetWorldSize(26);
        Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Label->RegisterComponent();
        Labels.Add(Label);
    }
    for (int32 I = 0; I < Labels.Num(); ++I)
    {
        Labels[I]->SetVisibility(Marks.IsValidIndex(I));
        if (!Marks.IsValidIndex(I))
            continue;
        const auto& M = Marks[I];
        Labels[I]->SetWorldLocation(M.Position);
        Labels[I]->SetText(
            FText::FromString(FString::Printf(TEXT("%d%s"), M.Layers, M.bPending ? TEXT("!") : TEXT(""))));
        Labels[I]->SetTextRenderColor(M.bPending ? FColor::Red : FColor::Yellow);
    }
}
/** 本地逐帧令可见标签朝向第一玩家相机；不修改服务器叠层状态。 */
void AExtensionPresentation::Tick(float Delta)
{
    Super::Tick(Delta);
    auto* PC = GetWorld()->GetFirstPlayerController();
    if (PC && PC->PlayerCameraManager)
        for (const auto& Label : Labels)
            if (Label->IsVisible())
                Label->SetWorldRotation(
                    (PC->PlayerCameraManager->GetCameraLocation() - Label->GetComponentLocation()).Rotation());
}
/** 只复制 Marks 快照；客户端组件由 Refresh 重建，不复制组件数组。 */
void AExtensionPresentation::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AExtensionPresentation, Marks);
}
