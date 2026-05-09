#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include "platform/vita/display.h"
#include "platform/vita/input.h"
#include "ui/shell.h"

int main() {
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
    sceKernelExitProcess(0);
    return 0;
}
