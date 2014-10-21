// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ISlate3DRenderer.h"


class FSlate3DRenderer : public ISlate3DRenderer
{
public:
	FSlate3DRenderer( TSharedPtr<FSlateRHIResourceManager> InResourceManager, TSharedPtr<FSlateFontCache> InFontCache );
	~FSlate3DRenderer();

	virtual FSlateDrawBuffer& GetDrawBuffer() override;
	virtual void DrawWindow_GameThread(FSlateDrawBuffer& DrawBuffer) override;
	virtual void DrawWindowToTarget_RenderThread( FRHICommandListImmediate& RHICmdList, UTextureRenderTarget2D* RenderTarget, FSlateDrawBuffer& InDrawBuffer ) override;
private:

	/** Double buffered draw buffers so that the rendering thread can be rendering windows while the game thread is setting up for next frame */
	FSlateDrawBuffer DrawBuffers[2];

	/** Texture manager for accessing textures on the game thread */
	TSharedRef<FSlateRHIResourceManager> ResourceManager;

	TSharedRef<FSlateFontCache> FontCache;

	TSharedPtr<class FSlateRHIRenderingPolicy> RenderTargetPolicy;

	/** Element batcher which renders draw elements */
	TSharedPtr<FSlateElementBatcher> ElementBatcher;

	/** The draw buffer which is currently free for use by the game thread */
	uint8 FreeBufferIndex;

};