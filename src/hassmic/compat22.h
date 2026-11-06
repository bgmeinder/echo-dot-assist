/* bionic before API 24 (the Echo Spot: Android 5.1, API 22) lacks calls the shared code makes.  The Makefile
 * passes -include of this header and links compat22.c for such a target; the NDK's own headers declare them only
 * from the API they came with (ifaddrs and if_nameindex: 24, open_memstream: 23), so the shared code keeps its includes. */
#ifndef COMPAT22_H
#define COMPAT22_H
#if defined(__ANDROID_API__) && __ANDROID_API__ < 24
#ifndef _GNU_SOURCE
#define _GNU_SOURCE                 /* before the first header: this one comes ahead of every file's own define */
#endif
#include <ifaddrs.h>
#include <net/if.h>
#include <stdio.h>
#include <unistd.h>
int getifaddrs(struct ifaddrs **ifap);
void freeifaddrs(struct ifaddrs *ifa);
struct if_nameindex *if_nameindex(void);
void if_freenameindex(struct if_nameindex *list);
FILE *open_memstream(char **bufp, size_t *sizep);
#endif
#endif
