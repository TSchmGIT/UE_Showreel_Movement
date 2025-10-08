// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "InteractAction.h"
#include "GameFramework/Actor.h"
#include "Interactable.generated.h"

UCLASS()
class SHOWREEL_3CS_API AInteractable : public AActor
{
	GENERATED_BODY()
public:
	// Pick a different Action per placed instance
	UPROPERTY(EditAnywhere, Instanced, Category="Interact")
	UInteractAction* Action;

	UFUNCTION(BlueprintCallable)
	void Interact(AActor* InstigatorActor) const
	{
		if (Action) { Action->Execute(InstigatorActor); }
	}
};