# Window Manager (WM)

BoredOS features a fully custom, graphical Window Manager built directly into the kernel, residing in the `src/wm/` directory. It is responsible for compositing the screen, handling window logic, rendering text, and dispatching UI events.

## Framebuffer and Rendering

1.  **Limine Framebuffer**: During boot, the Limine bootloader requests a graphical framebuffer from the hardware (e.g., GOP in UEFI environments) and passes a pointer to this linear memory buffer to the kernel.
2.  **Double Buffering**: To prevent screen tearing, the WM does not draw directly to the screen. It allocates a "back buffer" in kernel memory equal to the size of the screen. All drawing operations (lines, rectangles, windows) happen on this back buffer.
3.  **Compositing**: Once per frame or upon request, the entire back buffer (or dirty regions) is copied to the actual Limine physical framebuffer memory, making the changes visible instantly.

## Window System (`wm.c`)

The windowing system is built around a linked list of `Window` structures.

-   **Z-Ordering**: The list determines the draw order. Windows at the back of the list are drawn first, and the active window is drawn last (on top).
-   **Window Structures**: Each window object tracks its dimensions (`x`, `y`, `width`, `height`), title, background color, and an internal buffer if it's acting as a canvas for userland apps.
-   **Decorations**: The kernel handles drawing window borders, title bars, and close buttons automatically unless a borderless style is specified.

## Input Handling and Events

The WM acts as the central hub for input routing.

1.  **Mouse Driver**: The PS/2 mouse driver (`dev/mouse.c`) detects movement and button clicks. It raises interrupts that update global cursor coordinates.
2.  **Hit Testing**: The WM checks these coordinates against the bounding boxes of existing windows. It handles dragging logic (if the user clicks a title bar) or focus changes.
3.  **Event Queue**: If a userland application owns the window that was clicked, the WM packages the input (coordinates, button state) into an event message and drops it into the owning process's event queue. The application can retrieve these via the custom libc UI functions.

## Userland API (`libui.c`)

Applications do not talk to the hardware directly. Instead, they use a library (`libui.c`) which makes specialized system calls (`SYS_GUI`).

-   **Window Creation**: `ui_create_window()` asks the kernel to instantiate a new window object and returns a handle.
-   **Drawing**: Applications can request the kernel to fill rectangles or plot pixels inside their designated window area.
-   **Event Polling**: The UI loop inside an app continuously calls `ui_poll_event()` to respond to mouse clicks and window movement dispatched by the kernel WM.
