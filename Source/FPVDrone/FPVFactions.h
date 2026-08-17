#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "FPVFactions.generated.h"

/**
 * Who something belongs to.
 *
 * Neutral is not padding. Every target in this project is currently, by definition, something to
 * destroy -- so without a neutral value every prop added to a level silently becomes an
 * objective. Scenery, civilians and the world itself are Neutral, and that is what allows them
 * to be destructible without being worth anything.
 */
UENUM(BlueprintType)
enum class EFaction : uint8
{
	Neutral		UMETA(DisplayName = "Neutral"),
	Russia		UMETA(DisplayName = "Russia"),
	NATO		UMETA(DisplayName = "NATO")
};

/** The relationship between two factions, from the first one's point of view. */
UENUM(BlueprintType)
enum class EFactionAttitude : uint8
{
	Neutral,
	Friendly,
	Hostile
};

UINTERFACE(MinimalAPI, BlueprintType)
class UFactionMember : public UInterface
{
	GENERATED_BODY()
};

/**
 * Implemented by anything that belongs to a side.
 *
 * An interface rather than a property on ADroneTarget, because the player pawn also needs a
 * faction and is not a target. Two separate hierarchies, one question.
 */
class FPVDRONE_API IFactionMember
{
	GENERATED_BODY()

public:
	virtual EFaction GetFaction() const = 0;
};

/**
 * The single place allegiance is interpreted.
 *
 * Everything -- damage scoring, objectives, HUD colours, AI target selection -- asks these
 * functions rather than comparing factions itself. Scattered comparisons drift, and the first
 * symptom of drift is something scoring when it should not.
 */
namespace FPVFaction
{
	/** Same side is friendly, different sides are hostile, anything involving Neutral is neutral. */
	FPVDRONE_API EFactionAttitude GetAttitude(EFaction Source, EFaction Target);

	/** Convenience overload. Actors that do not declare a faction are treated as Neutral. */
	FPVDRONE_API EFactionAttitude GetAttitude(const AActor* Source, const AActor* Target);

	/** Faction of an actor, or Neutral if it does not implement IFactionMember. */
	FPVDRONE_API EFaction GetFactionOf(const AActor* Actor);

	FPVDRONE_API bool IsHostile(const AActor* Source, const AActor* Target);
	FPVDRONE_API bool IsFriendly(const AActor* Source, const AActor* Target);

	FPVDRONE_API const TCHAR* GetDisplayName(EFaction Faction);

	/**
	 * Team index for UE's AI perception, which works in FGenericTeamId rather than this enum.
	 *
	 * Returned as a plain integer deliberately. Using FGenericTeamId directly would mean adding
	 * AIModule to the build for a bridge nothing crosses yet; when infantry perception arrives,
	 * implementing IGenericTeamAgentInterface on top of this is a few lines per class.
	 * 0 = Russia, 1 = NATO, 255 = no team, matching FGenericTeamId::NoTeam.
	 */
	FPVDRONE_API uint8 ToTeamIndex(EFaction Faction);
	FPVDRONE_API EFaction FromTeamIndex(uint8 TeamIndex);
}
