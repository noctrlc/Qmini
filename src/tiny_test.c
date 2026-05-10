#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)inst; (void)prev; (void)cmd; (void)show;
    MessageBoxW(NULL, L"Test OK", L"Qmini Test", MB_OK);
    return 0;
}
