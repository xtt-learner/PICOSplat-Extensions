/*
  Copyright (c) 2025 PICO Technology Co., Ltd. See LICENSE.md.
*/

#pragma once

#include <type_traits>

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "HLSLTypeAliases.h"
#include "SceneView.h"
#include "ShaderParameterStruct.h"

namespace PICO::Splat::Shaders
{

/**
 * For compute shader pre-passes only.
 * TODO(seth): This needs to be tuned for performance.
 */
constexpr uint32 THREAD_GROUP_SIZE_X = 32;

BEGIN_SHADER_PARAMETER_STRUCT(FPackedPositionParameters, )
SHADER_PARAMETER(FVector3f, pos_min_cm)
SHADER_PARAMETER(FVector3f, pos_scale_cm)
SHADER_PARAMETER_SRV(Buffer<uint>, positions)
END_SHADER_PARAMETER_STRUCT()

/**
 * Calculates distances to each splat, for GPU sorting.
 */
class FComputeDistanceCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FComputeDistanceCS);
	SHADER_USE_PARAMETER_STRUCT(FComputeDistanceCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
	SHADER_PARAMETER(FMatrix44f, local_to_clip)
	SHADER_PARAMETER(uint32, num_splats)
	SHADER_PARAMETER_STRUCT_INCLUDE(FPackedPositionParameters, Positions)
	SHADER_PARAMETER_UAV(RWBuffer<uint>, indices)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, distances)
	END_SHADER_PARAMETER_STRUCT()

public:
	static void ModifyCompilationEnvironment(
		const FGlobalShaderPermutationParameters& Parameters,
		FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(
			TEXT("THREAD_GROUP_SIZE_X"), THREAD_GROUP_SIZE_X);
	}
};

/**
 * Calculates 2x2 transform for each splat.
 */
class FComputeTransformCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FComputeTransformCS);
	SHADER_USE_PARAMETER_STRUCT(FComputeTransformCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
	SHADER_PARAMETER(FMatrix44f, local_to_view)
	SHADER_PARAMETER(float, two_focal_length)
	SHADER_PARAMETER(uint32, num_splats)
	SHADER_PARAMETER_STRUCT_INCLUDE(FPackedPositionParameters, Positions)
	SHADER_PARAMETER_SRV(Buffer<uint2>, covariances)
	SHADER_PARAMETER_UAV(RWBuffer<float4>, transforms)
	END_SHADER_PARAMETER_STRUCT()

public:
	static void ModifyCompilationEnvironment(
		const FGlobalShaderPermutationParameters& Parameters,
		FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(
			TEXT("THREAD_GROUP_SIZE_X"), THREAD_GROUP_SIZE_X);
	}
};

/**
 * For controlling shader parameters in RenderSplatVS.
 */
enum class ESortingDevice : uint8
{
	GPU = 0,
	CPU = 1
};

/**
 * Parameters shared between both CPU and GPU sorting versions.
 */
BEGIN_SHADER_PARAMETER_STRUCT(FRenderSplatSharedParameters, )
SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
SHADER_PARAMETER_STRUCT_REF(
	FInstancedViewUniformShaderParameters, InstancedView)
SHADER_PARAMETER(FMatrix44f, local_to_world)
SHADER_PARAMETER_STRUCT_INCLUDE(FPackedPositionParameters, Positions)
SHADER_PARAMETER_SRV(Buffer<float4>, transforms)
SHADER_PARAMETER_SRV(Buffer<float4>, colors)
END_SHADER_PARAMETER_STRUCT()

/**
 * Per splat, creates a containing triangle.
 */
template <ESortingDevice Device>
class FRenderSplatVS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRenderSplatVS);
	SHADER_USE_PARAMETER_STRUCT(FRenderSplatVS, FGlobalShader);

	// With HLSLTypeAliases.h, gives access to HLSL style types outside parameter
	// struct.
	using T = std::conditional_t<
		Device == ESortingDevice::GPU,
		UE::HLSL::uint,
		UE::HLSL::uint2>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FRenderSplatSharedParameters, Shared)
	SHADER_PARAMETER_SRV(Buffer<T>, Indices)
	END_SHADER_PARAMETER_STRUCT()

public:
	static void ModifyCompilationEnvironment(
		const FGlobalShaderPermutationParameters& Parameters,
		FShaderCompilerEnvironment& OutEnvironment)
	{
		if constexpr (Device == ESortingDevice::GPU)
		{
			FGlobalShader::ModifyCompilationEnvironment(
				Parameters, OutEnvironment);
			OutEnvironment.SetDefine(TEXT("GPU_SORT"), 1);
		}
	}
};

/**
 * Draws a splat into each triangle.
 */
class FRenderSplatPS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRenderSplatPS);
	SHADER_USE_PARAMETER_STRUCT(FRenderSplatPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
	SHADER_PARAMETER(FVector4f, global_tint)
	SHADER_PARAMETER(float, exposure)
	RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

class FGSPortalRenderSplatVS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSPortalRenderSplatVS);
	SHADER_USE_PARAMETER_STRUCT(FGSPortalRenderSplatVS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
	SHADER_PARAMETER(FMatrix44f, local_to_view)
	SHADER_PARAMETER(FMatrix44f, local_to_clip)
	SHADER_PARAMETER(FVector2f, render_resolution)
	SHADER_PARAMETER(float, near_clip)
	SHADER_PARAMETER(float, far_clip)
	SHADER_PARAMETER(float, frustum_margin)
	SHADER_PARAMETER(float, max_splat_scale)
	SHADER_PARAMETER(uint32, debug_centers_only)
	SHADER_PARAMETER(float, debug_fixed_splat_radius)
	SHADER_PARAMETER(float, debug_splat_offset_scale)
	SHADER_PARAMETER(uint32, debug_branch_color)
	SHADER_PARAMETER_STRUCT_INCLUDE(FPackedPositionParameters, Positions)
	SHADER_PARAMETER_SRV(Buffer<float4>, transforms)
	SHADER_PARAMETER_SRV(Buffer<float4>, colors)
	SHADER_PARAMETER_SRV(Buffer<uint>, Indices)
	END_SHADER_PARAMETER_STRUCT()
};

class FGSPortalVisibilityStatsCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSPortalVisibilityStatsCS);
	SHADER_USE_PARAMETER_STRUCT(FGSPortalVisibilityStatsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
	SHADER_PARAMETER(FMatrix44f, local_to_view)
	SHADER_PARAMETER(FMatrix44f, local_to_clip)
	SHADER_PARAMETER(float, near_clip)
	SHADER_PARAMETER(float, far_clip)
	SHADER_PARAMETER(float, frustum_margin)
	SHADER_PARAMETER(float, max_splat_scale)
	SHADER_PARAMETER(uint32, num_splats)
	SHADER_PARAMETER_STRUCT_INCLUDE(FPackedPositionParameters, Positions)
	SHADER_PARAMETER_SRV(Buffer<float4>, transforms)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float>, view_z)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, flags)
	END_SHADER_PARAMETER_STRUCT()

public:
	static void ModifyCompilationEnvironment(
		const FGlobalShaderPermutationParameters& Parameters,
		FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(
			TEXT("THREAD_GROUP_SIZE_X"), THREAD_GROUP_SIZE_X);
	}
};

} // namespace PICO::Splat::Shaders
