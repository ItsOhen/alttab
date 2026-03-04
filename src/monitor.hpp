#pragma once
#include "container.hpp"
#include "defines.hpp"

class Monitor {
private:
  struct CardData {
    CBox box;
    float scale = 1.0f;
    float alpha = 1.0f;
    float z = 0.0f;
    bool visible = false;
  };

  struct RenderTask {
    WindowCard *card;
    CardData data;
    float visibility = 0.0f;
    float since = 0.0f;
  };
  std::vector<RenderTask> renderTasks;

public:
  Monitor(PHLMONITOR monitor);
  void createTexture();
  void renderTexture(const CRegion &damage);
  WP<WindowCard> addWindow(PHLWINDOW window);
  size_t removeWindow(PHLWINDOW window);
  void next();
  void prev();
  bool animate(const float delta, const bool active);
  void update(const float delta, const bool active);
  void draw(const CRegion &damage, const float &offset, const bool active);
  CardData getCardBox(int index, const float &offset);
  PHLWINDOW select(int card);

  bool animating = false;
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
