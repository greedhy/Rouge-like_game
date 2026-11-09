#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Game/PlayerProfession.h"
#include "RoomMenuWidget.generated.h"
class UButton;
class UTextBlock;
class UEditableTextBox;
class UWidgetSwitcher;
class UVerticalBox;
class UBorder;
class UTexture2D;
class URoomSubsystem;

/** Native widget tree also usable as a Widget Blueprint parent. */
UCLASS()
class TEST_25D_API URoomMenuWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    URoomMenuWidget(const FObjectInitializer& ObjectInitializer);
    UFUNCTION(BlueprintCallable)
    void ShowModeSelection();
    void ShowProfessionSelection();
    UFUNCTION(BlueprintCallable)
    void ShowLobby();

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& Geometry, float Delta) override;

private:
    UPROPERTY()
    TObjectPtr<UTexture2D> ButtonNormal;
    UPROPERTY()
    TObjectPtr<UTexture2D> ButtonHover;
    UPROPERTY()
    TObjectPtr<UTexture2D> Background;
    UPROPERTY()
    TObjectPtr<UWidgetSwitcher> Pages;
    UPROPERTY()
    TObjectPtr<UTextBlock> StatusText;
    UPROPERTY()
    TObjectPtr<UTextBlock> PasswordText;
    UPROPERTY()
    TObjectPtr<UTextBlock> MembersText;
    UPROPERTY()
    TObjectPtr<UTextBlock> ModalText;
    UPROPERTY()
    TObjectPtr<UTextBlock> LobbyHint;
    UPROPERTY()
    TObjectPtr<UEditableTextBox> JoinPassword;
    UPROPERTY()
    TObjectPtr<UEditableTextBox> NewPassword;
    UPROPERTY()
    TObjectPtr<UBorder> Modal;
    UPROPERTY()
    TObjectPtr<UButton> ChangeButton;
    UPROPERTY()
    TObjectPtr<UButton> StartButton;
    UPROPERTY()
    TArray<TObjectPtr<UButton>> ActionButtons;
    FDelegateHandle ChangeHandle;
    float RefreshElapsed = 0;
    bool bLobby = false;
    URoomSubsystem* Rooms() const;
    void BuildTree();
    void Refresh();
    UTextBlock* AddText(UVerticalBox* Parent, const FString& Text, int32 Size = 22);
    UButton* AddButton(UVerticalBox* Parent, FName Name, const FString& Text);
    UEditableTextBox* AddPassword(UVerticalBox* Parent, FName Name);
    UVerticalBox* AddPage(const FString& Title, const FString& Subtitle);
    UFUNCTION()
    void SingleClicked();
    UFUNCTION()
    void SwordTrialClicked();
    UFUNCTION()
    void MageProfessionClicked();
    UFUNCTION()
    void StanderProfessionClicked();
    void ConfirmProfession(EPlayerProfession Profession);
    UPROPERTY()
    TObjectPtr<UTextBlock> ProfessionError;
    UPROPERTY()
    TObjectPtr<UVerticalBox> ProfessionPage;
    UFUNCTION()
    void MultiplayerClicked();
    UFUNCTION()
    void CreateClicked();
    UFUNCTION()
    void JoinPageClicked();
    UFUNCTION()
    void JoinClicked();
    UFUNCTION()
    void BackClicked();
    UFUNCTION()
    void LeaveClicked();
    UFUNCTION()
    void ChangeClicked();
    UFUNCTION()
    void StartClicked();
    UFUNCTION()
    void DismissClicked();
};
