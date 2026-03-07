#pragma once

#include "animvar.hpp"
#include "defines.hpp"
#include <hyprutils/math/Region.hpp>
#include <hyprutils/math/Vector2D.hpp>
#include <hyprutils/signal/Listener.hpp>
#include <src/config/ConfigDataValues.hpp>
#include <src/helpers/time/Time.hpp>
#include <src/protocols/core/Compositor.hpp>
#define private public
#include <src/render/Framebuffer.hpp>
#undef private
#include <src/render/Texture.hpp>

class WindowCard {
public:
  WindowCard(PHLWINDOW window);
  ~WindowCard();
  void requestFrame(PHLMONITOR monitor);
  bool snapshot(const Vector2D &targetSize);
  void draw(const CBox &box, const float scale, const float alpha);
  void drawTitle(const CBox &box, const float scale, const float alpha);
  void drawBorder(const float alpha);
  void attachListeners(SP<CWLSurfaceResource> surface);
  void setPosition(const CBox &position);
  CBox getPosition() const;

  PHLWINDOW window;
  CFramebuffer fb;
  bool ready = false;
  Timestamp lastCommit, lastSnapshot;
  float z = 0.0f;
  bool isActive = false;
  bool firstSnapshot = true;

private:
  CBox position;
  CBox contentBox;
  CBox titleBox;
  CBox previewBox;
  std::string title;
  SP<CTexture> titleTexture;
  double lastWidth = 0;
  std::vector<CHyprSignalListener> commit;
};
