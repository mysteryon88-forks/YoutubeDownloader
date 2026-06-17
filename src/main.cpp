#include "Application.h"

#include <windows.h>

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    Application app;
    if (!app.Initialize(hInstance, nCmdShow)) {
        return 1;
    }
    return app.Run();
}
