/* runexit.c - WinExec a command line, wait for it to finish, exit Windows.
 * usage: win runexit <delay seconds> <command line...> */
#include <windows.h>

static void Pump(DWORD ms)
{
    DWORD t0 = GetTickCount();
    MSG msg;
    while (GetTickCount() - t0 < ms) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Yield();
    }
}

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    HINSTANCE h;
    int delay = 0;

    while (*lpCmd >= '0' && *lpCmd <= '9')
        delay = delay * 10 + (*lpCmd++ - '0');
    while (*lpCmd == ' ')
        lpCmd++;

    Pump((DWORD)delay * 1000);              /* let the desktop settle */
    {
        /* module name = program name without path or extension */
        char mod[16];
        LPSTR p = lpCmd, s = lpCmd;
        int n = 0;
        while (*p && *p != ' ') { if (*p == '\\' || *p == ':') s = p + 1; p++; }
        while (s < p && *s != '.' && n < 15) mod[n++] = *s++;
        mod[n] = 0;

        h = (HINSTANCE)WinExec(lpCmd, SW_SHOWNORMAL);
        if (h >= HINSTANCE_ERROR)
            while (GetModuleHandle(mod))
                Pump(200);
    }
    Pump(1000);
    ExitWindows(0, 0);
    return 0;
}
