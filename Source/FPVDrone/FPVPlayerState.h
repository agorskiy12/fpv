#pragma once

#include "CoreMinimal.h"
#include "FPVFactions.h"
#include "GameFramework/PlayerState.h"
#include "FPVPlayerState.generated.h"

/**
 * Holds which side the operator is fighting for.
 *
 * On the player state rather than the pawn because the pawn is expendable by design -- it is
 * destroyed on every strike. Allegiance has to outlive the aircraft.
 */
UCLASS()
class FPVDRONE_API AFPVPlayerState : public APlayerState, public IFactionMember
{
	GENERATED_BODY()

public:
	/** Defaults to Neutral so the team system lands without changing any behaviour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Faction")
	EFaction Faction = EFaction::Neutral;

	virtual EFaction GetFaction() const override { return Faction; }

	UFUNCTION(BlueprintCallable, Category = "Faction")
	void SetFaction(EFaction NewFaction);
};
