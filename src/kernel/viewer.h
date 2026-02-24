// viewer.h - Image Viewer app for BoredOS
#ifndef VIEWER_H
#define VIEWER_H

#include "wm.h"

extern Window win_viewer;

void viewer_init(void);
void viewer_open_file(const char *path);        // Safe from interrupt context (deferred)
void viewer_process_pending(void);              // Call from main loop only

#endif // VIEWER_H
