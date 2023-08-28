#pragma once

#include "include/sl.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11PixelShader;

namespace sl
{

namespace imgui
{
struct ImGUI;
struct Context;
}

namespace chi
{
class ICompute;
}

union MtssFgDrawFlag
{
    uint32_t u32All;

    struct
    {
        uint32_t showPrevDepth        : 1;
        uint32_t showCurrDepth        : 1;
        uint32_t showPrevHudLessColor : 1;
        uint32_t showCurrHudLessColor : 1;
        uint32_t showPrevMotionVector : 1;
        uint32_t showCurrMotionVector : 1;
        uint32_t reserved             : 26;
    };
};

struct MtssFgDebugOverlayInfo
{
    MtssFgDrawFlag flag;
    sl::Resource*  pPrevDepth;
    sl::Resource*  pCurrDepth;
    sl::Resource*  pPrevHudLessColor;
    sl::Resource*  pCurrHudLessColor;
    sl::Resource*  pMotionVectorLv0;

    sl::Resource* pRenderTarget;
};

class ImGuiDebugOverlay
{
public:
    ImGuiDebugOverlay(sl::imgui::ImGUI* pUi, sl::chi::ICompute* pCompute, void* pDevice, sl::RenderAPI renderApi);

    void SetWindow(void* window);
    void Init(uint32_t width, uint32_t height, uint32_t nativeFormat);
    void DeInit();

    void DrawMtssFG(const MtssFgDebugOverlayInfo& info);

    sl::imgui::ImGUI* ImGui() const
    {
        return m_pImGui;
    }

    sl::chi::ICompute* Compute() const
    {
        return m_pCompute;
    }

    ID3D11Device* D3d11Device() const
    {
        return m_pD3d11Device;
    }

    ID3D11DeviceContext* D3d11Context() const
    {
        return m_pD3d11DeviceContext;
    }

    ID3D11PixelShader* D3d11PixelShaderDepth() const
    {
        return m_pD3d11PixelShaderDepth;
    }

    ID3D11PixelShader* D3d11PixelShaderColor() const
    {
        return m_pD3d11PixelShaderColor;
    }

    ID3D11PixelShader* D3d11PixelShaderMevc() const
    {
        return m_pD3d11PixelShaderMevc;
    }

    bool IsD3d11() const
    {
        return m_renderApi == sl::RenderAPI::eD3D11;
    }

    const MtssFgDebugOverlayInfo& MtssFgDebugInfo() const
    {
        return m_mtssFgInfo;
    }

private:
    bool CreateInternalPixelShader();

private:
    sl::imgui::ImGUI*        m_pImGui;
    sl::chi::ICompute*       m_pCompute;
    sl::imgui::Context*      m_pImGuiCtx;
    void*                    m_pDevice;
    sl::RenderAPI            m_renderApi;
    void*                    m_window;

    bool                 m_inited;
    ID3D11Device*        m_pD3d11Device;
    ID3D11DeviceContext* m_pD3d11DeviceContext;
    ID3D11PixelShader*   m_pD3d11PixelShaderDepth;
    ID3D11PixelShader*   m_pD3d11PixelShaderColor;
    ID3D11PixelShader*   m_pD3d11PixelShaderMevc;

    MtssFgDebugOverlayInfo m_mtssFgInfo;
};

} // namespace sl
