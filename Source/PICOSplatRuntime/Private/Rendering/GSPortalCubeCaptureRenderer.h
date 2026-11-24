/*
  Copyright (c) 2025 PICO Technology Co., Ltd. See LICENSE.md.
*/

#pragma once

#include "Rendering/GSPortalCubeCaptureTypes.h"

namespace PICO::Splat
{

FString GetGSPortalCubeCaptureDebugState_GameThread();
void SubmitGSPortalCubeCapture_RenderThread(const FGSPortalCubeCaptureRequest& Request);

} // namespace PICO::Splat
