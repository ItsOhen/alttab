#pragma once
#include "monitor.hpp"
#include <map>
#include <src/SharedDefs.hpp>
#include <src/render/Texture.hpp>
#include <src/render/pass/PassElement.hpp>

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
  void draw(MONITORID monid, const CRegion &damage);

  bool active = false;

private:
  void onConfigReload();
  void onWindowCreated(PHLWINDOW window);
  void onWindowDestroyed(PHLWINDOW window);
  void onRender(eRenderStage stage);
  void onFocusChange(PHLMONITOR monitor);

#ifdef HYPRLAND_NEW_EVENTS
  struct {
    CHyprSignalListener config;
    CHyprSignalListener windowCreated;
    CHyprSignalListener windowDestroyed;
    CHyprSignalListener render;
    CHyprSignalListener focusChange;
    CHyprSignalListener monitorAdded;
    CHyprSignalListener monitorRemoved;
  } listeners;
#else
  struct {
    SP<HOOK_CALLBACK_FN> config;
    SP<HOOK_CALLBACK_FN> windowCreated;
    SP<HOOK_CALLBACK_FN> windowDestroyed;
    SP<HOOK_CALLBACK_FN> render;
    SP<HOOK_CALLBACK_FN> focusChange;
    SP<HOOK_CALLBACK_FN> monitorAdded;
    SP<HOOK_CALLBACK_FN> monitorRemoved;
  } listeners;
#endif

  MONITORID activeMonitor = MONITOR_INVALID;
  Timestamp lastFrame;
  std::map<MONITORID, UP<Monitor>> monitors;
  AnimatedValue<float> monitorOffset;
  Timestamp lastUpdate;
};

inline UP<Manager> manager;

class RenderPass : public IPassElement {
public:
  virtual void draw(const CRegion &damage);
  virtual bool needsLiveBlur() { return BLURBG && !POWERSAVE; }
  virtual bool needsPrecomputeBlur() { return false; }
  virtual const char *passName() { return "TabCarouselPassElement"; }
};
