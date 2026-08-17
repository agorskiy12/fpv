#include "FPVPlayerState.h"
#include "FPVDrone.h"

void AFPVPlayerState::SetFaction(EFaction NewFaction)
{
	Faction = NewFaction;
	UE_LOG(LogFPV, Log, TEXT("Operator faction set to %s"), FPVFaction::GetDisplayName(NewFaction));
}
