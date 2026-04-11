#pragma once

#include "defines.hpp"
#include <src/render/Texture.hpp>
#include <src/helpers/Color.hpp>
#include <string>

namespace alttab {

/// Render text to a CTexture using Cairo/Pango.
/// Returns a shared pointer to the texture, or nullptr on failure.
/// @param text     The text string to render.
/// @param color    The text color.
/// @param fontSize The font size in pixels (unscaled).
/// @param scale    The monitor scale factor.
SP<CTexture> renderTextToTexture(const std::string& text, const CHyprColor& color, int fontSize, float scale = 1.0f);

} // namespace alttab
