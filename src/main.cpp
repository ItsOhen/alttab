#include <algorithm>
#include <cstdint>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/managers/HookSystemManager.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprlang.hpp>
#include <iostream>
#include <src/SharedDefs.hpp>
#include <src/desktop/state/FocusState.hpp>
#include <src/devices/IKeyboard.hpp>
#include <src/managers/input/InputManager.hpp>
#include <src/plugins/HookSystem.hpp>

#define private public
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>
#undef private

APICALL EXPORT std::string PLUGIN_API_VERSION() {
  return HYPRLAND_API_VERSION;
}

inline HANDLE PHANDLE = nullptr;
static PHLMONITOR mon = nullptr;

static auto TEXTSIZE = 24;

std::string middleTruncate(std::string str, size_t maxLen = 40) {
  if (str.length() <= maxLen)
    return str;

  size_t sideLen = (maxLen - 3) / 2;
  return str.substr(0, sideLen) + "..." + str.substr(str.length() - sideLen);
}

struct TextureElement {
  Vector2D pos, targetPos;
  Vector2D size, targetSize;
  CFramebuffer fb;
  PHLWINDOW w;
  Vector2D lastSize;

  TextureElement(PHLWINDOW win) : w(win) {}

  void tick(float lerpFactor = 0.1f) {
    pos = pos + (targetPos - pos) * lerpFactor;
    size = size + (targetSize - size) * lerpFactor;
  }

  void update(int borderSize, CHyprColor borderColor) {
    if (!mon || !w || !w->wlSurface())
      return;

    Vector2D surfSize = w->wlSurface()->getViewporterCorrectedSize();

    if (surfSize.x <= 1.0 || surfSize.y <= 1.0)
      return;

    Vector2D snappedTarget = {std::round(targetSize.x), std::round(targetSize.y)};

    if (lastSize.distanceSq(snappedTarget) > 0.1) {
      fb.alloc(snappedTarget.x, snappedTarget.y, mon->m_output->state->state().drmFormat);
      lastSize = snappedTarget;
    }

    double fw = fb.m_size.x;
    double fh = fb.m_size.y;

    CRegion fbRegion = {0.0, 0.0, fw, fh};

    g_pHyprRenderer->makeEGLCurrent();
    if (!g_pHyprRenderer->beginRender(mon, fbRegion, RENDER_MODE_FULL_FAKE, nullptr, &fb))
      return;

    g_pHyprOpenGL->clear(CHyprColor{0, 0, 0, 1.0});

    float scale = std::min((float)(fw - borderSize * 2.0) / (float)surfSize.x, (float)(fh - borderSize * 2.0) / (float)surfSize.y);

    g_pHyprOpenGL->renderRect(CBox{0, 0, fw, fh}, CHyprColor{0, 0, 0, 1.0f}, {.xray = true});

    auto root = w->wlSurface()->resource();
    root->breadthfirst([&](SP<CWLSurfaceResource> s, const Vector2D &offset, void *) {
      if (!s->m_current.texture)
        return;

      CBox box = {
          (double)borderSize + (offset.x * scale),
          (double)borderSize + (offset.y * scale),
          s->m_current.size.x * scale,
          s->m_current.size.y * scale};

      g_pHyprOpenGL->renderTexture(s->m_current.texture, box, {.a = 1.0f});
    },
                       nullptr);

    g_pHyprOpenGL->renderRect(CBox{0, 0, fw, (double)borderSize}, borderColor, {});
    g_pHyprOpenGL->renderRect(CBox{0, fh - borderSize, fw, (double)borderSize}, borderColor, {});
    g_pHyprOpenGL->renderRect(CBox{0, 0, (double)borderSize, fh}, borderColor, {});
    g_pHyprOpenGL->renderRect(CBox{fw - borderSize, 0, (double)borderSize, fh}, borderColor, {});

    // CBox titleBox = {0, 0, fw, (double)TEXTSIZE};
    // auto titlestr = middleTruncate(w->m_title);
    // auto title = g_pHyprOpenGL->renderText(titlestr, CHyprColor{1.0f, 1.0f, 1.0f, 1.0f}, TEXTSIZE);
    // g_pHyprOpenGL->renderTexture(title, titleBox, {});

    int textHeight = TEXTSIZE;
    int fontSize = TEXTSIZE;

    auto estmax = (fw / (TEXTSIZE * 0.6));
    std::string displayTitle = middleTruncate(w->m_title, estmax);

    auto titleTex = g_pHyprOpenGL->renderText(displayTitle, CHyprColor{1.f, 1.f, 1.f, 1.f}, TEXTSIZE);

    if (titleTex) {
      float texW = titleTex->m_size.x;
      float texH = titleTex->m_size.y;

      CBox titleBox = {
          (fw - texW) / 2.0,
          (textHeight - texH) / 2.0,
          texW,
          texH};

      g_pHyprOpenGL->renderRect(
          CBox{(double)(borderSize + 1), (double)borderSize, (fw - (borderSize * 2) - 1), titleBox.height},
          CHyprColor{0, 0, 0, 0.5f},
          {});

      g_pHyprOpenGL->renderTexture(titleTex, titleBox, {.a = 1.0f});
    }

    g_pHyprRenderer->endRender();
  }

  void draw() {
    auto dmg = CRegion({pos, size});
    g_pHyprOpenGL->renderTexture(fb.getTexture(), dmg.getExtents(), {.damage = &dmg});
  }
};

int activeIndex = 0;

class RenderPass : public IPassElement {
public:
  std::vector<TextureElement *> elements;
  RenderPass(std::vector<TextureElement *> e) : elements(e) {}

  virtual void draw(const CRegion &damage) {
    for (auto *el : elements) {
      el->draw();
    }
  }

  virtual bool needsLiveBlur() { return false; }
  virtual bool needsPrecomputeBlur() { return false; }
  virtual const char *passName() { return "TabCarouselPassElement"; }

  virtual std::optional<CBox> boundingBox() {
    if (elements.empty())
      return std::nullopt;
    return CBox(mon->m_position.x, mon->m_position.y, mon->m_size.x, mon->m_size.y);
  }

  virtual CRegion opaqueRegion() { return {}; }
};

class BlurPass : public IPassElement {
public:
  inline static bool dim = true;
  inline static CConfigValue<Hyprlang::INT> blur = CConfigValue<Hyprlang::INT>("decoration:blur:enabled");
  inline static float dimamount = 0.15f;
  inline static CHyprColor dimcolor = CHyprColor(0.0f, 0.0f, 0.0f, dimamount);

  virtual void draw(const CRegion &damage) {
    CBox monitorBox = {mon->m_position.x, mon->m_position.y, mon->m_size.x, mon->m_size.y};
    auto renderdata = CHyprOpenGLImpl::SRectRenderData{
        .damage = &damage,
        .blur = (*blur ? true : false),
    };
    g_pHyprOpenGL->renderRect(monitorBox, dimcolor, renderdata);
  }

  virtual bool needsLiveBlur() { return true; }
  virtual bool needsPrecomputeBlur() { return true; }
  virtual const char *passName() { return "TabCarouselBlurElement"; }

  virtual std::optional<CBox> boundingBox() {
    return CBox{mon->m_position.x, mon->m_position.y, mon->m_size.x, mon->m_size.y};
  }

  virtual CRegion opaqueRegion() { return {}; }
};

class CarouselManager {
public:
  bool active = false;
  int activeIndex = 0;
  std::vector<std::unique_ptr<TextureElement>> elements;

  PHLMONITOR lastmon = nullptr;

  void toggle() {
    active = !active;
    if (!active)
      deactivate();
  }

  void activate() {
    if (active)
      return;
    active = true;
    lastmon = mon;
    refreshLayout();
  }

  void deactivate() {
    active = false;
    g_pHyprRenderer->m_renderPass.removeAllOfType("TabCarouselPassElement");
    g_pHyprRenderer->m_renderPass.removeAllOfType("TabCarouselBlurElement");
    if (mon)
      g_pHyprRenderer->damageMonitor(mon);
  }

  void next() {
    if (elements.empty())
      return;
    activeIndex = (activeIndex + 1) % elements.size();
    refreshLayout();
  }

  void prev() {
    if (elements.empty())
      return;
    activeIndex = (activeIndex - 1 + elements.size()) % elements.size();
    refreshLayout();
  }

  void confirm() {
    if (elements.empty())
      return;
    auto window = elements[activeIndex]->w;
    Desktop::focusState()->fullWindowFocus(window);
    deactivate();
  }

  void refreshLayout() {
    if (!mon || elements.empty())
      return;

    Vector2D screenCenter = mon->m_position + (mon->m_size / 2.0);
    double activeHeight = mon->m_size.y * 0.4;
    double inactiveHeight = mon->m_size.y * 0.3;
    double spacing = 10;

    for (size_t i = 0; i < elements.size(); ++i) {
      Vector2D winSize = elements[i]->w->m_realSize->goal();
      double aspect = winSize.x / std::max(winSize.y, 1.0);
      double targetH = (i == (size_t)activeIndex) ? activeHeight : inactiveHeight;
      elements[i]->targetSize = Vector2D(targetH * aspect, targetH);
    }

    elements[activeIndex]->targetPos = screenCenter - (elements[activeIndex]->targetSize / 2.0);

    for (int i = activeIndex - 1; i >= 0; --i) {
      auto &nextEl = elements[i + 1];
      elements[i]->targetPos.x = nextEl->targetPos.x - elements[i]->targetSize.x - spacing;
      elements[i]->targetPos.y = screenCenter.y - (elements[i]->targetSize.y / 2.0);
    }

    for (size_t i = activeIndex + 1; i < elements.size(); ++i) {
      auto &prevEl = elements[i - 1];
      elements[i]->targetPos.x = prevEl->targetPos.x + prevEl->targetSize.x + spacing;
      elements[i]->targetPos.y = screenCenter.y - (elements[i]->targetSize.y / 2.0);
    }
  }

  void onPreRender() {
    if (!active || elements.empty())
      return;

    CRegion totalDamage;
    for (auto &el : elements) {
      el->tick(0.1f);
      el->update(1, CHyprColor{0.39f, 0.45f, 0.55f, 1.0f});
      totalDamage.add(CBox{el->pos, el->size});
    }

    if (mon)
      g_pHyprRenderer->damageMonitor(mon);
  }

  std::vector<TextureElement *> getRenderList() {
    std::vector<TextureElement *> list;
    for (auto &el : elements)
      list.push_back(el.get());

    std::sort(list.begin(), list.end(), [this](TextureElement *a, TextureElement *b) {
      int idxA = getIndex(a);
      int idxB = getIndex(b);
      return std::abs(idxA - activeIndex) > std::abs(idxB - activeIndex);
    });
    return list;
  }

private:
  int getIndex(TextureElement *el) {
    for (int i = 0; i < (int)elements.size(); ++i) {
      if (elements[i].get() == el)
        return i;
    }
    return -1;
  }
};

inline UP<CarouselManager> g_pCarouselManager = makeUnique<CarouselManager>();

static void onPreRender() {
  if (g_pCarouselManager->active) {
    g_pCarouselManager->onPreRender();
  }
}

static void onRender(eRenderStage stage) {
  if (!g_pCarouselManager->active)
    return;

  if (stage == eRenderStage::RENDER_LAST_MOMENT) {
    g_pHyprRenderer->m_renderPass.add(makeUnique<BlurPass>());
    g_pHyprRenderer->m_renderPass.add(makeUnique<RenderPass>(g_pCarouselManager->getRenderList()));
  }
}

static void onWindowCreated(PHLWINDOW w) {
  g_pCarouselManager->elements.emplace_back(std::make_unique<TextureElement>(w));
  g_pCarouselManager->refreshLayout();
}

static void onWindowClosed(PHLWINDOW w) {
  std::erase_if(g_pCarouselManager->elements, [&](auto &el) {
    return el->w == w;
  });

  if (g_pCarouselManager->active) {
    g_pCarouselManager->activeIndex %= std::max((size_t)1, g_pCarouselManager->elements.size());
    g_pCarouselManager->refreshLayout();
  }
}

static void onMonitorAdded() {
  mon = g_pCompositor->getMonitorFromName("WAYLAND-1");
  if (!mon)
    mon = g_pCompositor->getMonitorFromID(0);
}

static CFunctionHook *keyhookfn = nullptr;
typedef bool (*CKeybindManager_onKeyEvent)(void *self, std::any &event, SP<IKeyboard> pKeyboard);

static bool onKeyEvent(void *self, std::any event, SP<IKeyboard> pKeyboard) {
  if (!keyhookfn || !keyhookfn->m_original)
    return false;

  auto e = std::any_cast<IKeyboard::SKeyEvent>(event);
  const auto MODS = g_pInputManager->getModsFromAllKBs();

  if (!g_pCarouselManager->active && e.state == WL_KEYBOARD_KEY_STATE_PRESSED) {
    if (e.keycode == 15 && (MODS & HL_MODIFIER_ALT)) {
      g_pCarouselManager->activate();
    }
  }

  if (!g_pCarouselManager->active)
    return ((CKeybindManager_onKeyEvent)keyhookfn->m_original)(self, event, pKeyboard);

  if (e.state == WL_KEYBOARD_KEY_STATE_PRESSED) {
    if (e.keycode == 15) {
      if (MODS & HL_MODIFIER_SHIFT)
        g_pCarouselManager->prev();
      else
        g_pCarouselManager->next();
      return true;
    }
  } else {
    if (e.keycode == 56 || e.keycode == 100) {
      g_pCarouselManager->confirm();
      return true;
    }
  }

  return true;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
  PHANDLE = handle;
  if (const std::string hash = __hyprland_api_get_hash(); hash != __hyprland_api_get_client_hash())
    throw std::runtime_error("Version mismatch");

  static auto PRENDER = HyprlandAPI::registerCallbackDynamic(handle, "render", [&](void *s, SCallbackInfo &i, std::any p) { onRender(std::any_cast<eRenderStage>(p)); });
  static auto PPRERENDER = HyprlandAPI::registerCallbackDynamic(handle, "preRender", [&](void *s, SCallbackInfo &i, std::any p) { onPreRender(); });
  static auto PMONITORADD = HyprlandAPI::registerCallbackDynamic(handle, "monitorAdded", [&](void *s, SCallbackInfo &i, std::any p) { onMonitorAdded(); });
  static auto POPENWINDOW = HyprlandAPI::registerCallbackDynamic(handle, "openWindow", [&](void *s, SCallbackInfo &i, std::any p) { onWindowCreated(std::any_cast<PHLWINDOW>(p)); });
  static auto PCLOSEWINDOW = HyprlandAPI::registerCallbackDynamic(handle, "closeWindow", [&](void *s, SCallbackInfo &i, std::any p) { onWindowClosed(std::any_cast<PHLWINDOW>(p)); });

  auto keyhooklookup = HyprlandAPI::findFunctionsByName(PHANDLE, "onKeyEvent");
  if (keyhooklookup.size() != 1) {
    for (auto &f : keyhooklookup)
      Log::logger->log(Log::ERR, "onKeyEvent found at {} :: sig: {}, demangled: {}", f.address, f.signature, f.demangled);
    throw std::runtime_error("CKeybindManager::onKeyEvent not found");
  }
  Log::logger->log(Log::ERR, "onKeyEvent found at {} :: sig: {}, demangled: {}", keyhooklookup[0].address, keyhooklookup[0].signature,
                   keyhooklookup[0].demangled);
  keyhookfn = HyprlandAPI::createFunctionHook(PHANDLE, keyhooklookup[0].address, (void *)onKeyEvent);
  auto success = keyhookfn->hook();
  if (!success)
    throw std::runtime_error("Failed to hook CKeybindManager::onKeyEvent");

  return {PLUGIN_NAME, PLUGIN_DESCRIPTION, PLUGIN_AUTHOR, PLUGIN_VERSION};
}

APICALL EXPORT void PLUGIN_EXIT() {
  g_pCarouselManager.reset();
}
