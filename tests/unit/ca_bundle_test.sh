#!/bin/sh
# https.c's CA bundle (https_ca_file): every certificate of the system's store and hassmic's cacerts.pem in one file,
# but none that names eMudhra (DISTRUSTED).  A stand-in store of two self-signed roots, one of them eMudhra's by name,
# and a harness that asks https_ca_file() for the bundle (HASSMIC_CA_STORE, HASSMIC_STATE).
set -e
cd "$(dirname "$0")/../.."
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
mkdir -p "$T/store" "$T/state"
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$T/k1" -out "$T/store/aaaa0001.0" -days 30 -subj "/O=Good Root Inc/CN=Good Root" 2>/dev/null
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$T/k2" -out "$T/store/bbbb0002.0" -days 30 -subj "/O=eMudhra Technologies Limited/CN=emSign Root CA - G1" 2>/dev/null
openssl x509 -in "$T/store/aaaa0001.0" -noout -text >> "$T/store/aaaa0001.0"      # Android's files carry the text too
cat > "$T/h.c" <<'C'
#include "https.h"
#include <stdio.h>
int main(void) { const char *f = https_ca_file(); printf("%s\n", f ? f : "(none)"); return !f; }
C
cc -O2 -w -DVERSION="\"test\"" -Isrc/hassmic "$T/h.c" src/hassmic/https.c src/hassmic/net.c src/hassmic/hash.c src/hassmic/statefile.c -ldl -lpthread -o "$T/h" 2>"$T/cc.log" || { cat "$T/cc.log"; exit 1; }
out=$(HASSMIC_CA_STORE="$T/store" HASSMIC_STATE="$T/state" "$T/h" 2>"$T/log")
fail=0
n=$(grep -c "BEGIN CERTIFICATE" "$T/state/cacerts.bundle.pem" 2>/dev/null || echo 0)
good=$(openssl x509 -in "$T/state/cacerts.bundle.pem" -noout -subject 2>/dev/null)
[ "$out" = "$T/state/cacerts.bundle.pem" ] && echo "ok   the bundle is written to state/cacerts.bundle.pem" || { echo "FAIL bundle path: $out"; fail=1; }
[ "$n" = 1 ] && echo "ok   one certificate in it (the store's two, the eMudhra one left out)" || { echo "FAIL $n certificates in the bundle"; fail=1; }
case "$good" in *"Good Root"*) echo "ok   the one kept is the other root";; *) echo "FAIL kept: $good"; fail=1;; esac
grep -q "1 distrusted left out" "$T/log" && echo "ok   the log says one was left out" || { echo "FAIL log: $(cat "$T/log")"; fail=1; }
exit $fail
