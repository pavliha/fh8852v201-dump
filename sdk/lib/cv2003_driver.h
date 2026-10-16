#ifndef __CV2003_DRIVER_H__
#define __CV2003_DRIVER_H__

#include <stdint.h>

int cv2003_init(void);
void cv2003_deinit(void);
int cv2003_set_format(int format);
void cv2003_build_vtable(uint32_t vtable[31]);

#endif
