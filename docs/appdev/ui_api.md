# UI API (`libui.h`)

For an application to be visible on the screen, it must interact with the BoredOS Window Manager (WM). The tools required for this are located in `src/userland/libc/libui.h` and `libui.c`.

## Core Concepts

The UI library sends requests (via `SYS_GUI`) to the kernel to reserve an area on the screen (a `Window`) and then issues commands to color specific pixels within that area. The kernel is responsible for compositing this area over other windows.

## Example: Creating a Window

First, include the library and define an event structure:

```c
#include <libui.h>
#include <stdlib.h>

int main(void) {
    // 1. Create the window
    // Arguments: Title, Width, Height, Flags (e.g. 0 for bordered window)
    int window_id = ui_create_window("Hello World App", 400, 300, 0);

    if (window_id < 0) {
        printf("Failed to create window!\n");
        return 1;
    }

    // ... Event loop will go here ...
    return 0;
}
```

## Drawing Primitives

The library offers functions to mutate the window's internal buffer. After issuing drawing commands, you **must** instruct the kernel to push the changes onto the screen.

```c
// Fill the entire window with a solid blue background
// Arguments: Window ID, X, Y, Width, Height, ARGB Color value
ui_fill_rect(window_id, 0, 0, 400, 300, 0xFF0000FF);

// Tell the kernel to commit the drawing commands to the screen
ui_swap_buffers(window_id);
```

Available rendering methods:
-   `ui_fill_rect(id, x, y, w, h, color)`: Draw a solid rectangle.
-   `ui_draw_rect(id, x, y, w, h, color)`: Draw an outline of a rectangle.
-   `ui_draw_line(id, x0, y0, x1, y1, color)`: Bresenham line algorithm.
-   `ui_draw_string(id, string, x, y, color)`: Render text using the kernel's built-in font.
-   `ui_update_region(id, x, y, w, h)`: A targeted version of `ui_swap_buffers` that only updates a specific area, saving performance.

## Handling the Event Loop

Graphical applications are event-driven. They stay alive inside a `while (1)` loop, periodically asking the kernel if the user clicked the mouse or pressed a key inside their window.

```c
    ui_event_t event;

    // Main UI Loop
    while (1) {
        // ui_poll_event is non-blocking. It returns 1 if an event occurred, 0 otherwise.
        if (ui_poll_event(&event)) {
            
            // The WM dispatch sets event.window_id 
            // We only care about events meant for our specific window
            if (event.window_id == window_id) {

                if (event.type == UI_EVENT_MOUSE_DOWN) {
                    printf("User clicked at X:%d Y:%d\n", event.mouse_x, event.mouse_y);
                    
                    // Respond visually to the click
                    ui_fill_rect(window_id, event.mouse_x, event.mouse_y, 10, 10, 0xFFFF0000); // Red dot
                    ui_swap_buffers(window_id);
                } 
                else if (event.type == UI_EVENT_WINDOW_CLOSE) {
                    // Start tearing down the application safely
                    break;
                }
            }
        }
        
        // Prevent 100% CPU usage by yielding execution time back to the OS scheduler
        syscall1(SYSTEM_CMD_YIELD, 0);
    }
```
