#include "logger.hpp"
#include <any>
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

  auto tex = g_pHyprRenderer->renderText(m_sBuffer, {1.0, 1.0, 1.0, 1.0}, 24);
  if (!tex)
    return;

  Vector2D logicalSize = tex->m_size * monitor->m_scale;
  CBox textBox = {{10, 10}, logicalSize};
  g_pHyprOpenGL->renderRect(textBox, {0, 0, 0, 0.6}, {});
  g_pHyprOpenGL->renderTexture(tex, textBox, {.a = 1.0});

  m_sBuffer.clear();
}
