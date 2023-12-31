#include "debugoverlay.h"

#include <assert.h>
#include "source/core/sl.api/internal.h"
#include "source/platforms/sl.chi/compute.h"
#include "source/plugins/sl.imgui/imgui.h"
#include "external/imgui/imgui.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#ifdef _MSC_VER
#pragma comment(lib, "d3dcompiler")
#endif

namespace sl
{

namespace
{

void DepthDrawCallBack(const sl::imgui::DrawData* drawData, const sl::imgui::DrawCommand* cmd)
{
    const ImDrawList*  pDrawList   = reinterpret_cast<const ImDrawList*>(drawData);
    const ImDrawCmd*   pDrawCmd    = reinterpret_cast<const ImDrawCmd*>(cmd);
    ImGuiDebugOverlay* pThis       = static_cast<ImGuiDebugOverlay*>(pDrawCmd->UserCallbackData);

    if (pThis->IsD3d11())
    {
        pThis->D3d11Context()->PSSetShader(pThis->D3d11PixelShaderDepth(), nullptr, 0);
        const float blend_factor[4] = {0.f, 0.f, 0.f, 0.f};
        // Use default blend state, it means disable blend.
        pThis->D3d11Context()->OMSetBlendState(nullptr, blend_factor, 0xffffffff);
    }
}

void ColorDrawCallBack(const sl::imgui::DrawData* drawData, const sl::imgui::DrawCommand* cmd)
{
    const ImDrawList*  pDrawList   = reinterpret_cast<const ImDrawList*>(drawData);
    const ImDrawCmd*   pDrawCmd    = reinterpret_cast<const ImDrawCmd*>(cmd);
    ImGuiDebugOverlay* pThis       = static_cast<ImGuiDebugOverlay*>(pDrawCmd->UserCallbackData);

    if (pThis->IsD3d11())
    {
        pThis->D3d11Context()->PSSetShader(pThis->D3d11PixelShaderColor(), nullptr, 0);
        const float blend_factor[4] = {0.f, 0.f, 0.f, 0.f};
        pThis->D3d11Context()->OMSetBlendState(nullptr, blend_factor, 0xffffffff);
    }
}

void MevcDrawCallBack(const sl::imgui::DrawData* drawData, const sl::imgui::DrawCommand* cmd)
{
    const ImDrawList*  pDrawList   = reinterpret_cast<const ImDrawList*>(drawData);
    const ImDrawCmd*   pDrawCmd    = reinterpret_cast<const ImDrawCmd*>(cmd);
    ImGuiDebugOverlay* pThis       = static_cast<ImGuiDebugOverlay*>(pDrawCmd->UserCallbackData);

    if (pThis->IsD3d11())
    {
        pThis->D3d11Context()->PSSetShader(pThis->D3d11PixelShaderMevc(), nullptr, 0);
        const float blend_factor[4] = {0.f, 0.f, 0.f, 0.f};
        pThis->D3d11Context()->OMSetBlendState(nullptr, blend_factor, 0xffffffff);
    }
}

}
ImGuiDebugOverlay::ImGuiDebugOverlay(
    sl::imgui::ImGUI* pUi,
    sl::chi::ICompute* pCompute,
    void*              pDevice,
    sl::RenderAPI     renderApi)
    : 
    m_pImGui(pUi),
    m_pCompute(pCompute),
    m_pImGuiCtx(nullptr),
    m_pDevice(pDevice),
    m_renderApi(renderApi),
    m_window(NULL),
    m_inited(false),
    m_pD3d11Device(nullptr),
    m_pD3d11DeviceContext(nullptr),
    m_pD3d11PixelShaderDepth(nullptr),
    m_pD3d11PixelShaderColor(nullptr),
    m_pD3d11PixelShaderMevc(nullptr),
    m_mtssFgInfo{}
{
    if (IsD3d11())
    {
        m_pD3d11Device = static_cast<ID3D11Device*>(m_pDevice);
        m_pD3d11Device->GetImmediateContext(&m_pD3d11DeviceContext);
    }
}

void ImGuiDebugOverlay::SetWindow(
    void* window)
{
    m_window = window;
}

void ImGuiDebugOverlay::Init(
    uint32_t width,
    uint32_t height,
    uint32_t nativeFormat)
{
    if (m_pImGui)
    {
        if (m_inited)
        {
            DeInit();
        }

        sl::imgui::ContextDesc desc;
        desc.width            = width;
        desc.height           = height;
        desc.hWnd             = (HWND)m_window;
        desc.backBufferFormat = nativeFormat;

        m_pImGuiCtx = m_pImGui->createContext(desc);
        m_pImGui->setCurrentContext(m_pImGuiCtx);
        m_pImGui->setDisplaySize(sl::type::Float2(static_cast<float>(width), static_cast<float>(height)));

        bool success = CreateInternalPixelShader();
        assert(success == true);

        m_inited = true;
    }
}

void ImGuiDebugOverlay::DeInit()
{
    if (m_pImGuiCtx != nullptr)
    {
        m_pImGui->destroyContext(m_pImGuiCtx);
    }

    m_inited = false;
}

void ImGuiDebugOverlay::DrawMtssFG(const MtssFgDebugOverlayInfo& info)
{
    if (m_pImGui)
    {
        bool open = true;
        m_pImGui->newFrame(0.f);
        m_pImGui->begin("MTSS-FG Debug Overlay", &open, sl::imgui::kWindowFlagNone);

        if (IsD3d11())
        {
            ID3D11Resource*         pRt  = static_cast<ID3D11Resource*>(info.pRenderTarget->native);
            ID3D11RenderTargetView* pRtv = nullptr;
            m_pD3d11Device->CreateRenderTargetView(pRt, nullptr, &pRtv);
            m_pD3d11DeviceContext->OMSetRenderTargets(1, &pRtv, nullptr);
        }

        uint32_t id = m_pImGui->getIdString("MtssFgDockSpace");
        m_pImGui->dockSpace(id, sl::type::Float2(0.0f, 0.0f), 0, nullptr);

        {
            m_pImGui->setNextWindowDockId(id, sl::imgui::Condition::eFirstUseEver);

            const char* pText[2] = {"Prev HudLess Color", "Curr HudLess Color"};
            sl::Resource* pResources[2] = {info.pPrevHudLessColor, info.pCurrHudLessColor};

            DrawTextureWithNewFrame("HudLess Color",
                                    pText,
                                    pResources,
                                    2,
                                    info.pPrevHudLessColor->width,
                                    info.pPrevHudLessColor->height,
                                    ColorDrawCallBack);
        }

        {
            m_pImGui->setNextWindowDockId(id, sl::imgui::Condition::eFirstUseEver);

            const char*   pText[2]      = {"Prev Depth", "Curr Depth"};
            sl::Resource* pResources[2] = {info.pPrevDepth, info.pCurrDepth};

            DrawTextureWithNewFrame("Depth",
                                    pText,
                                    pResources,
                                    2,
                                    info.pPrevDepth->width,
                                    info.pPrevDepth->height,
                                    DepthDrawCallBack);
        }

        {
            m_pImGui->setNextWindowDockId(id, sl::imgui::Condition::eFirstUseEver);

            const char*   pText[2]      = {"Prev Motion Vector", "Curr Motion Vector"};
            sl::Resource* pResources[2] = {info.pPrevMotionVector, info.pCurrMotionVector};

            DrawTextureWithNewFrame("Motion Vector",
                                    pText,
                                    pResources,
                                    2,
                                    info.pPrevMotionVector->width,
                                    info.pPrevMotionVector->height,
                                    MevcDrawCallBack);
        }

        {
            m_pImGui->setNextWindowDockId(id, sl::imgui::Condition::eFirstUseEver);

            const char*   pText[1]      = {"UI Color"};
            sl::Resource* pResources[1] = {info.pUiColor};

            DrawTextureWithNewFrame("UI Color",
                                    pText,
                                    pResources,
                                    1,
                                    info.pUiColor->width,
                                    info.pUiColor->height,
                                    ColorDrawCallBack);
        }

        m_pImGui->end();

        if (IsD3d11())
        {
            m_pImGui->render(nullptr, nullptr, 0);
        }
    }
}

bool ImGuiDebugOverlay::CreateInternalPixelShader()
{
    if (IsD3d11())
    {
        {
            static const char* pixelShader = "struct PS_INPUT\
            {\
            float4 pos : SV_POSITION;\
            float4 col : COLOR0;\
            float2 uv  : TEXCOORD0;\
            };\
            sampler sampler0;\
            Texture2D texture0;\
            \
            float4 main(PS_INPUT input) : SV_Target\
            {\
            float4 out_col = input.col * (texture0.Sample(sampler0, input.uv) * 100.0f); \
            out_col.w = 0.0f; \
            return out_col; \
            }";

            ID3DBlob* pixelShaderBlob;
            if (FAILED(D3DCompile(pixelShader,
                                  strlen(pixelShader),
                                  NULL,
                                  NULL,
                                  NULL,
                                  "main",
                                  "ps_4_0",
                                  0,
                                  0,
                                  &pixelShaderBlob,
                                  NULL)))
                return false;

            if (m_pD3d11Device->CreatePixelShader(pixelShaderBlob->GetBufferPointer(),
                                                  pixelShaderBlob->GetBufferSize(),
                                                  NULL,
                                                  &m_pD3d11PixelShaderDepth) != S_OK)
            {
                pixelShaderBlob->Release();
                return false;
            }
            pixelShaderBlob->Release();
        }

        {
            static const char* pixelShader = "struct PS_INPUT\
            {\
            float4 pos : SV_POSITION;\
            float4 col : COLOR0;\
            float2 uv  : TEXCOORD0;\
            };\
            sampler sampler0;\
            Texture2D texture0;\
            \
            float4 main(PS_INPUT input) : SV_Target\
            {\
            float4 out_col = input.col * (texture0.Sample(sampler0, input.uv)); \
            out_col.w = 0.0f; \
            return out_col; \
            }";

            ID3DBlob* pixelShaderBlob;
            if (FAILED(D3DCompile(pixelShader,
                                  strlen(pixelShader),
                                  NULL,
                                  NULL,
                                  NULL,
                                  "main",
                                  "ps_4_0",
                                  0,
                                  0,
                                  &pixelShaderBlob,
                                  NULL)))
                return false;

            if (m_pD3d11Device->CreatePixelShader(pixelShaderBlob->GetBufferPointer(),
                                                  pixelShaderBlob->GetBufferSize(),
                                                  NULL,
                                                  &m_pD3d11PixelShaderColor) != S_OK)
            {
                pixelShaderBlob->Release();
                return false;
            }
            pixelShaderBlob->Release();
        }

        {
            static const char* pixelShader = "struct PS_INPUT\
            {\
            float4 pos : SV_POSITION;\
            float4 col : COLOR0;\
            float2 uv  : TEXCOORD0;\
            };\
            sampler sampler0;\
            Texture2D texture0;\
            \
            float4 main(PS_INPUT input) : SV_Target\
            {\
            float4 out_col = input.col * (abs((texture0.Sample(sampler0, input.uv)))); \
            out_col.w = 0.0f; \
            return out_col; \
            }";

            ID3DBlob* pixelShaderBlob;
            if (FAILED(D3DCompile(pixelShader,
                                  strlen(pixelShader),
                                  NULL,
                                  NULL,
                                  NULL,
                                  "main",
                                  "ps_4_0",
                                  0,
                                  0,
                                  &pixelShaderBlob,
                                  NULL)))
                return false;

            if (m_pD3d11Device->CreatePixelShader(pixelShaderBlob->GetBufferPointer(),
                                                  pixelShaderBlob->GetBufferSize(),
                                                  NULL,
                                                  &m_pD3d11PixelShaderMevc) != S_OK)
            {
                pixelShaderBlob->Release();
                return false;
            }
            pixelShaderBlob->Release();
        }
    }

    return true;
}

void ImGuiDebugOverlay::DrawTextureWithNewFrame(const char*             pFrameName,
                                                const char**            ppText,
                                                sl::Resource**          ppResources,
                                                uint8_t                 resourceCount,
                                                uint32_t                resourceWidth,
                                                uint32_t                resourceHeight,
                                                sl::imgui::DrawCallback callback)
{
    bool open = true;
    m_pImGui->begin(pFrameName, &open, sl::imgui::kWindowFlagNone);

    for (uint8_t i = 0; i < resourceCount; i++)
    {
        if (ppResources[i])
        {
            m_pImGui->beginGroup();
            void* pSrv = nullptr;
            m_pCompute->getTextureSrv(ppResources[i], &pSrv);
            m_pImGui->textUnformatted(ppText[i]);
            m_pImGui->addWindowDrawCallback(callback, this);
            m_pImGui->image(static_cast<sl::imgui::TextureId>(pSrv),
                            {float(resourceWidth / 2.0), float(resourceHeight / 2.0)},
                            {0.0f, 0.0f},
                            {1.0f, 1.0f},
                            {1.0f, 1.0f, 1.0f, 1.0f},
                            {0.0f, 0.0f, 0.0f, 0.0f});
            m_pImGui->addWindowDrawCallback(sl::imgui::DrawCallback(-1), this);
            m_pImGui->endGroup();

            m_pImGui->sameLine();
        }
    }

    m_pImGui->end();
}

}
