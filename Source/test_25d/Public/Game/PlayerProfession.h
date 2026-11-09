// Save selection supplies the profession used for each player's pawn and HUD.
#pragma once
#include "CoreMinimal.h"
#include "PlayerProfession.generated.h"

UENUM(BlueprintType)
enum class EPlayerProfession : uint8
{
    Unselected,
    Mage,
    Stander
};
