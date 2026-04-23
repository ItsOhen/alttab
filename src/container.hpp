#pragma once

#include "defines.hpp"
#include <src/protocols/core/Compositor.hpp>
#include <src/render/Framebuffer.hpp>

struct CardData {
  CBox position;
  float z;
  float scale;
  float alpha;
};

struct CardLayout {
  CBox outer;
  CBox content;
  CBox title;
  CBox preview;
};

class WindowCard {
public:
  WindowCard(PHLWINDOW window);
  void draw(const CRegion& damage);
  void present();
  void setPosition(const CBox &position);
  CBox getPosition() const;
  CardLayout buildLayout(float scale);

  PHLWINDOW window;
  Timestamp lastSnapshot;
  bool ready = false;
  float z = 0.0f;
  bool isActive = false;
  bool firstSnapshot = true;
  CBox lastBox;
  float lastBaseWidth = -1.f;

private:
  void updateTitleTexture(float scale);
  CBox position;
  std::string title;
  SP<CTexture> titleTexture;
};
