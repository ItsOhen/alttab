#include "elements.hpp"
class CarouselManager {
public:
  bool active = false;
  struct Monitor {
    std::vector<UP<WindowContainer>> windows;
    size_t activeIndex = 0;
    int offset = 0;
  };
  std::map<int, Monitor> monitors;
  std::chrono::time_point<std::chrono::steady_clock> lastframe = std::chrono::steady_clock::now();
  size_t activeMonitorIndex = 0;

  CarouselManager();

  void toggle();
  void activate();
  void deactivate();

  void next(bool snap = false);
  void prev(bool snap = false);
  void up(bool snap = false);
  void down(bool snap = false);

  void confirm();

  static bool shouldIncludeWindow(PHLWINDOW w);
  bool isElementOnScreen(WindowContainer *target);
  bool windowPicked(Vector2D mousePos);
  void updateSelection(WindowContainer *target);
  void updateSelection(PHLWINDOW target);

  void update();
  void refreshLayout(bool snap = false);
  void rebuildAll();
  void damageMonitors();

  std::vector<Element *> getRenderList();
};

inline UP<CarouselManager> g_pCarouselManager;
