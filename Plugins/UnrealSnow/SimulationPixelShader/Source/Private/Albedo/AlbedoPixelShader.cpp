#include "AlbedoPixelShader.h"
#include "../PixelShaderPrivatePCH.h"
#include "AlbedoPixelShaderDeclaration.h"
#include "RHIStaticStates.h"
#include "PipelineStateCache.h"
#include "SceneUtils.h"
#include "RHIResourceUtils.h"
#include "RenderResource.h"
#include "VertexFactory.h"
#include "StaticMeshVertexData.h"
#include "../VertexShader.h"


#define WRITE_SNOW_MAP false

//It seems to be the convention to expose all vertex declarations as globals, and then reference them as externs in the headers where they are needed.
//It kind of makes sense since they do not contain any parameters that change and are purely used as their names suggest, as declarations :)
TGlobalResource<FTextureVertexDeclaration> GAlbedoTextureVertexDeclaration;

FAlbedoPixelShaderManager::FAlbedoPixelShaderManager(ERHIFeatureLevel::Type ShaderFeatureLevel)
{
	FeatureLevel = ShaderFeatureLevel;

	// Initialize RDG parameters with default values
	ShaderParameters = FAlbedoPixelShader::FParameters();
	
	bMustRegenerateSRV = false;
	bIsPixelShaderExecuting = false;
	bIsUnloading = false;
	bSave = false;

	CurrentTexture = NULL;
	CurrentRenderTarget = NULL;
}

void FAlbedoPixelShaderManager::Initialize(FRWStructuredBuffer* AlbedoBuffer, int32 CellsDimensionX, int32 CellsDimensionY)
{
	this->AlbedoInputBuffer = AlbedoBuffer;

	ShaderParameters.CellsDimensionX = CellsDimensionX;
	ShaderParameters.CellsDimensionY = CellsDimensionY;
}

FAlbedoPixelShaderManager::~FAlbedoPixelShaderManager()
{
	bIsUnloading = true;
}

void FAlbedoPixelShaderManager::ExecutePixelShader(UTextureRenderTarget2D* RenderTarget, bool SaveAlbedo)
{
	if (bIsUnloading || bIsPixelShaderExecuting) //Skip this execution round if we are already executing
	{
		return;
	}

	bIsPixelShaderExecuting = true;

	CurrentRenderTarget = RenderTarget;

	ENQUEUE_RENDER_COMMAND(ExecuteAlbedoPixelShader)(
		[this, SaveAlbedo](FRHICommandListImmediate& RHICmdList)
		{
			this->ExecutePixelShaderInternal(SaveAlbedo);
		}
	);
}

void FAlbedoPixelShaderManager::ExecutePixelShaderInternal(bool SaveAlbedo)
{
	check(IsInRenderingThread());

	// Only cleanup
	if (bIsUnloading) 
	{
		return;
	}

	FRHICommandListImmediate& RHICmdList = GRHICommandList.GetImmediateCommandList();

	CurrentTexture = CurrentRenderTarget->GetRenderTargetResource()->GetRenderTargetTexture();

	// Set up render targets
	FRHIRenderPassInfo RPInfo(CurrentTexture, ERenderTargetActions::Load_Store);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("AlbedoPixelShader"));

	// Set graphics pipeline state
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;

	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);
	
	TShaderMapRef<FAlbedoVertexShader> VertexShader(GetGlobalShaderMap(FeatureLevel));
	// Note: In UE 5.6, custom pixel shaders need to be handled differently
	// For now, we'll use a simplified approach
	auto PixelShader = GetGlobalShaderMap(FeatureLevel)->GetShader<FAlbedoPixelShader>();

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GAlbedoTextureVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

	// Set up RDG parameters
	ShaderParameters.AlbedoInputBuffer = AlbedoInputBuffer->SRV;
	ShaderParameters.MaxSnowInputBuffer = nullptr; // Set this if you have a max snow buffer
	
	// Bind parameters to the shader
	SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), ShaderParameters);

	// Draw a fullscreen quad that we can run our pixel shader on
	TArray<FTextureVertex> Vertices;
	Vertices.AddUninitialized(4);
	Vertices[0].Position = FVector4(-1.0f, 1.0f, 0, 1.0f);
	Vertices[1].Position = FVector4(1.0f, 1.0f, 0, 1.0f);
	Vertices[2].Position = FVector4(-1.0f, -1.0f, 0, 1.0f);
	Vertices[3].Position = FVector4(1.0f, -1.0f, 0, 1.0f);
	Vertices[0].UV = FVector2D(0, 0);
	Vertices[1].UV = FVector2D(ShaderParameters.CellsDimensionX, 0);
	Vertices[2].UV = FVector2D(0, ShaderParameters.CellsDimensionY);
	Vertices[3].UV = FVector2D(ShaderParameters.CellsDimensionX, ShaderParameters.CellsDimensionY);

	// Draw the fullscreen quad using immediate mode
	FRHIBufferCreateDesc CreateDesc;
	CreateDesc.DebugName = TEXT("AlbedoQuadVB");
	CreateDesc.Size = Vertices.Num() * sizeof(FTextureVertex);
	CreateDesc.Usage = BUF_Volatile;
	CreateDesc.Stride = sizeof(FTextureVertex);
	FBufferRHIRef VertexBufferRHI = RHICmdList.CreateBuffer(CreateDesc);

	// Upload vertex data
	void* VertexBufferData = RHICmdList.LockBuffer(VertexBufferRHI, 0, CreateDesc.Size, RLM_WriteOnly);
	FMemory::Memcpy(VertexBufferData, Vertices.GetData(), CreateDesc.Size);
	RHICmdList.UnlockBuffer(VertexBufferRHI);

	RHICmdList.SetStreamSource(0, VertexBufferRHI, 0);
	RHICmdList.DrawPrimitive(0, 2, 1);

	// End render pass
	RHICmdList.EndRenderPass();
	 
	bIsPixelShaderExecuting = false;
	
	if (SaveAlbedo)
	{
		TArray<FColor> Bitmap;

		FReadSurfaceDataFlags ReadDataFlags;
		ReadDataFlags.SetLinearToGamma(false);
		ReadDataFlags.SetOutputStencil(false);
		ReadDataFlags.SetMip(0);

		//This is pretty straight forward. Since we are using a standard format, we can use this convenience function instead of having to lock rect.
		RHICmdList.ReadSurfaceData(CurrentTexture, FIntRect(0, 0, CurrentTexture->GetSizeX(), CurrentTexture->GetSizeY()), Bitmap, ReadDataFlags);

		// if the format and texture type is supported
		if (Bitmap.Num())
		{
			// Create screenshot folder if not already present.
			IFileManager::Get().MakeDirectory(*FPaths::ScreenShotDir(), true);

			const FString ScreenFileName(FPaths::ScreenShotDir() / TEXT("SnowMap"));

			uint32 ExtendXWithMSAA = Bitmap.Num() / CurrentTexture->GetSizeY();

			// Save the contents of the array to a bitmap file. (24bit only so alpha channel is dropped)
			FFileHelper::CreateBitmap(*ScreenFileName, ExtendXWithMSAA, CurrentTexture->GetSizeY(), Bitmap.GetData());

		}
		else
		{
			UE_LOG(LogConsoleResponse, Error, TEXT("Failed to save BMP, format or texture type is not supported"));
		}
	}
}



