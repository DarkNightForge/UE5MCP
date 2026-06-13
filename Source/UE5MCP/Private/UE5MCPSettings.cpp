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
}
