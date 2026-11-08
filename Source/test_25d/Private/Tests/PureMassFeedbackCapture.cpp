/** 主要调用链：控制台PureMass.QA.CaptureUI -> 定位指定PIE实例的Slate视口 -> 保存含UI的PNG与伤害组件状态JSON。 */
// Editor-only visual regression helper. Capture a specific PIE client's Slate viewport,
// not HighResShot (which omits UI) or the global Shot request (first viewport wins).
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "UI/Widget/DamageTextComponent.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"

static FAutoConsoleCommand PureMassCaptureFeedbackUI(
    TEXT("PureMass.QA.CaptureUI"),
    TEXT(
        "CaptureUI <PIE instance> <name>: save Slate pixels and damage widget screen-layer state under Saved/PureMass/FeedbackUI."),
    FConsoleCommandWithArgsDelegate::CreateLambda(
        [](const TArray<FString>& Args)
        {
            if (Args.Num() != 2 || !GEngine || !FSlateApplication::IsInitialized())
                return;
            const int32 Instance = FCString::Atoi(*Args[0]);
            for (const auto& Context : GEngine->GetWorldContexts())
            {
                UWorld* World = Context.World();
                if (Context.WorldType != EWorldType::PIE || Context.PIEInstance != Instance || !World)
                    continue;
                auto* ViewportClient = World->GetGameViewport();
                auto Viewport = ViewportClient ? ViewportClient->GetGameViewportWidget() : nullptr;
                if (!Viewport.IsValid())
                    return;
                TArray<FColor> Pixels;
                FIntVector Size;
                if (!FSlateApplication::Get().TakeScreenshot(Viewport.ToSharedRef(), Pixels, Size))
                    return;
                for (auto& Pixel : Pixels)
                    Pixel.A = 255;
                TArray64<uint8> PNG;
                FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
                const FString Folder = FPaths::ProjectSavedDir() / TEXT("PureMass/FeedbackUI");
                IFileManager::Get().MakeDirectory(*Folder, true);
                const FString Base = Folder / FPaths::GetCleanFilename(Args[1]);
                if (!FFileHelper::SaveArrayToFile(PNG, *(Base + TEXT(".png"))))
                    return;
                auto Report = MakeShared<FJsonObject>();
                Report->SetStringField(TEXT("world"), World->GetPathName());
                Report->SetNumberField(TEXT("pie_instance"), Instance);
                Report->SetNumberField(TEXT("width"), Size.X);
                Report->SetNumberField(TEXT("height"), Size.Y);
                TArray<TSharedPtr<FJsonValue>> Numbers;
                const auto* Added =
                    FindFProperty<FBoolProperty>(UWidgetComponent::StaticClass(), TEXT("bAddedToScreen"));
                for (TObjectIterator<UDamageTextComponent> It; It; ++It)
                {
                    if (It->GetWorld() != World || !It->IsRegistered() || !It->GetOwner())
                        continue;
                    auto Item = MakeShared<FJsonObject>();
                    Item->SetStringField(TEXT("component"), It->GetPathName());
                    Item->SetBoolField(TEXT("owner_hidden"), It->GetOwner()->IsHidden());
                    Item->SetBoolField(TEXT("added_to_screen"), Added && Added->GetPropertyValue_InContainer(*It));
                    if (auto* Widget = It->GetUserWidgetObject())
                        if (auto* Label = Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("Text_Damage"))))
                            Item->SetStringField(TEXT("text"), Label->GetText().ToString());
                    Numbers.Add(MakeShared<FJsonValueObject>(Item));
                }
                Report->SetArrayField(TEXT("numbers"), Numbers);
                FString JSON;
                FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&JSON));
                FFileHelper::SaveStringToFile(JSON, *(Base + TEXT(".json")));
                return;
            }
        }));
#endif
