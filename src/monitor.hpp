#pragma once
#include "container.hpp"
#include "defines.hpp"

class Monitor {
private:
  struct CardData {
    CBox box = {0, 0, 0, 0};
    float z = 0.0f;
    float alpha = 0.0f;
    float scale = 1.0f;
  };

public:
  Monitor(PHLMONITOR monitor);
  void createTexture();
  void renderTexture(const CRegion &damage);
  WP<WindowCard> addWindow(PHLWINDOW window);
  size_t removeWindow(PHLWINDOW window);
  void next();
  void prev();
  void update(float delta, const bool active);
  void draw(const CRegion &damage, const float &offset, const bool active);
  CardData getCardBox(int index, const float &offset, const bool active);
  PHLWINDOW select(int card);

  bool animating = false;
  CRegion lastDamage;
  AnimatedValue<float> rotation;
  AnimatedValue<float> zoom;
  AnimatedValue<float> alpha;
  Timestamp lastFrame;
  PHLMONITOR monitor;
  SP<CTexture> texture;
  SP<CTexture> blurred;
  CFramebuffer bgFb, blurFb;
  size_t activeWindow = 0;
  std::vector<UP<WindowCard>> windows;
};
