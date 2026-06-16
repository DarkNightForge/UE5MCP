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
	auto EnumEntry = [](const TCHAR* Class, const TCHAR* Prop)
	{
		FUE5MCPPropertyAllowEntry Entry;
		Entry.ClassPath = Class;
		Entry.PropertyName = Prop;
		Entry.Type = TEXT("enum");
		return Entry;
	};
	// A struct-member sub-path with its paired bOverride_ flag (so the value takes effect).
	auto StructFloatEntry = [](const TCHAR* Class, const TCHAR* PathProp, const TCHAR* Override, double Min, double Max)
	{
		FUE5MCPPropertyAllowEntry Entry;
		Entry.ClassPath = Class;
		Entry.PropertyName = PathProp;
		Entry.Type = TEXT("float");
		Entry.bHasRange = true;
		Entry.Min = Min;
		Entry.Max = Max;
		Entry.OverrideFlag = Override;
		return Entry;
	};
	auto AssetEntry = [](const TCHAR* Class, const TCHAR* Prop, const TCHAR* AssetClass)
	{
		FUE5MCPPropertyAllowEntry Entry;
		Entry.ClassPath = Class;
		Entry.PropertyName = Prop;
		Entry.Type = TEXT("asset");
		Entry.AssetClass = AssetClass;
		return Entry;
	};
	PropertyAllowlist = {
		FloatEntry(TEXT("/Script/Engine.PointLightComponent"), TEXT("Intensity"), 0.0, 1000000.0),
		FloatEntry(TEXT("/Script/Engine.SpotLightComponent"), TEXT("Intensity"), 0.0, 1000000.0),
		FloatEntry(TEXT("/Script/Engine.DirectionalLightComponent"), TEXT("Intensity"), 0.0, 150.0),
		ColorEntry(TEXT("/Script/Engine.PointLightComponent"), TEXT("LightColor")),
		ColorEntry(TEXT("/Script/Engine.SpotLightComponent"), TEXT("LightColor")),
		ColorEntry(TEXT("/Script/Engine.DirectionalLightComponent"), TEXT("LightColor")),
		// Enum value-kind: set a light's intensity units by name (e.g. "Lumens", "Candelas").
		EnumEntry(TEXT("/Script/Engine.LocalLightComponent"), TEXT("IntensityUnits")),
		// Struct-member sub-path + paired override: a camera's post-process bloom intensity.
		StructFloatEntry(TEXT("/Script/Engine.CameraComponent"), TEXT("PostProcessSettings.BloomIntensity"), TEXT("bOverride_BloomIntensity"), 0.0, 8.0),
		// Asset-ref value-kind: assign an allowlisted static mesh to a mesh component.
		AssetEntry(TEXT("/Script/Engine.StaticMeshComponent"), TEXT("StaticMesh"), TEXT("/Script/Engine.StaticMesh")),
	};
}
