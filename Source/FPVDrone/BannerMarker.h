#pragma once

#include "CoreMinimal.h"
#include "FPVFactions.h"
#include "GameFramework/Actor.h"
#include "BannerMarker.generated.h"

class UStaticMeshComponent;

/**
 * A flag marking which side holds a piece of ground.
 *
 * Built from stacked coloured blocks rather than a texture, so it needs no assets and reads at
 * the distance it is actually seen from — a flag with fine detail is a smear from a drone at
 * fifty metres, whereas three horizontal bands of colour are unmistakable.
 *
 * Deliberately placed away from the operator it belongs to. The banner says whose ground this
 * is; it must never say where the operator is standing, or the entire hunt collapses into
 * flying to the nearest flag.
 */
UCLASS()
class FPVDRONE_API ABannerMarker : public AActor
{
	GENERATED_BODY()

public:
	ABannerMarker();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	/** Which side's colours to fly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Banner")
	EFaction Faction = EFaction::Neutral;

	/** Height of the mast in centimetres. Tall, because it has to be seen from the air. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Banner")
	float MastHeight = 1400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Banner")
	float FlagWidth = 800.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Banner")
	float FlagHeight = 520.f;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Banner")
	TObjectPtr<USceneComponent> BannerRoot;

	UPROPERTY(VisibleAnywhere, Category = "Banner")
	TObjectPtr<UStaticMeshComponent> Mast;

	/** Horizontal bands, top to bottom. Three covers the tricolour; two are used for Ukraine. */
	UPROPERTY(VisibleAnywhere, Category = "Banner")
	TArray<TObjectPtr<UStaticMeshComponent>> Bands;

	static constexpr int32 MaxBands = 3;

private:
	void RebuildBanner();

	/** Band colours for a faction, top to bottom. Empty for Neutral. */
	static void GetBandColours(EFaction Faction, TArray<FLinearColor>& OutColours);

	void PaintBand(UStaticMeshComponent* Band, const FLinearColor& Colour);
};
