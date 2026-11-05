#include "NativeGameplayTags.h"

// Register before saved GA defaults are deserialized, as with the existing prototype batches.
#define BATCH6_TAG(Name) \
UE_DEFINE_GAMEPLAY_TAG_STATIC(Ability##Name, "Abilities.Batch6." #Name); \
UE_DEFINE_GAMEPLAY_TAG_STATIC(Cooldown##Name, "Cooldown.Batch6." #Name);
BATCH6_TAG(StarFlameHalo)
BATCH6_TAG(PetrifyingGaze)
BATCH6_TAG(SolarFireball)
BATCH6_TAG(GlacialStorm)
BATCH6_TAG(FissionChain)
BATCH6_TAG(EarthShockwave)
#undef BATCH6_TAG
