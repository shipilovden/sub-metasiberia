/*=====================================================================
InstallerApp.cpp
--------------
CEF-based installer application for Metasiberia Beta
Uses CEF to display web-based installation UI
=====================================================================*/
#include "InstallerApp.h"
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>

#ifdef CEF_SUPPORT
#include <cef_app.h>
#include <cef_client.h>
#include <wrapper/cef_helpers.h>

class InstallerClient : public CefClient, public CefDisplayHandler, public CefLifeSpanHandler
{
public:
    InstallerClient() {}

    // CefClient methods
    virtual CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
    virtual CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }

    // CefDisplayHandler methods
    virtual void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override {}

    // CefLifeSpanHandler methods
    virtual void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {}
    virtual bool DoClose(CefRefPtr<CefBrowser> browser) override { return false; }
    virtual void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {}

private:
    IMPLEMENT_REFCOUNTING(InstallerClient);
};

class InstallerApp : public CefApp, public CefBrowserProcessHandler
{
public:
    InstallerApp() {}

    virtual CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }

    virtual void OnContextInitialized() override
    {
        CEF_REQUIRE_UI_THREAD();

        CefWindowInfo window_info;
        window_info.SetAsPopup(NULL, "Metasiberia Beta Installer");
        window_info.width = 800;
        window_info.height = 600;
        window_info.x = 100;
        window_info.y = 100;

        CefBrowserSettings browser_settings;
        std::string html_path = GetInstallerHTMLPath();
        // Convert backslashes to forward slashes for file:// URL
        std::replace(html_path.begin(), html_path.end(), '\\', '/');
        std::string url = "file:///" + html_path;

        CefRefPtr<InstallerClient> client(new InstallerClient());
        CefBrowserHost::CreateBrowser(window_info, client, CefString(url), browser_settings, NULL, NULL);
    }

private:
    std::string GetInstallerHTMLPath()
    {
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        std::string exe_path = path;
        size_t last_slash = exe_path.find_last_of("\\/");
        if (last_slash != std::string::npos)
        {
            exe_path = exe_path.substr(0, last_slash + 1);
        }
        return exe_path + "installer_ui.html";
    }

    IMPLEMENT_REFCOUNTING(InstallerApp);
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    CefMainArgs main_args(hInstance);

    CefSettings settings;
    settings.no_sandbox = true;
    settings.multi_threaded_message_loop = false;
    settings.log_severity = LOGSEVERITY_WARNING;

    CefRefPtr<InstallerApp> app(new InstallerApp());

    int exit_code = CefExecuteProcess(main_args, app, NULL);
    if (exit_code >= 0)
        return exit_code;

    CefInitialize(main_args, settings, app, NULL);
    CefRunMessageLoop();
    CefShutdown();

    return 0;
}

#else
// Fallback if CEF not available
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    MessageBoxA(NULL, "CEF support is required for this installer.", "Error", MB_OK | MB_ICONERROR);
    return 1;
}
#endif
