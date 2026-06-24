#include "Application.h"

#include <windows.h>

int APIENTRY wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR commandLine,
    _In_ int nCmdShow
) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(commandLine);
    Application app;
    if (!app.Initialize(hInstance, nCmdShow)) {
        return 1;
    }
    return app.Run();
}
