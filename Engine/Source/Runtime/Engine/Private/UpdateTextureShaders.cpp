// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.


#include "EnginePrivate.h"
#include "UpdateTextureShaders.h"

IMPLEMENT_SHADER_TYPE(,FUpdateTexture2DSubresouceCS,TEXT("UpdateTextureShaders"),TEXT("UpdateTexture2DSubresourceCS"),SF_Compute);
IMPLEMENT_SHADER_TYPE(,FCopyTexture2DCS,TEXT("UpdateTextureShaders"),TEXT("CopyTexture2DCS"),SF_Compute);
