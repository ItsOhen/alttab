#pragma once

#include "animvar.hpp"
#include "defines.hpp"
#include <hyprutils/math/Region.hpp>
#include <hyprutils/math/Vector2D.hpp>
#include <hyprutils/signal/Listener.hpp>
#include <src/config/ConfigDataValues.hpp>
#include <src/helpers/time/Time.hpp>
#define private public
#include <src/render/Framebuffer.hpp>
#undef private
#include <src/render/Texture.hpp>

class WindowCard {
public:
  WindowCard(PHLWINDOW window);
  ~WindowCard();
  bool snapshot(const Vector2D &targetSize);
  void draw(const CBox &box, const float scale, const float alpha);
  void drawTitle(const CBox &box, const float scale, const float alpha);
  void drawBorder(const float alpha);

  PHLWINDOW window;
  CFramebuffer fb;
  bool ready = false;
  Timestamp lastCommit;
  float z = 0.0f;
  bool isActive = false;
  bool needsSnapshot = true;

private:
  CBox contentBox;
  CBox titleBox;
  CBox previewBox;
  const double borderSize = 4;
  CGradientValueData borderColor;
  Vector2D lastSize;
  std::string title;
  SP<CTexture> titleTexture;
  double lastWidth = 0;
#ifdef HYPRLAND_NEW_EVENTS
  CHyprSignalListener commit;
#else
  SP<HOOK_CALLBACK_FN> commit;
#endif
};
