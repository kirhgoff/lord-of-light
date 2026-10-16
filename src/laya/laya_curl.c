#include <curl/curl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "laya.h"

#define CONNECT_TIMEOUT_MS 1000
#define REQUEST_PATH "/v1/systemone"

typedef struct {
    int id;
    char url[LAYA_URL_LEN + sizeof REQUEST_PATH];
    char *body;
} Request;

typedef struct {
    LayaTransport base;
    int timeout_ms;
    pthread_t worker;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    Request pending[MAX_COMBATANTS];
    int pending_head;
    int pending_count;
    LayaReply done[MAX_COMBATANTS];
    int done_head;
    int done_count;
    bool closing;
    atomic_bool abort_transfer;
} CurlTransport;

typedef struct {
    char *text;
    size_t length;
    bool too_large;
} Download;

static size_t append_download(char *chunk, size_t size, size_t count, void *user)
{
    Download *download = user;
    size_t added = size * count;
    if (download->length + added > LAYA_REPLY_MAX) return download->too_large = true, 0;
    char *grown = realloc(download->text, download->length + added + 1);
    if (!grown) return 0;
    memcpy(grown + download->length, chunk, added);
    download->text = grown;
    download->length += added;
    download->text[download->length] = '\0';
    return added;
}

static int abort_when_closing(void *user, curl_off_t total_down, curl_off_t down, curl_off_t total_up, curl_off_t up)
{
    (void)total_down;
    (void)down;
    (void)total_up;
    (void)up;
    return atomic_load(&((CurlTransport *)user)->abort_transfer);
}

static LayaReply perform(CURL *curl, const Request *request, CurlTransport *transport)
{
    Download download = {0};
    struct curl_slist *headers = curl_slist_append(NULL, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, request->url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request->body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)transport->timeout_ms);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, (long)CONNECT_TIMEOUT_MS);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, abort_when_closing);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, transport);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append_download);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &download);
    CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_off_t pretransfer_us = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_getinfo(curl, CURLINFO_PRETRANSFER_TIME_T, &pretransfer_us);
    curl_slist_free_all(headers);
    TransferOutcome outcome = {.curl_ok = result == CURLE_OK, .timed_out = result == CURLE_OPERATION_TIMEDOUT, .connected = pretransfer_us > 0, .too_large = download.too_large, .http_status = status};
    LayaReply reply = {request->id, download.text, laya_classify_transfer(&outcome)};
    if (reply.failure != FALLBACK_NONE) free(reply.text), reply.text = NULL;
    return reply;
}

static void *work(void *arg)
{
    CurlTransport *transport = arg;
    CURL *curl = curl_easy_init();
    pthread_mutex_lock(&transport->lock);
    for (;;) {
        while (!transport->closing && transport->pending_count == 0) pthread_cond_wait(&transport->wake, &transport->lock);
        if (transport->closing) break;
        Request request = transport->pending[transport->pending_head];
        transport->pending_head = (transport->pending_head + 1) % MAX_COMBATANTS;
        transport->pending_count--;
        pthread_mutex_unlock(&transport->lock);
        LayaReply reply = curl ? perform(curl, &request, transport) : (LayaReply){request.id, NULL, FALLBACK_UNREACHABLE};
        free(request.body);
        pthread_mutex_lock(&transport->lock);
        if (transport->done_count == MAX_COMBATANTS) {
            free(reply.text);
            continue;
        }
        transport->done[(transport->done_head + transport->done_count++) % MAX_COMBATANTS] = reply;
    }
    pthread_mutex_unlock(&transport->lock);
    if (curl) curl_easy_cleanup(curl);
    return NULL;
}

static bool curl_send(LayaTransport *self, int id, const char *url, const char *body)
{
    CurlTransport *transport = (CurlTransport *)self;
    pthread_mutex_lock(&transport->lock);
    bool room = transport->pending_count < MAX_COMBATANTS;
    if (room) {
        Request *slot = &transport->pending[(transport->pending_head + transport->pending_count++) % MAX_COMBATANTS];
        slot->id = id;
        snprintf(slot->url, sizeof slot->url, "%s" REQUEST_PATH, url);
        slot->body = strdup(body);
        pthread_cond_signal(&transport->wake);
    }
    pthread_mutex_unlock(&transport->lock);
    return room;
}

static bool curl_poll(LayaTransport *self, LayaReply *out)
{
    CurlTransport *transport = (CurlTransport *)self;
    pthread_mutex_lock(&transport->lock);
    bool ready = transport->done_count > 0;
    if (ready) {
        *out = transport->done[transport->done_head];
        transport->done_head = (transport->done_head + 1) % MAX_COMBATANTS;
        transport->done_count--;
    }
    pthread_mutex_unlock(&transport->lock);
    return ready;
}

static void curl_close(LayaTransport *self)
{
    CurlTransport *transport = (CurlTransport *)self;
    atomic_store(&transport->abort_transfer, true);
    pthread_mutex_lock(&transport->lock);
    transport->closing = true;
    pthread_cond_signal(&transport->wake);
    pthread_mutex_unlock(&transport->lock);
    pthread_join(transport->worker, NULL);
    for (int i = 0; i < transport->pending_count; i++) free(transport->pending[(transport->pending_head + i) % MAX_COMBATANTS].body);
    for (int i = 0; i < transport->done_count; i++) free(transport->done[(transport->done_head + i) % MAX_COMBATANTS].text);
    pthread_mutex_destroy(&transport->lock);
    pthread_cond_destroy(&transport->wake);
    curl_global_cleanup();
    free(transport);
}

LayaTransport *laya_curl_transport(int timeout_ms)
{
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return NULL;
    CurlTransport *transport = calloc(1, sizeof *transport);
    if (!transport) return NULL;
    transport->base = (LayaTransport){curl_send, curl_poll, curl_close};
    transport->timeout_ms = timeout_ms;
    pthread_mutex_init(&transport->lock, NULL);
    pthread_cond_init(&transport->wake, NULL);
    if (pthread_create(&transport->worker, NULL, work, transport) != 0) {
        free(transport);
        return NULL;
    }
    return &transport->base;
}
