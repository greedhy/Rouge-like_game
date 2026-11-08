// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySystem/Passive/PassiveNiagaraComponent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "MyGameplayTags.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "Interface/CombatInterface.h"

UPassiveNiagaraComponent::UPassiveNiagaraComponent()
{
    bAutoActivate = false;
}

void UPassiveNiagaraComponent::BeginPlay()
{
    Super::BeginPlay();

    if (UMyAbilitySystemComponent* MyASC =
            Cast<UMyAbilitySystemComponent>(UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner())))
    {
        MyASC->ActivatePassiveEffect.AddUObject(this, &UPassiveNiagaraComponent::OnPassiveActivate);
        ActivateIfEquipped(MyASC);
    }
    else if (ICombatInterface* CombatInterface = Cast<ICombatInterface>(GetOwner()))
    {
        CombatInterface->GetOnASCRegisteredDelegate().AddLambda(
            [this](UAbilitySystemComponent* ASC)
            {
                if (UMyAbilitySystemComponent* MyASC = Cast<UMyAbilitySystemComponent>(
                        UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner())))
                {
                    MyASC->ActivatePassiveEffect.AddUObject(this, &UPassiveNiagaraComponent::OnPassiveActivate);
                    ActivateIfEquipped(MyASC);
                }
            });
    }
}

void UPassiveNiagaraComponent::ActivateIfEquipped(UMyAbilitySystemComponent* MyASC)
{
    if (MyASC->bStartupAbilityGiven)
    {
        if (MyASC->GetStatusFromAbilityTag(PassiveSpellTag) == FMyGameplayTags::Get().Abilities_Status_Equipped)
        {
            Activate();
        }
    }
}

void UPassiveNiagaraComponent::OnPassiveActivate(const FGameplayTag& AbilityTag, bool bActivate)
{
    if (AbilityTag.MatchesTagExact(PassiveSpellTag))
    {
        if (bActivate && !IsActive())
        {
            Activate();
        }
        else
        {
            Deactivate();
        }
    }
}
