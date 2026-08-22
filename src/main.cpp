#include "win32_app.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int commandShow) {
    return Win32App{}.Run(instance, commandShow);
}
