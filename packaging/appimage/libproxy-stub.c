/* Stand-in for libproxy.so.1, used by the AppImage only when the system has none (see
 * crtplayer-libproxy.sh). Qt Network links libproxy to look up the system proxy; without
 * any libproxy the AppImage could not start at all. This answers "no proxy" (direct). */
#include <stdlib.h>
#include <string.h>

typedef struct { int unused; } pxProxyFactory;

pxProxyFactory* px_proxy_factory_new(void) { return calloc(1, sizeof(pxProxyFactory)); }

char** px_proxy_factory_get_proxies(pxProxyFactory* f, const char* url)
{
    (void)f; (void)url;
    char** r = calloc(2, sizeof(char*));
    if (r) r[0] = strdup("direct://");
    return r;
}

void px_proxy_factory_free_proxies(char** proxies)
{
    if (!proxies) return;
    for (char** p = proxies; *p; ++p) free(*p);
    free(proxies);
}

void px_proxy_factory_free(pxProxyFactory* f) { free(f); }
