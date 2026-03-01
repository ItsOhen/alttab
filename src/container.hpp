#pragma once

#include "animvar.hpp"
#include "defines.hpp"
#include <hyprutils/math/Region.hpp>
#include <hyprutils/math/Vector2D.hpp>
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
  void snapshot(const Vector2D &targetSize);
  void draw(const CBox &box, const float scale, const float alpha);
  void drawTitle(const CBox &box, const float scale, const float alpha);
  void drawBorder(const float alpha);

  PHLWINDOW window;
  CFramebuffer fb;
  bool needsRefresh = true;
  bool ready = false;
  Timestamp lastCommit;
  float z = 0.0f;
  bool isActive = false;

private:
  CBox contentBox;
  CBox titleBox;
  CBox previewBox;
  const double borderSize = 4;
  CGradientValueData borderColor;
  GLuint fboID = 0;
  Vector2D lastSize;
  CHyprSignalListener commitListener;
  std::string title;
  SP<CTexture> titleTexture;
  bool initGL();
  double lastWidth = 0;
};
