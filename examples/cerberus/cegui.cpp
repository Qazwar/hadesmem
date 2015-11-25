// Copyright (C) 2010-2015 Joshua Boyce
// See the file COPYING for copying permission.

#if defined(HADESMEM_DETAIL_ENABLE_CEGUI)

#include "cegui.hpp"

#include <memory>

#include <windows.h>
#include <windowsx.h>

#include <hadesmem/config.hpp>
#include <hadesmem/detail/filesystem.hpp>
#include <hadesmem/detail/self_path.hpp>
#include <hadesmem/detail/smart_handle.hpp>

// TODO: Define NOMINMAX in project settings. CEGUI undefines it and warns.
// Caused by Windows headers.
#include <hadesmem/detail/warning_disable_prefix.hpp>
#include <CEGUI/CEGUI.h>
#include <CEGUI/RendererModules/Direct3D9/Renderer.h>
#include <CEGUI/RendererModules/Direct3D10/Renderer.h>
#include <CEGUI/RendererModules/Direct3D11/Renderer.h>
#include <CEGUI/RendererModules/OpenGL/GLRenderer.h>
#include <hadesmem/detail/warning_disable_suffix.hpp>

#include "callbacks.hpp"
#include "cursor.hpp"
#include "hook_disabler.hpp"
#include "input.hpp"
#include "plugin.hpp"
#include "render.hpp"
#include "window.hpp"

#if defined(NDEBUG)
#pragma comment(lib, "CEGUIBase-0.lib")
#pragma comment(lib, "CEGUICommonDialogs-0.lib")
#pragma comment(lib, "CEGUICoreWindowRendererSet.lib")
#pragma comment(lib, "CEGUIDirect3D9Renderer-0.lib")
#pragma comment(lib, "CEGUIDirect3D10Renderer-0.lib")
#pragma comment(lib, "CEGUIDirect3D11Renderer-0.lib")
#pragma comment(lib, "CEGUIExpatParser.lib")
#pragma comment(lib, "CEGUILuaScriptModule-0.lib")
#pragma comment(lib, "CEGUIOpenGLRenderer-0.lib")
#pragma comment(lib, "CEGUISILLYImageCodec.lib")
#else
#pragma comment(lib, "CEGUIBase-0_d.lib")
#pragma comment(lib, "CEGUICommonDialogs-0_d.lib")
#pragma comment(lib, "CEGUICoreWindowRendererSet_d.lib")
#pragma comment(lib, "CEGUIDirect3D9Renderer-0_d.lib")
#pragma comment(lib, "CEGUIDirect3D10Renderer-0_d.lib")
#pragma comment(lib, "CEGUIDirect3D11Renderer-0_d.lib")
#pragma comment(lib, "CEGUIExpatParser_d.lib")
#pragma comment(lib, "CEGUILuaScriptModule-0_d.lib")
#pragma comment(lib, "CEGUIOpenGLRenderer-0_d.lib")
#pragma comment(lib, "CEGUISILLYImageCodec_d.lib")
#endif

// TODO: Fix this to actually support multiple devices, OpenGL, expose to
// plugins, multiple simultaneous renderers (e.g. D3D10 and D3D11 at the same
// time), multiple windows, etc.

// TODO: Fix to load skins etc from correct full path rather than game dir.

// TODO: Add a basic console which exposes at the very least plugin load/unload
// functionality and memory dumping.

// TODO: Fix thread safety of initialization etc.

// TODO: Support putting all the CEGUI DLLs and their dependencies in a
// different directory (e.g. deps). Will require special support in the
// injector. Probably good to build it in to the proposed --cerberus flag.

namespace
{
// TODO: Put renderer name in console title.
class GameConsoleWindow
{
public:
  GameConsoleWindow();
  void SetVisible(bool visible);
  bool IsVisible();

private:
  void CreateCEGUIWindow();
  void RegisterHandlers();
  bool HandleTextSubmitted(CEGUI::EventArgs const& e);
  bool HandleSendButtonPressed(CEGUI::EventArgs const& e);
  void ParseText(CEGUI::String const& msg);
  void OutputText(CEGUI::String const& msg,
                  CEGUI::Colour colour = CEGUI::Colour(0xFFFFFFFF));

  CEGUI::Window* console_wnd_{nullptr};
};

GameConsoleWindow::GameConsoleWindow()
{
  CreateCEGUIWindow();
  SetVisible(false);
}

void GameConsoleWindow::CreateCEGUIWindow()
{
  CEGUI::WindowManager* pWindowManager =
    CEGUI::WindowManager::getSingletonPtr();
  console_wnd_ = pWindowManager->loadLayoutFromFile("console.layout");
  if (console_wnd_)
  {
    CEGUI::System::getSingleton().getDefaultGUIContext().setRootWindow(
      console_wnd_);
    RegisterHandlers();
  }
  else
  {
    CEGUI::Logger::getSingleton().logEvent(
      "Error: Unable to load the ConsoleWindow from .layout");
  }
}

void GameConsoleWindow::RegisterHandlers()
{
  console_wnd_->getChild("Submit")
    ->subscribeEvent(CEGUI::PushButton::EventClicked,
                     CEGUI::Event::Subscriber(
                       &GameConsoleWindow::HandleSendButtonPressed, this));

  console_wnd_->getChild("Editbox")->subscribeEvent(
    CEGUI::Editbox::EventTextAccepted,
    CEGUI::Event::Subscriber(&GameConsoleWindow::HandleTextSubmitted, this));
}

bool GameConsoleWindow::HandleTextSubmitted(CEGUI::EventArgs const& e)
{
  CEGUI::String const msg = console_wnd_->getChild("Editbox")->getText();
  ParseText(msg);
  CEGUI::Window* edit_box = console_wnd_->getChild("Editbox");
  edit_box->setText("");
  edit_box->activate();
  return true;
}

bool GameConsoleWindow::HandleSendButtonPressed(CEGUI::EventArgs const& e)
{
  CEGUI::String const msg = console_wnd_->getChild("Editbox")->getText();
  ParseText(msg);
  CEGUI::Window* edit_box = console_wnd_->getChild("Editbox");
  edit_box->setText("");
  edit_box->activate();

  return true;
}

void GameConsoleWindow::ParseText(CEGUI::String const& msg)
{
  std::string msg_str = msg.c_str();
  if (msg_str.length() >= 1)
  {
    if (msg_str.at(0) == '/')
    {
      std::string::size_type command_end = msg_str.find(" ", 1);
      std::string command = msg_str.substr(1, command_end - 1);
      std::string command_args =
        msg_str.substr(command_end + 1, msg_str.size() - (command_end + 1));
      for (std::string::size_type i = 0; i < command.size(); i++)
      {
        command[i] = tolower(command[i]);
      }

      if (command == "say")
      {
        std::string outString = "You:" + msg_str;
        OutputText(outString);
      }
      else if (command == "quit")
      {
        OutputText("quit command detected");
      }
      else if (command == "help")
      {
        OutputText("help command detected");
      }
      else
      {
        std::string outString = "<" + msg_str + "> is an invalid command.";
        auto const red = CEGUI::Colour(1.0f, 0.0f, 0.0f);
        OutputText(outString, red);
      }
    }
    else
    {
      OutputText(msg_str);
    }
  }
}

void GameConsoleWindow::OutputText(CEGUI::String const& msg,
                                   CEGUI::Colour colour)
{
  CEGUI::Listbox* outputWindow =
    static_cast<CEGUI::Listbox*>(console_wnd_->getChild("History"));
  CEGUI::ListboxTextItem* newItem = 0;
  newItem = new CEGUI::ListboxTextItem(msg);
  newItem->setTextColours(colour);
  outputWindow->addItem(newItem);
}

void GameConsoleWindow::SetVisible(bool visible)
{
  console_wnd_->setVisible(visible);

  CEGUI::Editbox* editBox =
    static_cast<CEGUI::Editbox*>(console_wnd_->getChild("Editbox"));
  if (visible)
  {
    editBox->activate();
  }
  else
  {
    editBox->deactivate();
  }
}

bool GameConsoleWindow::IsVisible()
{
  return console_wnd_->isVisible();
}

std::unique_ptr<GameConsoleWindow>& GetGameConsoleWindow()
{
  static std::unique_ptr<GameConsoleWindow> console;
  return console;
}

hadesmem::cerberus::Callbacks<hadesmem::cerberus::OnCeguiInitializeCallback>&
  GetOnCeguiInitializeCallbacks()
{
  static hadesmem::cerberus::Callbacks<
    hadesmem::cerberus::OnCeguiInitializeCallback> callbacks;
  return callbacks;
}

hadesmem::cerberus::Callbacks<hadesmem::cerberus::OnCeguiCleanupCallback>&
  GetOnCeguiCleanupCallbacks()
{
  static hadesmem::cerberus::Callbacks<
    hadesmem::cerberus::OnCeguiCleanupCallback> callbacks;
  return callbacks;
}

bool& GetCeguiInitialized(hadesmem::cerberus::RenderApi api)
{
  if (api == hadesmem::cerberus::RenderApi::kD3D9)
  {
    static bool initialized{false};
    return initialized;
  }
  else if (api == hadesmem::cerberus::RenderApi::kD3D10)
  {
    static bool initialized{false};
    return initialized;
  }
  else if (api == hadesmem::cerberus::RenderApi::kD3D11)
  {
    static bool initialized{false};
    return initialized;
  }
  else if (api == hadesmem::cerberus::RenderApi::kOpenGL32)
  {
    static bool initialized{false};
    return initialized;
  }
  else
  {
    HADESMEM_DETAIL_ASSERT(false);
    HADESMEM_DETAIL_THROW_EXCEPTION(
      hadesmem::Error{} << hadesmem::ErrorString{"Unknown render API."});
  }
}

void SetCeguiInitialized(hadesmem::cerberus::RenderApi api, bool value)
{
  auto& initialized = GetCeguiInitialized(api);
  initialized = value;
}

bool CeguiInitializedAny() noexcept
{
  return GetCeguiInitialized(hadesmem::cerberus::RenderApi::kD3D9) ||
         GetCeguiInitialized(hadesmem::cerberus::RenderApi::kD3D10) ||
         GetCeguiInitialized(hadesmem::cerberus::RenderApi::kD3D11) ||
         GetCeguiInitialized(hadesmem::cerberus::RenderApi::kOpenGL32);
}

class CeguiImpl : public hadesmem::cerberus::CeguiInterface
{
public:
  virtual std::size_t RegisterOnInitialize(
    std::function<hadesmem::cerberus::OnCeguiInitializeCallback> const&
      callback) final
  {
    auto& callbacks = GetOnCeguiInitializeCallbacks();
    return callbacks.Register(callback);
  }

  virtual void UnregisterOnInitialize(std::size_t id) final
  {
    auto& callbacks = GetOnCeguiInitializeCallbacks();
    return callbacks.Unregister(id);
  }

  virtual std::size_t RegisterOnCleanup(
    std::function<hadesmem::cerberus::OnCeguiCleanupCallback> const& callback)
    final
  {
    auto& callbacks = GetOnCeguiCleanupCallbacks();
    return callbacks.Register(callback);
  }

  virtual void UnregisterOnCleanup(std::size_t id) final
  {
    auto& callbacks = GetOnCeguiCleanupCallbacks();
    return callbacks.Unregister(id);
  }

  virtual bool IsInitialized() final
  {
    return CeguiInitializedAny();
  }
};

void OnCleanupCeguiGui(hadesmem::cerberus::RenderApi api)
{
  if (!GetCeguiInitialized(api))
  {
    return;
  }

  HADESMEM_DETAIL_TRACE_A("Calling Cegui cleanup callbacks.");

  auto& callbacks = GetOnCeguiCleanupCallbacks();
  callbacks.Run(&hadesmem::cerberus::GetCeguiInterface());

  HADESMEM_DETAIL_TRACE_A("Cleaning up Cegui.");

  CEGUI::System::getSingleton().destroy();

  SetCeguiInitialized(api, false);
}

void OnInitializeCeguiGui(hadesmem::cerberus::RenderApi api, void* device)
{
  if (CeguiInitializedAny())
  {
    HADESMEM_DETAIL_TRACE_A("WARNING! Cegui is already initialized. Skipping.");
    return;
  }

  HADESMEM_DETAIL_TRACE_A("Initializing Cegui.");

  switch (api)
  {
  case hadesmem::cerberus::RenderApi::kD3D9:
    CEGUI::Direct3D9Renderer::bootstrapSystem(
      static_cast<IDirect3DDevice9*>(device));
    break;
  case hadesmem::cerberus::RenderApi::kD3D10:
    CEGUI::Direct3D10Renderer::bootstrapSystem(
      static_cast<ID3D10Device*>(device));
    break;
  case hadesmem::cerberus::RenderApi::kD3D11:
  {
    ID3D11DeviceContext* device_context = nullptr;
    static_cast<ID3D11Device*>(device)->GetImmediateContext(&device_context);
    CEGUI::Direct3D11Renderer::bootstrapSystem(
      static_cast<ID3D11Device*>(device), device_context);
    break;
  }
  case hadesmem::cerberus::RenderApi::kOpenGL32:
    CEGUI::OpenGLRenderer::bootstrapSystem().enableExtraStateSettings(true);
    break;
  default:
    HADESMEM_DETAIL_ASSERT(false);
    HADESMEM_DETAIL_THROW_EXCEPTION(
      hadesmem::Error{} << hadesmem::ErrorString{"Unknown render API."});
  }

  SetCeguiInitialized(api, true);

  RECT wnd_rect{0, 0, 800, 600};
  auto& window_interface = hadesmem::cerberus::GetWindowInterface();
  if (auto const window = window_interface.GetCurrentWindow())
  {
    HADESMEM_DETAIL_TRACE_A("Have a window.");

    if (!::GetClientRect(window, &wnd_rect) || wnd_rect.right == 0 ||
        wnd_rect.bottom == 0)
    {
      HADESMEM_DETAIL_TRACE_A(
        "GetClientRect failed (or returned an invalid box).");

      wnd_rect = RECT{0, 0, 800, 600};
    }
    else
    {
      HADESMEM_DETAIL_TRACE_A("Got client rect.");
    }
  }
  else
  {
    HADESMEM_DETAIL_TRACE_A("Do not have a window.");
  }
  HADESMEM_DETAIL_TRACE_FORMAT_A(
    "Window size is %ldx%ld.", wnd_rect.right, wnd_rect.bottom);

  try
  {
    CEGUI::DefaultResourceProvider* rp =
      static_cast<CEGUI::DefaultResourceProvider*>(
        CEGUI::System::getSingleton().getResourceProvider());
    // TODO: Don't do this. Use the default directory layout including 
    // subdirectories...
    auto const cegui_path =
      hadesmem::detail::WideCharToMultiByte(hadesmem::detail::CombinePath(
        hadesmem::detail::GetSelfDirPath(), L"cegui"));
    rp->setResourceGroupDirectory("schemes", cegui_path);
    rp->setResourceGroupDirectory("imagesets", cegui_path);
    rp->setResourceGroupDirectory("fonts", cegui_path);
    rp->setResourceGroupDirectory("layouts", cegui_path);
    rp->setResourceGroupDirectory("looknfeels", cegui_path);
    rp->setResourceGroupDirectory("lua_scripts", cegui_path);

    CEGUI::ImageManager::setImagesetDefaultResourceGroup("imagesets");
    CEGUI::Font::setDefaultResourceGroup("fonts");
    CEGUI::Scheme::setDefaultResourceGroup("schemes");
    CEGUI::WidgetLookManager::setDefaultResourceGroup("looknfeels");
    CEGUI::WindowManager::setDefaultResourceGroup("layouts");
    CEGUI::ScriptModule::setDefaultResourceGroup("lua_scripts");

    CEGUI::SchemeManager::getSingletonPtr()->createFromFile(
      "TaharezLook.scheme");
    CEGUI::Font& default_font =
      CEGUI::FontManager::getSingleton().createFromFile("DejaVuSans-12.font");
    CEGUI::System::getSingleton().getDefaultGUIContext().setDefaultFont(
      &default_font);

    CEGUI::System::getSingleton().notifyDisplaySizeChanged(
      CEGUI::Sizef(wnd_rect.right, wnd_rect.bottom));

    auto& console = GetGameConsoleWindow();
    console = std::make_unique<GameConsoleWindow>();
  }
  catch (...)
  {
    HADESMEM_DETAIL_TRACE_A(
      boost::current_exception_diagnostic_information().c_str());
    OnCleanupCeguiGui(api);
  }

  HADESMEM_DETAIL_TRACE_A("Calling Cegui initialization callbacks.");

  auto& callbacks = GetOnCeguiInitializeCallbacks();
  callbacks.Run(&hadesmem::cerberus::GetCeguiInterface());
}

void SetAllCeguiVisibility(bool visible, bool /*old_visible*/)
{
  if (auto& console = GetGameConsoleWindow())
  {
    console->SetVisible(visible);
  }
}

UINT VirtualKeyToScanCode(WPARAM wParam, LPARAM lParam)
{
  if (HIWORD(lParam) & 0x0F00)
  {
    UINT scancode = MapVirtualKeyW(wParam, 0);
    return scancode | 0x80;
  }
  else
  {
    return HIWORD(lParam) & 0x00FF;
  }
}

void HandleInputQueueEntry(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
  if (!CeguiInitializedAny())
  {
    return;
  }

  auto& window_interface = hadesmem::cerberus::GetWindowInterface();
  if (hwnd == window_interface.GetCurrentWindow())
  {
    static auto last_input_time = ::GetTickCount64();

    float const elapsed = ::GetTickCount64() - last_input_time;
    CEGUI::System::getSingleton().injectTimePulse(elapsed);
    CEGUI::System::getSingleton().getDefaultGUIContext().injectTimePulse(
      elapsed);

    switch (msg)
    {
    case WM_CHAR:
      CEGUI::System::getSingleton().getDefaultGUIContext().injectChar(
        (CEGUI::utf32)wparam);
      break;

    case WM_KEYDOWN:
      CEGUI::System::getSingleton().getDefaultGUIContext().injectKeyDown(
        (CEGUI::Key::Scan)VirtualKeyToScanCode(wparam, lparam));
      break;

    case WM_KEYUP:
      CEGUI::System::getSingleton().getDefaultGUIContext().injectKeyUp(
        (CEGUI::Key::Scan)VirtualKeyToScanCode(wparam, lparam));
      break;

    case WM_MOUSEMOVE:
      CEGUI::System::getSingleton().getDefaultGUIContext().injectMousePosition(
        GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
      break;

    case WM_LBUTTONDOWN:
      CEGUI::System::getSingleton()
        .getDefaultGUIContext()
        .injectMouseButtonDown(CEGUI::LeftButton);
      break;

    case WM_LBUTTONUP:
      CEGUI::System::getSingleton().getDefaultGUIContext().injectMouseButtonUp(
        CEGUI::LeftButton);
      break;

    case WM_RBUTTONDOWN:
      CEGUI::System::getSingleton()
        .getDefaultGUIContext()
        .injectMouseButtonDown(CEGUI::RightButton);
      break;

    case WM_RBUTTONUP:
      CEGUI::System::getSingleton().getDefaultGUIContext().injectMouseButtonUp(
        CEGUI::RightButton);
      break;

    case WM_MBUTTONDOWN:
      CEGUI::System::getSingleton()
        .getDefaultGUIContext()
        .injectMouseButtonDown(CEGUI::MiddleButton);
      break;

    case WM_MBUTTONUP:
      CEGUI::System::getSingleton().getDefaultGUIContext().injectMouseButtonUp(
        CEGUI::MiddleButton);
      break;

    case WM_MOUSEWHEEL:
      CEGUI::System::getSingleton()
        .getDefaultGUIContext()
        .injectMouseWheelChange(GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA);
      break;

    // TODO: Should probably be using the viewport instead?
    case WM_SIZE:
      CEGUI::System::getSingleton().notifyDisplaySizeChanged(
        CEGUI::Sizef(LOWORD(lparam), HIWORD(lparam)));
      break;
    }
  }
}

void OnFrameCegui(hadesmem::cerberus::RenderApi /*api*/, void* /*device*/)
{
  if (!CeguiInitializedAny())
  {
    return;
  }

  CEGUI::System::getSingleton().renderAllGUIContexts();
}

void OnResizeCegui(hadesmem::cerberus::RenderApi /*api*/,
                   void* /*device*/,
                   UINT width,
                   UINT height)
{
  if (!CeguiInitializedAny())
  {
    return;
  }

  // TODO: Reduce code duplication between this and other renderers.
  if (!width || !height)
  {
    HADESMEM_DETAIL_TRACE_FORMAT_A("Size is zero, attempting to use client "
                                   "area of window. Width: [%u]. Height: [%u].",
                                   width,
                                   height);

    // TODO: Ensure we're using the right window.
    auto& window_interface = hadesmem::cerberus::GetWindowInterface();
    RECT rect{};
    if (::GetClientRect(window_interface.GetCurrentWindow(), &rect))
    {
      width = width ? width : rect.right;
      height = height ? height : rect.bottom;

      HADESMEM_DETAIL_TRACE_FORMAT_A(
        "Got client rect. Width: [%u]. Height: [%u].", width, height);
    }
    else
    {
      DWORD const last_error = ::GetLastError();
      HADESMEM_DETAIL_TRACE_FORMAT_A("GetClientRect failed. LastError: [%lu].",
                                     last_error);
    }
  }

  if (!width || !height)
  {
    HADESMEM_DETAIL_TRACE_A("Skipping resize due to unknown size.");
    return;
  }

  CEGUI::System::getSingleton().notifyDisplaySizeChanged(
    CEGUI::Sizef(width, height));
}

void OnUnloadPlugins()
{
  SetCeguiInitialized(hadesmem::cerberus::RenderApi::kD3D9, false);
  SetCeguiInitialized(hadesmem::cerberus::RenderApi::kD3D10, false);
  SetCeguiInitialized(hadesmem::cerberus::RenderApi::kD3D11, false);
  SetCeguiInitialized(hadesmem::cerberus::RenderApi::kOpenGL32, false);
}
}

namespace hadesmem
{
namespace cerberus
{
CeguiInterface& GetCeguiInterface() noexcept
{
  static CeguiImpl Cegui;
  return Cegui;
}

void InitializeCegui()
{
  auto& input = GetInputInterface();
  input.RegisterOnInputQueueEntry(HandleInputQueueEntry);

  auto& render = GetRenderInterface();
  render.RegisterOnFrame(OnFrameCegui);
  render.RegisterOnResize(OnResizeCegui);
  render.RegisterOnInitializeGui(OnInitializeCeguiGui);
  render.RegisterOnCleanupGui(OnCleanupCeguiGui);
  render.RegisterOnSetGuiVisibility(SetAllCeguiVisibility);

  RegisterOnUnloadPlugins(OnUnloadPlugins);
}
}
}

#endif
