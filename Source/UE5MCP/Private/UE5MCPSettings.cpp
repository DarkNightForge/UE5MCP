// Copyright Epic Games, Inc. All Rights Reserved.

#include "UE5MCPSettings.h"

UUE5MCPSettings::UUE5MCPSettings()
{
	// Default spawn policy: a deliberately tiny set of harmless, always-available
	// engine classes and basic-shape meshes — enough to build and light a demo
	// scene, nothing that can run logic. Projects widen this list consciously in
	// Project Settings, never implicitly.
	SpawnClassAllowlist = {
		TEXT("/Script/Engine.StaticMeshActor"),
		TEXT("/Script/Engine.PointLight"),
		TEXT("/Script/Engine.CameraActor"),
	};
	SpawnMeshAllowlist = {
		TEXT("/Engine/BasicShapes/Cube.Cube"),
		TEXT("/Engine/BasicShapes/Sphere.Sphere"),
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"),
		TEXT("/Engine/BasicShapes/Cone.Cone"),
		TEXT("/Engine/BasicShapes/Plane.Plane"),
	};

	// Default property policy: a deliberately tiny, visual-only set on the standard
	// light components — enough to dim/recolor a light in a demo with preview + undo,
	// nothing that runs logic or touches assets. Projects widen this consciously.
	auto FloatEntry = [](const TCHAR* Class, const TCHAR* Prop, double Min, double Max)
	{
		FUE5MCPPropertyAllowEntry Entry;
		Entry.ClassPath = Class;
		Entry.PropertyName = Prop;
		Entry.Type = TEXT("float");
		Entry.bHasRange = true;
		Entry.Min = Min;
		Entry.Max = Max;
		return Entry;
	};
	auto ColorEntry = [](const TCHAR* Class, const TCHAR* Prop)
	{
		FUE5MCPPropertyAllowEntry Entry;
		Entry.ClassPath = Class;
		Entry.PropertyName = Prop;
		Entry.Type = TEXT("color");
		return Entry;
	};
	PropertyAllowlist = {
		FloatEntry(TEXT("/Script/Engine.PointLightComponent"), TEXT("Intensity"), 0.0, 1000000.0),
		FloatEntry(TEXT("/Script/Engine.SpotLightComponent"), TEXT("Intensity"), 0.0, 1000000.0),
		FloatEntry(TEXT("/Script/Engine.DirectionalLightComponent"), TEXT("Intensity"), 0.0, 150.0),
		ColorEntry(TEXT("/Script/Engine.PointLightComponent"), TEXT("LightColor")),
		ColorEntry(TEXT("/Script/Engine.SpotLightComponent"), TEXT("LightColor")),
		ColorEntry(TEXT("/Script/Engine.DirectionalLightComponent"), TEXT("LightColor")),
	};
}
