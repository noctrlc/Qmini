#include "notify.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <stdio.h>
#include <stdlib.h>

static const GUID CLSID_MMDeviceEnumerator =
    {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const GUID IID_IMMDeviceEnumerator =
    {0xA95664D2, 0xDF14, 0x4FAF, {0xA2, 0x5F, 0xBE, 0x47, 0x23, 0xAE, 0x0E, 0x63}};

static const IID IID_IMMNotificationClient =
    {0x7991EEC9, 0x7E89, 0x4D85, {0x83, 0x90, 0x6C, 0x70, 0x3C, 0xEC, 0x60, 0xC0}};

typedef struct {
    IMMNotificationClientVtbl *lpVtbl;
    LONG    ref;
    int    *restart_capture;
    int    *restart_playback;
    void   *enumerator;
} notify_client_t;

static notify_client_t *g_notify_client = NULL;

static HRESULT STDMETHODCALLTYPE nc_query(IMMNotificationClient *self, REFIID riid, void **ppv) {
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMMNotificationClient)) {
        *ppv = self;
        ((IMMNotificationClient*)self)->lpVtbl->AddRef(self);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE nc_add(IMMNotificationClient *self) {
    notify_client_t *nc = (notify_client_t*)self;
    return InterlockedIncrement(&nc->ref);
}
static ULONG STDMETHODCALLTYPE nc_release(IMMNotificationClient *self) {
    notify_client_t *nc = (notify_client_t*)self;
    LONG r = InterlockedDecrement(&nc->ref);
    if (r == 0) free(nc);
    return r;
}
static HRESULT STDMETHODCALLTYPE nc_def0(IMMNotificationClient *self, LPCWSTR id) {
    (void)self; (void)id; return S_OK;
}
static HRESULT STDMETHODCALLTYPE nc_def1(IMMNotificationClient *self, LPCWSTR id, DWORD st) {
    (void)self; (void)id; (void)st; return S_OK;
}
static HRESULT STDMETHODCALLTYPE nc_def2(IMMNotificationClient *self, LPCWSTR id, PROPERTYKEY key) {
    (void)self; (void)id; (void)key; return S_OK;
}
static HRESULT STDMETHODCALLTYPE nc_changed(IMMNotificationClient *self, EDataFlow flow, ERole role, LPCWSTR id) {
    (void)id;
    notify_client_t *nc = (notify_client_t*)self;
    if (role == eCommunications || role == eConsole) {
        if (flow == eCapture && nc->restart_capture) *nc->restart_capture = 1;
        if (flow == eRender && nc->restart_playback) *nc->restart_playback = 1;
    }
    return S_OK;
}

static const IMMNotificationClientVtbl g_nc_vtbl = {
    nc_query, nc_add, nc_release,
    nc_def1, nc_def0, nc_def0, nc_changed, nc_def2
};

int notify_init(void *unused, int *restart_capture, int *restart_playback) {
    (void)unused;
    notify_client_t *nc = (notify_client_t*)calloc(1, sizeof(notify_client_t));
    if (!nc) return 0;
    nc->lpVtbl = (IMMNotificationClientVtbl*)&g_nc_vtbl;
    nc->ref = 1;
    nc->restart_capture = restart_capture;
    nc->restart_playback = restart_playback;
    nc->enumerator = NULL;

    /* Create our own enumerator and register */
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    IMMDeviceEnumerator *e = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL,
                                  CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER,
                                  &IID_IMMDeviceEnumerator, (void**)&e);
    if (FAILED(hr) || !e) { free(nc); return 0; }

    hr = IMMDeviceEnumerator_RegisterEndpointNotificationCallback(e, (IMMNotificationClient*)nc);
    if (FAILED(hr)) { free(nc); IMMDeviceEnumerator_Release(e); return 0; }

    nc->enumerator = e;
    g_notify_client = nc;
    return 1;
}

void notify_shutdown(void *unused, void *unused2) {
    (void)unused; (void)unused2;
    if (g_notify_client && g_notify_client->enumerator) {
        IMMDeviceEnumerator *e = (IMMDeviceEnumerator*)g_notify_client->enumerator;
        IMMDeviceEnumerator_UnregisterEndpointNotificationCallback(e, (IMMNotificationClient*)g_notify_client);
        IMMDeviceEnumerator_Release(e);
    }
    g_notify_client = NULL;
}

void* notify_get_client(void) {
    return g_notify_client;
}
