#include <psp2/kernel/processmgr.h>
#ifdef __vita__
#include <psp2/sysmodule.h>
#endif
#include <vita2d.h>

#include "platform/vita/display.h"
#include "platform/vita/input.h"
#include "ui/shell.h"

int main() {
#ifdef __vita__
    // Load modules required by PGF fonts, IME dialog, and networking.
    sceSysmoduleLoadModule(SCE_SYSMODULE_PGF);
#if defined(SCE_SYSMODULE_COMMON_DIALOG)
    sceSysmoduleLoadModule(SCE_SYSMODULE_COMMON_DIALOG);
#endif
    sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
#if defined(SCE_SYSMODULE_NETCTL)
    sceSysmoduleLoadModule(SCE_SYSMODULE_NETCTL);
#endif
    sceSysmoduleLoadModule(SCE_SYSMODULE_SSL);
    sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP);
#endif

    vita2d_init();
    vita2d_set_clear_color(RGBA8(0x10, 0x10, 0x18, 0xFF));

    platform::vita::Display display;
    platform::vita::Input   input;
    ui::Shell               shell;

    shell.init();

    bool running = true;
    while (running) {
        input.poll();

        shell.handle_input(input);
        if (shell.should_exit()) {
            running = false;
        }

        vita2d_start_drawing();
        vita2d_clear_screen();
        shell.render();
        vita2d_end_drawing();
        vita2d_swap_buffers();
    }

    shell.shutdown();
    vita2d_fini();

#ifdef __vita__
    sceSysmoduleUnloadModule(SCE_SYSMODULE_HTTP);
    sceSysmoduleUnloadModule(SCE_SYSMODULE_SSL);
#if defined(SCE_SYSMODULE_NETCTL)
    sceSysmoduleUnloadModule(SCE_SYSMODULE_NETCTL);
#endif
    sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
    sceSysmoduleUnloadModule(SCE_SYSMODULE_IME);
#if defined(SCE_SYSMODULE_COMMON_DIALOG)
    sceSysmoduleUnloadModule(SCE_SYSMODULE_COMMON_DIALOG);
#endif
    sceSysmoduleUnloadModule(SCE_SYSMODULE_PGF);
#endif

    sceKernelExitProcess(0);
    return 0;
}
