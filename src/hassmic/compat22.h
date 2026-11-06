/* bionic before API 24 (the Echo Spot: Android 5.1, API 22) lacks three calls the shared code makes.  The Makefile
 * passes -include of this header and links compat22.c for such a target; the NDK's own headers declare them only
 * from the API they came with (ifaddrs: 24, open_memstream: 23), so the shared code keeps its includes. */
#ifndef COMPAT22_H
#define COMPAT22_H
#if defined(__ANDROID_API__) && __ANDROID_API__ < 24
#include <ifaddrs.h>
#include <stdio.h>
#include <unistd.h>
int getifaddrs(struct ifaddrs **ifap);
void freeifaddrs(struct ifaddrs *ifa);
FILE *open_memstream(char **bufp, size_t *sizep);
/* in libc.so since API 9 and 1, declared by the NDK's headers only from 24 and 23 */
int pipe2(int pipefd[2], int flags);
FILE *funopen(const void *cookie, int (*readfn)(void *, char *, int), int (*writefn)(void *, const char *, int),
              fpos_t (*seekfn)(void *, fpos_t, int), int (*closefn)(void *));
#endif
#endif
