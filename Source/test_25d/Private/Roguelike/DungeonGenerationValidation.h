#pragma once
#include "CoreMinimal.h"

class UDungeonGenerationProfile;
namespace DungeonGeneration
{
    bool CheckProfile(const UDungeonGenerationProfile* Profile, FString& Error);
}
