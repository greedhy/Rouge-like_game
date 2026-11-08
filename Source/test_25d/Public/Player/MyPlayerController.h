// 主要链路：本地输入经标签门禁转交能力系统；拾取、使用、交换和丢弃通过服务器请求进入玩家状态背包，光标与界面表现留在本地。
// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Input/MyInputConfig.h"
#include "UI/Widget/DamageTextComponent.h"

#include "MyPlayerController.generated.h"

class IHighLightInterface;
class UNiagaraSystem;
class USplineComponent;
class UInputMappingContext;
class UInputAction;
class UMyAbilitySystemComponent;
struct FInputActionValue;
class AMagicCircle;
class UUserWidget;

/**
 * 
 */
UCLASS()
class TEST_25D_API AMyPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    // 启用复制并创建导航样条与光标目标初始状态。
    AMyPlayerController();
    // 读取本地定期更新的最近可拾取装备。
    AActor* GetFocusedEquipmentPickup() const
    {
        return FocusedEquipmentPickup.Get();
    }
    // 本地定期选择最近装备拾取目标，并更新光标高亮和技能指示器。
    virtual void PlayerTick(float DeltaTime) override;
    // 本地玩家连接就绪后补装输入映射。
    virtual void ReceivedPlayer() override;
    // 切换受控角色时清除能力与光标缓存，再初始化本地输入。
    virtual void AcknowledgePossession(APawn* P) override;

    UFUNCTION(Client, Reliable)
    // 仅本地控制器将伤害数字排入世界飘字池。
    void ShowDamageNumber(float DamageAccount, ACharacter* TargetCharacter, bool bBlockHit, bool bCriticalHit);

    UFUNCTION(Server, Reliable)
    // 服务器从重叠候选中选择拾取物，并交给拾取组件校验执行。
    void ServerRequestPickup();

    // Called by authoritative WorldPickupComponent overlap callbacks. These are
    // deliberately actor-based so one pickup with several primitive components
    // remains a single candidate.
    // 按拾取演员累计重叠次数，避免多个碰撞组件产生重复候选。
    void RegisterOverlappingInteractPickup(AActor* PickupActor);
    // 递减重叠次数，归零时移除拾取候选。
    void UnregisterOverlappingInteractPickup(AActor* PickupActor);
    // 清理失效候选并按距离排序，实际返回最近候选。
    AActor* GetRandomOverlappingInteractPickup();
    // 读取已登记候选数量。
    int32 GetOverlappingInteractPickupCount() const;

    UFUNCTION(Server, Reliable)
    // 服务器将槽位交换请求交给背包组件，并记录结果。
    void ServerRequestSwap(int32 SourceSlotIndex, int32 TargetSlotIndex);

    UFUNCTION(Server, Reliable)
    // 服务器使用指定槽位物品，目标为当前受控角色。
    void ServerRequestUse(int32 SlotIndex);

    UFUNCTION(Server, Reliable)
    // 服务器按角色前方位置请求背包丢弃整组物品。
    void ServerRequestDropWholeStack(int32 SlotIndex);

    UFUNCTION(BlueprintCallable, Category = "Backpack|UI")
    // 依据当前开关状态打开或关闭背包。
    void ToggleBackpackUI();
    UFUNCTION(BlueprintCallable, Category = "Backpack|UI")
    // 创建背包并绑定视图模型与装备面板；首帧布局完成后显示，避免模板闪烁。
    void OpenBackpackUI();
    UFUNCTION(BlueprintCallable, Category = "Backpack|UI")
    // 移除背包和装备面板，恢复游戏与界面输入模式。
    void CloseBackpackUI();
    UFUNCTION(BlueprintPure, Category = "Backpack|UI")
    // 读取背包界面开关状态。
    bool IsBackpackUIOpen() const
    {
        return bBackpackUIOpen;
    }

    UFUNCTION(BlueprintCallable)
    // 创建或显示技能范围指示器，使用可选贴花材质。
    void ShowMagicCircle(UMaterialInterface* DecalMaterial = nullptr);

    UFUNCTION(BlueprintCallable)
    // 移除技能范围指示器。
    void HideMagicCircle();

protected:
    // 开始游戏后尝试初始化本地输入。
    virtual void BeginPlay() override;
    // 绑定移动动作与能力标签的按下、松开、持续事件。
    virtual void SetupInputComponent() override;

private:
    friend class FPlayerBasicInputTest;
    // 仅为已有本地玩家的控制器安装基础输入映射与鼠标模式。
    void InitializeLocalInput();
    UPROPERTY(EditAnywhere, Category = "Input")
    TObjectPtr<UInputMappingContext> PlayerContext;

    UPROPERTY(EditAnywhere, Category = "Input")
    TObjectPtr<UInputAction> MoveAction;

    UPROPERTY(EditAnywhere, Category = "Input")
    TObjectPtr<UInputAction> ShiftAction;

    UPROPERTY(EditDefaultsOnly, Category = "Backpack|UI")
    TSubclassOf<UUserWidget> BackpackWidgetClass;

    UPROPERTY(Transient)
    TObjectPtr<UUserWidget> BackpackWidget;
    UPROPERTY(Transient)
    TObjectPtr<class UEquipmentPanel> EquipmentWidget;

    bool bBackpackUIOpen = false;
    TWeakObjectPtr<AActor> FocusedEquipmentPickup;
    float EquipmentFocusElapsed = 0.f;

    // 在背包和状态门禁允许时，将二维输入转换为控制器朝向的移动向量。
    void Move(const FInputActionValue& InputActionValue);
    // 根据技能指示器选择射线通道，并维护新旧目标高亮；封锁时清除目标。
    void CursorTrace();

    TObjectPtr<AActor> LastActor;
    TObjectPtr<AActor> ThisActor;
    // 通过高亮接口启用有效目标的高亮表现。
    static void HighlightActor(AActor* InActor);
    // 通过高亮接口取消有效目标的高亮表现。
    static void UnHighlightActor(AActor* InActor);
    FHitResult CursorHit;
    // 优先处理背包与拾取，其余按下事件经状态门禁送入能力系统。
    void AbilityInputTagPressed(FGameplayTag InputTag);
    // 始终转交松开事件，使已激活技能能在界面或眩晕封锁期间结束。
    void AbilityInputTagReleased(FGameplayTag InputTag);
    // 持续输入通过背包和状态门禁后交给能力系统。
    void AbilityInputTagHeld(FGameplayTag InputTag);
    // 向服务器提交拾取请求，具体候选由服务器决定。
    void RequestPickupFocusedActor();

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UMyInputConfig> InputConfig;
    UPROPERTY()
    TObjectPtr<UMyAbilitySystemComponent> MyAbilitySystemComponent;

    // 根据当前受控角色刷新并返回自定义能力系统。
    UMyAbilitySystemComponent* GetASC();

    UPROPERTY(EditDefaultsOnly, Category = "Backpack", meta = (ClampMin = "0"))
    float PickupRequestRange = 250.f;
    TMap<TWeakObjectPtr<AActor>, int32> OverlappingInteractPickups;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USplineComponent> Spline;

    UPROPERTY(EditDefaultsOnly)
    TObjectPtr<UNiagaraSystem> ClickNiagaraSystem;

    UPROPERTY(EditDefaultsOnly)
    TSubclassOf<UDamageTextComponent> DamageTextComponentClass;

    UPROPERTY(EditDefaultsOnly)
    TSubclassOf<AMagicCircle> MagicCircleClass;

    UPROPERTY()
    TObjectPtr<AMagicCircle> MagicCircle;

    // 将技能范围指示器移动到当前光标命中点。
    void UpdateMagicCircleLocation();
};
