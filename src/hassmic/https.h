/* Media over HTTPS (issue #30): what Home Assistant hands us for TTS, announcements and play_media when it serves
 * HTTPS itself, fetched through the firmware's libcurl and handed on as a socket like a plain http:// fetch. */
#ifndef HTTPS_H
#define HTTPS_H

/* GET url (https://, redirects followed); a socket that reads the body, or -1 (logged).  Blocks until the response's
 * headers are in.  shutdown() or close() on the socket stops the transfer. */
int https_open(const char *url);

/* Check the server's certificate against the system's CAs and its name (setting "https_verify", on by default); 0
 * accepts any certificate (self-signed, or not naming the address Home Assistant gives out).  -1 reads */
int https_verify(int set);

/* The certificates to check against, set on a libcurl handle for every user of the firmware's libcurl (https.c,
 * update.c, davs.c; setopt: its curl_easy_setopt): CURLOPT_CAINFO the one bundle https.c writes into state/ (the
 * firmware's store and cacerts.pem's Let's Encrypt roots; HASSMIC_CA_FILE in tests), CURLOPT_CAPATH the firmware's
 * store.  Neither where there is none (the PC): libcurl keeps its own default.  That default is what libcurl was built
 * with, and rook's names a Linux file the Echo does not have: every certificate was refused there */
void https_ca_set(void *h, int (*setopt)(void *, int, ...));
/* the bundle's file, or NULL (tests) */
const char *https_ca_file(void);
#endif
