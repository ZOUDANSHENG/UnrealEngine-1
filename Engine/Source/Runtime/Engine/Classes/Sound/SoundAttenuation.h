// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once
 
#include "SoundAttenuation.generated.h"

UENUM()
enum ESoundDistanceModel
{
	ATTENUATION_Linear,
	ATTENUATION_Logarithmic,
	ATTENUATION_Inverse,
	ATTENUATION_LogReverse,
	ATTENUATION_NaturalSound,
	ATTENUATION_MAX,
};

UENUM()
enum ESoundDistanceCalc
{
	SOUNDDISTANCE_Normal,
	SOUNDDISTANCE_InfiniteXYPlane,
	SOUNDDISTANCE_InfiniteXZPlane,
	SOUNDDISTANCE_InfiniteYZPlane,
	SOUNDDISTANCE_MAX,
};

UENUM()
namespace EAttenuationShape
{
	enum Type
	{
		Sphere,
		Capsule,
		Box,
		Cone
	};
}

USTRUCT(BlueprintType)
struct ENGINE_API FAttenuationSettings
{
	GENERATED_USTRUCT_BODY()

	/* 
	 The settings for attenuating. 
	 */

	/* Enable attenuation via volume. */
	UPROPERTY(EditAnywhere, Category=Attenuation )
	uint32 bAttenuate:1;

	/* Enable the source to be positioned in 3D. */
	UPROPERTY(EditAnywhere, Category=Attenuation )
	uint32 bSpatialize:1;

	/* Enable attenuation via low pass filter. */
	UPROPERTY(EditAnywhere, Category=LowPassFilter )
	uint32 bAttenuateWithLPF:1;

	/* The type of volume versus distance algorithm to use for the attenuation model. */
	UPROPERTY(EditAnywhere, Category=Attenuation )
	TEnumAsByte<enum ESoundDistanceModel> DistanceAlgorithm;

	UPROPERTY()
	TEnumAsByte<enum ESoundDistanceCalc> DistanceType_DEPRECATED;

	/* The shape of the attenuation volume. */
	UPROPERTY(EditAnywhere, Category=Attenuation )
	TEnumAsByte<enum EAttenuationShape::Type> AttenuationShape;

	/* The volume at maximum distance in deciBels. */
	UPROPERTY(EditAnywhere, Category=Attenuation, meta=(DisplayName = "dB Attenuation At Max", ClampMax = "0" ))
	float dBAttenuationAtMax;

	/** At what distance we start transforming into omnidirectional soundsource */
	UPROPERTY(EditAnywhere, Category=Attenuation, meta=(ClampMin = "0"))
	float OmniRadius;

	UPROPERTY()
	float RadiusMin_DEPRECATED;

	UPROPERTY()
	float RadiusMax_DEPRECATED;

	UPROPERTY(EditAnywhere, Category=Attenuation)
	FVector AttenuationShapeExtents;

	UPROPERTY(EditAnywhere, Category=Attenuation, meta=(ClampMin = "0"))
	float ConeOffset;

	UPROPERTY(EditAnywhere, Category=Attenuation, meta=(ClampMin = "0"))
	float FalloffDistance;

	/* The range at which to start applying a low passfilter. */
	UPROPERTY(EditAnywhere, Category=LowPassFilter )
	float LPFRadiusMin;

	/* The range at which to apply the maximum amount of low pass filter. */
	UPROPERTY(EditAnywhere, Category=LowPassFilter )
	float LPFRadiusMax;

	FAttenuationSettings()
		: bAttenuate(true)
		, bSpatialize(true)
		, bAttenuateWithLPF(false)
		, DistanceAlgorithm(ATTENUATION_Linear)
		, DistanceType_DEPRECATED(SOUNDDISTANCE_Normal)
		, AttenuationShape(EAttenuationShape::Sphere)
		, dBAttenuationAtMax(-60.f)
		, RadiusMin_DEPRECATED(400.f)
		, RadiusMax_DEPRECATED(4000.f)
		, AttenuationShapeExtents(400.f, 0.f, 0.f)
		, ConeOffset(0.f)
		, FalloffDistance(3600.f)
		, LPFRadiusMin(3000.f)
		, LPFRadiusMax(6000.f)
	{
	}

	bool operator==(const FAttenuationSettings& Other) const;
	void PostSerialize(const FArchive& Ar);

	void ApplyAttenuation( const FTransform& SoundTransform, const FVector ListenerLocation, float& Volume, float& HighFrequencyGain ) const;

	struct AttenuationShapeDetails
	{
		FVector Extents;
		float Falloff;
		float ConeOffset;
	};

	void CollectAttenuationShapesForVisualization(TMap<EAttenuationShape::Type, AttenuationShapeDetails>& ShapeDetailsMap) const;
	float GetMaxDimension() const;

private:

	float AttenuationEvalBox(const FTransform& SoundLocation, const FVector ListenerLocation) const;
	float AttenuationEvalCapsule(const FTransform& SoundLocation, const FVector ListenerLocation) const;
	float AttenuationEvalCone(const FTransform& SoundLocation, const FVector ListenerLocation) const;
};

template<>
struct TStructOpsTypeTraits<FAttenuationSettings> : public TStructOpsTypeTraitsBase
{
	enum 
	{
		WithPostSerialize = true,
	};
};

/** 
 * Defines how a sounds changes volume with distance to the listener
 */
UCLASS(BlueprintType, hidecategories=Object, editinlinenew, MinimalAPI)
class USoundAttenuation : public UObject
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, Category=Settings)
	FAttenuationSettings Attenuation;
};
