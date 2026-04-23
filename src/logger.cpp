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

  auto tex = g_pHyprOpenGL->renderText(m_sBuffer, CHyprColor(1.0, 1.0, 1.0, 1.0), 14);
  if (!tex) {
    m_sBuffer.clear();
    return;
  }

  Vector2D logicalSize = Vector2D(tex->m_size.x, tex->m_size.y);

  {
    CRectPassElement::SRectData rect;
    rect.box = CBox{{10, 10}, logicalSize};
    rect.color = {0, 0, 0, 0.6};
    g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(rect));
  }

  CTexPassElement::SRenderData text;
  text.tex = tex;
  text.box = {{10, 10}, logicalSize};
  text.a = 1.0f;
  g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(text));

  m_sBuffer.clear();
}
