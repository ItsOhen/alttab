#include "logger.hpp"
#include <any>
#include <src/render/pass/RectPassElement.hpp>
#include <src/render/pass/TexPassElement.hpp>
#define private public
#include <src/render/OpenGL.hpp>
#include <src/render/Renderer.hpp>
#undef private

void DebugText::add(const std::string &text) {
  if (!m_sBuffer.empty())
    m_sBuffer += "\n";
  m_sBuffer += text;
}

void DebugText::draw(PHLMONITOR monitor) {
#ifdef NDEBUG
  return;
#endif

  if (m_sBuffer.empty())
    return;

  auto tex = g_pHyprOpenGL->renderText(m_sBuffer, {1.0, 1.0, 1.0, 1.0}, 24);
  if (!tex)
    return;

  Vector2D logicalSize = tex->m_size * monitor->m_scale;
  auto rect = CRectPassElement::SRectData{
      .box = {{10, 10}, logicalSize},
      .color = {0, 0, 0, 0.6},
      .blur = false};

  auto text = CTexPassElement::SRenderData{
      .tex = tex,
      .box = {{0, 0}, logicalSize},
      .a = 1.0f};

  g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(rect));
  g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(text));

  m_sBuffer.clear();
}
