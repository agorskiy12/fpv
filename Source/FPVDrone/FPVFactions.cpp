#include "FPVFactions.h"

#include "GameFramework/Actor.h"

namespace FPVFaction
{
	EFactionAttitude GetAttitude(EFaction Source, EFaction Target)
	{
		// Neutral is contagious: anything involving it is neutral, regardless of the other side.
		// That is what keeps scenery and civilians out of the scoring entirely.
		if (Source == EFaction::Neutral || Target == EFaction::Neutral)
		{
			return EFactionAttitude::Neutral;
		}

		return (Source == Target) ? EFactionAttitude::Friendly : EFactionAttitude::Hostile;
	}

	EFaction GetFactionOf(const AActor* Actor)
	{
		if (!Actor)
		{
			return EFaction::Neutral;
		}

		// Const-correctness: the interface query needs a non-const object, but reading a faction
		// does not mutate anything.
		if (const IFactionMember* Member = Cast<IFactionMember>(Actor))
		{
			return Member->GetFaction();
		}

		return EFaction::Neutral;
	}

	EFactionAttitude GetAttitude(const AActor* Source, const AActor* Target)
	{
		return GetAttitude(GetFactionOf(Source), GetFactionOf(Target));
	}

	bool IsHostile(const AActor* Source, const AActor* Target)
	{
		return GetAttitude(Source, Target) == EFactionAttitude::Hostile;
	}

	bool IsFriendly(const AActor* Source, const AActor* Target)
	{
		return GetAttitude(Source, Target) == EFactionAttitude::Friendly;
	}

	const TCHAR* GetDisplayName(EFaction Faction)
	{
		switch (Faction)
		{
		case EFaction::Russia: return TEXT("RUSSIA");
		case EFaction::NATO:   return TEXT("NATO");
		default:               return TEXT("NEUTRAL");
		}
	}

	uint8 ToTeamIndex(EFaction Faction)
	{
		switch (Faction)
		{
		case EFaction::Russia: return 0;
		case EFaction::NATO:   return 1;
		default:               return 255;   // matches FGenericTeamId::NoTeam
		}
	}

	EFaction FromTeamIndex(uint8 TeamIndex)
	{
		switch (TeamIndex)
		{
		case 0:  return EFaction::Russia;
		case 1:  return EFaction::NATO;
		default: return EFaction::Neutral;
		}
	}
}
