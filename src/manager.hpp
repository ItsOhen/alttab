#pragma once
#include "monitor.hpp"
#include "styles.hpp"
#include <map>
#include <src/SharedDefs.hpp>
#include <src/helpers/time/Timer.hpp>
#include <src/managers/eventLoop/EventLoopTimer.hpp>
#include <src/render/Texture.hpp>
#include <src/render/pass/PassElement.hpp>

class Manager {
public:
  Manager();
  void activate();
  void init();
  void deactivate();
  void toggle();
  void confirm();
  void move(Direction dir);
  void update(float delta);
  void rebuild();
  void draw(MONITORID monid, const CRegion &damage);
  void damageMonitors();
  bool isActive() const;

protected:
  bool active = false;
  MONITORID activeMonitor = MONITOR_INVALID;

private:
  void onConfigReload();
  void onWindowCreated(PHLWINDOW window);
  void onWindowDestroyed(PHLWINDOW window);
  void onRender(eRenderStage stage);
  void onFocusChange(PHLMONITOR monitor);
  void onMouseClick(const IPointer::SButtonEvent button);

  struct MonitorElement {
    Monitor *monitor;
    float offset;
    float z;
  };
  void renderBackground(MONITORID monid, const CRegion &damage);
  void renderMonitors(const CRegion &damage);

  bool setLayout();

#ifdef HYPRLAND_LEGACY
  struct {
    SP<HOOK_CALLBACK_FN> config;
    SP<HOOK_CALLBACK_FN> windowCreated;
    SP<HOOK_CALLBACK_FN> windowDestroyed;
    SP<HOOK_CALLBACK_FN> render;
    SP<HOOK_CALLBACK_FN> focusChange;
    SP<HOOK_CALLBACK_FN> monitorAdded;
    SP<HOOK_CALLBACK_FN> monitorRemoved;
  } listeners;
#else
  struct {
    CHyprSignalListener config;
    CHyprSignalListener windowCreated;
    CHyprSignalListener windowDestroyed;
    CHyprSignalListener render;
    CHyprSignalListener focusChange;
    CHyprSignalListener monitorAdded;
    CHyprSignalListener monitorRemoved;
    CHyprSignalListener mouseClick;
    CHyprSignalListener mouseMove;
  } listeners;
#endif

  SP<CEventLoopTimer> loopTimer;
  SP<CEventLoopTimer> graceTimer;

  Timestamp lastFrame;
  std::map<MONITORID, UP<Monitor>> monitors;
  AnimatedValue<float> monitorOffset;
  AnimatedValue<float> monitorFade;
  Timestamp lastUpdate;
  SP<IStyle> layoutStyle;
  bool graceExpired = false;
  std::vector<MonitorElement> stack;

  friend class Monitor;
};

inline UP<Manager> manager;

class RenderPass : public IPassElement {
public:
  virtual void draw(const CRegion &damage);
  virtual bool needsLiveBlur() { return false; }
  virtual bool needsPrecomputeBlur() { return false; }
  virtual const char *passName() { return "TabCarouselPassElement"; }
};
