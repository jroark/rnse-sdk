#ifndef RNSE_DOOM_UNISTD_H
#define RNSE_DOOM_UNISTD_H
#include <sys/types.h>
int isatty(int fd);
int usleep(unsigned int us);
int unlink(const char *path);
int access(const char *path, int mode);
#define F_OK 0
#define R_OK 4
#endif
