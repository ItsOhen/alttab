#include "text.hpp"

#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <src/render/OpenGL.hpp>

namespace alttab {

SP<CTexture> renderTextToTexture(const std::string& text, const CHyprColor& color, int fontSize, float scale) {
  if (text.empty())
    return nullptr;

  // Measure text size first with a scratch surface
  const int scaledSize = static_cast<int>(fontSize * scale);

  auto* measureSurface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  auto* measureCr = cairo_create(measureSurface);

  PangoLayout* measureLayout = pango_cairo_create_layout(measureCr);
  pango_layout_set_text(measureLayout, text.c_str(), -1);

  PangoFontDescription* fontDesc = pango_font_description_from_string("sans");
  pango_font_description_set_size(fontDesc, scaledSize * PANGO_SCALE);
  pango_layout_set_font_description(measureLayout, fontDesc);
  pango_font_description_free(fontDesc);

  PangoRectangle ink, logical;
  pango_layout_get_pixel_extents(measureLayout, &ink, &logical);

  g_object_unref(measureLayout);
  cairo_destroy(measureCr);
  cairo_surface_destroy(measureSurface);

  const int texW = logical.width + 2;  // small padding
  const int texH = logical.height + 2;

  if (texW <= 0 || texH <= 0)
    return nullptr;

  // Render the text to a properly-sized surface
  auto* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, texW, texH);
  auto* cr = cairo_create(surface);

  // Clear
  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_restore(cr);

  // Draw text
  PangoLayout* layout = pango_cairo_create_layout(cr);
  pango_layout_set_text(layout, text.c_str(), -1);

  PangoFontDescription* fd = pango_font_description_from_string("sans");
  pango_font_description_set_size(fd, scaledSize * PANGO_SCALE);
  pango_layout_set_font_description(layout, fd);
  pango_font_description_free(fd);

  cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
  cairo_move_to(cr, 1, 1); // offset by padding
  pango_cairo_show_layout(cr, layout);

  g_object_unref(layout);
  cairo_surface_flush(surface);

  // Upload to GL texture
  const auto data = cairo_image_surface_get_data(surface);
  auto tex = makeShared<CTexture>();
  tex->allocate();

  glBindTexture(GL_TEXTURE_2D, tex->m_texID);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

#ifndef GLES2
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
#endif

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texW, texH, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, data);

  tex->m_size = {texW, texH};

  cairo_destroy(cr);
  cairo_surface_destroy(surface);

  return tex;
}

} // namespace alttab
