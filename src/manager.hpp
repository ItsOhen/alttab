#pragma once
#include "container.hpp"
#include "defines.hpp"
#include <map>
#include <src/SharedDefs.hpp>
#include <src/render/Texture.hpp>
#include <src/render/pass/PassElement.hpp>

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
  void update(float delta);
  void draw(const CRegion &damage);
  CardData getCardBox(int index);
  void select(int card);

  bool animating = false;
  AnimatedValue<float> rotation;
  Timestamp lastFrame;
  PHLMONITOR monitor;
  SP<CTexture> texture;
  SP<CTexture> blurred;
  CFramebuffer bgFb, blurFb;
  size_t activeWindow = 0;
  std::vector<UP<WindowCard>> windows;
};

class Manager {
public:
  Manager();
  void activate();
  void deactivate();
  void toggle();
  void confirm();
  void update(float delta);
  void up();
  void down();
  void next();
  void prev();
  void rebuild();
  void draw(const CRegion &damage);

  bool active = false;

private:
  void onConfigReload();
  void onWindowCreated(PHLWINDOW window);
  void onWindowDestroyed(PHLWINDOW window);
  void onRender(eRenderStage stage);

  struct {
    CHyprSignalListener config;
    CHyprSignalListener windowCreated;
    CHyprSignalListener windowDestroyed;
    CHyprSignalListener render;
  } listeners;

  MONITORID activeMonitor = MONITOR_INVALID;
  Timestamp lastFrame;
  std::map<MONITORID, UP<Monitor>> monitors;
};

inline UP<Manager> manager;

class RenderPass : public IPassElement {
public:
  virtual void draw(const CRegion &damage);
  virtual bool needsLiveBlur() { return false; }
  virtual bool needsPrecomputeBlur() { return false; }
  virtual const char *passName() { return "TabCarouselPassElement"; }
};
