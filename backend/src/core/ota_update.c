// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#include "ota_update.h"

#include "websocket.h"
#include "wifi.h"

#include "sdkconfig.h"

#include "cJSON.h"
#include "esp_chip_info.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/asn1.h"
#include "mbedtls/sha256.h"
#include <string.h>

static const char *TAG = "OTA_UPDATE";

// Release manifest location. Base overridable at build time for local
// testing (export OTA_RELEASE_BASE, plain http://<lan-ip>:8000 works);
// the per-chip file name derives from the IDF target (esp32c6, ...).
#ifndef OTA_RELEASE_BASE
#define OTA_RELEASE_BASE ""
#endif
#define OTA_RELEASE_BASE_DEFAULT "https://github.com/DavidBertet/SesameMaker/releases/latest/download"

// Release ECDSA-P256 public key: 128 hex chars (uncompressed point without
// the 0x04 prefix). Production key baked below (public by design);
// OTA_PUBKEY_HEX overrides it for local testing (export it pointed at
// tools/ota_dev_release.sh output). Empty override + empty default =
// on-device updates refused.
#ifndef OTA_PUBKEY_HEX
#define OTA_PUBKEY_HEX ""
#endif
#define OTA_PUBKEY_DEFAULT "0c8c9b162c9fcf3e446d3ecdcce1590e9c5785e3e1f5be3da9ad7335aa75384ee849750fbd29f9433d3676e95feb4a36ded9a43ff209ad03dcdd405dfebd3b23"

static const char *ota_pubkey_hex(void)
{
    return OTA_PUBKEY_HEX[0] ? OTA_PUBKEY_HEX : OTA_PUBKEY_DEFAULT;
}

#define OTA_TASK_STACK 16384
#define OTA_HTTP_BUF 2048
#define OTA_MANIFEST_MAX 4096

static const char *ota_base(void)
{
    return OTA_RELEASE_BASE[0] ? OTA_RELEASE_BASE : OTA_RELEASE_BASE_DEFAULT;
}

static bool ota_pubkey(uint8_t out[64])
{
    const char *hex = ota_pubkey_hex();
    if (strlen(hex) != 128)
    {
        return false;
    }
    for (int i = 0; i < 64; i++)
    {
        unsigned int byte;
        if (sscanf(hex + 2 * i, "%2x", &byte) != 1)
        {
            return false;
        }
        out[i] = (uint8_t)byte;
    }
    return true;
}

const char *ota_current_version(void)
{
#ifdef FW_VERSION
    return FW_VERSION[0] ? FW_VERSION : "dev";
#else
    return "dev";
#endif
}

const char *ota_chip_name(void)
{
    esp_chip_info_t info;
    esp_chip_info(&info);
    switch (info.model)
    {
    case CHIP_ESP32:
        return "ESP32";
    case CHIP_ESP32S2:
        return "ESP32-S2";
    case CHIP_ESP32S3:
        return "ESP32-S3";
    case CHIP_ESP32C3:
        return "ESP32-C3";
    case CHIP_ESP32C2:
        return "ESP32-C2";
    case CHIP_ESP32C6:
        return "ESP32-C6";
    case CHIP_ESP32H2:
        return "ESP32-H2";
    default:
        return "Unknown";
    }
}

// vX.Y.Z numeric compare; missing parts are 0, a trailing -suffix
// (prerelease) sorts below the bare release. Non-numeric cores are 0.
static int semver_part(const char **p)
{
    int v = 0;
    while (**p >= '0' && **p <= '9')
    {
        v = v * 10 + (**p - '0');
        (*p)++;
    }
    if (**p == '.')
    {
        (*p)++;
    }
    return v;
}

static int ota_semver_cmp(const char *a, const char *b)
{
    if (*a == 'v' || *a == 'V')
    {
        a++;
    }
    if (*b == 'v' || *b == 'V')
    {
        b++;
    }
    for (int i = 0; i < 3; i++)
    {
        int va = semver_part(&a);
        int vb = semver_part(&b);
        if (va != vb)
        {
            return va < vb ? -1 : 1;
        }
    }
    bool a_pre = *a == '-' || *a == '+';
    bool b_pre = *b == '-' || *b == '+';
    if (a_pre != b_pre)
    {
        return a_pre ? -1 : 1;
    }
    return 0;
}

bool ota_is_newer(const char *v)
{
    const char *cur = ota_current_version();
    if (strcmp(cur, "dev") == 0 || strcmp(cur, "unknown") == 0)
    {
        return true;
    }
    return ota_semver_cmp(v, cur) > 0;
}

// Manifest field values end up inside a rebuilt JSON string, so reject
// anything that could break out of it (quotes, backslashes, controls).
static bool json_safe(const char *s, size_t max)
{
    size_t n = strnlen(s, max + 1);
    if (n == 0 || n > max)
    {
        return false;
    }
    for (size_t i = 0; i < n; i++)
    {
        if (s[i] < 0x20 || s[i] == '"' || s[i] == '\\')
        {
            return false;
        }
    }
    return true;
}

// GET url into a malloc'd NUL-terminated buffer (caller frees). HTTPS
// validates against the IDF cert bundle; plain http is allowed for local
// test rigs (signature verification below is the real gate).
static bool ota_http_get(const char *url, char **out, size_t max_len, char *err, size_t errlen)
{
    esp_http_client_config_t config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
    {
        snprintf(err, errlen, "http init failed");
        return false;
    }
    if (esp_http_client_open(client, 0) != ESP_OK)
    {
        snprintf(err, errlen, "cannot reach update server");
        esp_http_client_cleanup(client);
        return false;
    }
    int content_len = esp_http_client_fetch_headers(client);
    if (esp_http_client_get_status_code(client) != 200)
    {
        snprintf(err, errlen, "update server answered HTTP %d",
                 esp_http_client_get_status_code(client));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }
    if (content_len > 0 && (size_t)content_len > max_len)
    {
        snprintf(err, errlen, "manifest too large (%d)", content_len);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }
    char *buf = malloc(max_len + 1);
    if (!buf)
    {
        snprintf(err, errlen, "out of memory");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }
    size_t got = 0;
    int n;
    while ((n = esp_http_client_read(client, buf + got, max_len - got)) > 0)
    {
        got += (size_t)n;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (n < 0)
    {
        snprintf(err, errlen, "download interrupted");
        free(buf);
        return false;
    }
    buf[got] = '\0';
    *out = buf;
    return true;
}

// Rebuild the signer's canonical bytes: manifest minus signature/key_id,
// keys sorted (app, chip, files, version), nested keys sorted, no spaces.
// Must match tools/sign_manifest.py byte-for-byte, or nothing verifies.
static bool ota_canonical(const char *app_name, const char *app_sha, int app_size,
                          const cJSON *files, const char *version, const char *chip,
                          char *out, size_t outlen)
{
    // files[] entries in manifest order, each {"name","offset","sha256","size"}.
    char files_json[3072] = {0};
    size_t fpos = 0;
    const cJSON *entry = NULL;
    bool first = true;
    cJSON_ArrayForEach(entry, files)
    {
        const cJSON *name = cJSON_GetObjectItem(entry, "name");
        const cJSON *offset = cJSON_GetObjectItem(entry, "offset");
        const cJSON *sha = cJSON_GetObjectItem(entry, "sha256");
        const cJSON *size = cJSON_GetObjectItem(entry, "size");
        if (!cJSON_IsString(name) || !cJSON_IsString(offset) || !cJSON_IsString(sha) ||
            !cJSON_IsNumber(size) || !json_safe(name->valuestring, 127) ||
            !json_safe(offset->valuestring, 15) || !json_safe(sha->valuestring, 64))
        {
            return false;
        }
        int w = snprintf(files_json + fpos, sizeof(files_json) - fpos,
                         "%s{\"name\":\"%s\",\"offset\":\"%s\",\"sha256\":\"%s\",\"size\":%d}",
                         first ? "" : ",", name->valuestring, offset->valuestring,
                         sha->valuestring, size->valueint);
        if (w < 0 || (size_t)w >= sizeof(files_json) - fpos)
        {
            return false;
        }
        fpos += (size_t)w;
        first = false;
    }
    int w = snprintf(out, outlen,
                     "{\"app\":{\"name\":\"%s\",\"offset\":\"ota\",\"sha256\":\"%s\",\"size\":%d},"
                     "\"chip\":\"%s\",\"files\":[%s],\"version\":\"%s\"}",
                     app_name, app_sha, app_size, chip, files_json, version);
    return w > 0 && (size_t)w < outlen;
}

static bool ota_verify_signature(const char *canonical, const char *sig_b64, char *err, size_t errlen)
{
    uint8_t pub[64];
    if (!ota_pubkey(pub))
    {
        snprintf(err, errlen, "no release key baked into this firmware");
        return false;
    }
    uint8_t hash[32];
    mbedtls_sha256((const uint8_t *)canonical, strlen(canonical), hash, 0);

    size_t der_len = 0;
    uint8_t der[80];
    if (mbedtls_base64_decode(der, sizeof(der), &der_len, (const uint8_t *)sig_b64,
                              strlen(sig_b64)) != 0)
    {
        snprintf(err, errlen, "signature is not valid base64");
        return false;
    }

    // mbedTLS 3.x keeps keypair members private: parse the DER SEQ{r,s}
    // via public asn1 helpers and verify against standalone objects.
    mbedtls_ecp_group grp;
    mbedtls_ecp_point Q;
    mbedtls_mpi r, s;
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&Q);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    bool ok = false;
    uint8_t point[65];
    point[0] = 0x04;
    memcpy(point + 1, pub, 64);
    {
        unsigned char *p = der;
        const unsigned char *end = der + der_len;
        size_t len = 0;
        if (mbedtls_asn1_get_tag(&p, end, &len,
                                 MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE) == 0 &&
            mbedtls_asn1_get_mpi(&p, end, &r) == 0 &&
            mbedtls_asn1_get_mpi(&p, end, &s) == 0 &&
            mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
            mbedtls_ecp_point_read_binary(&grp, &Q, point, sizeof(point)) == 0 &&
            mbedtls_ecdsa_verify(&grp, hash, sizeof(hash), &Q, &r, &s) == 0)
        {
            ok = true;
        }
        else
        {
            snprintf(err, errlen, "signature verification failed");
        }
    }
    mbedtls_ecp_group_free(&grp);
    mbedtls_ecp_point_free(&Q);
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    return ok;
}

bool ota_fetch_manifest(ota_manifest_t *out, char *err, size_t errlen)
{
    char url[256];
    snprintf(url, sizeof(url), "%s/sesamemaker-%s-manifest.json", ota_base(), CONFIG_IDF_TARGET);

    char *body = NULL;
    if (!ota_http_get(url, &body, OTA_MANIFEST_MAX, err, errlen))
    {
        return false;
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root)
    {
        snprintf(err, errlen, "manifest is not valid JSON");
        return false;
    }
    const cJSON *version = cJSON_GetObjectItem(root, "version");
    const cJSON *chip = cJSON_GetObjectItem(root, "chip");
    const cJSON *sig = cJSON_GetObjectItem(root, "signature");
    const cJSON *app = cJSON_GetObjectItem(root, "app");
    const cJSON *files = cJSON_GetObjectItem(root, "files");
    const cJSON *app_name = app ? cJSON_GetObjectItem(app, "name") : NULL;
    const cJSON *app_sha = app ? cJSON_GetObjectItem(app, "sha256") : NULL;
    const cJSON *app_size = app ? cJSON_GetObjectItem(app, "size") : NULL;
    bool valid =
        cJSON_IsString(version) && cJSON_IsString(chip) && cJSON_IsString(sig) &&
        cJSON_IsObject(app) && cJSON_IsArray(files) && cJSON_IsString(app_name) &&
        cJSON_IsString(app_sha) && cJSON_IsNumber(app_size) &&
        json_safe(version->valuestring, 31) && json_safe(chip->valuestring, 23) &&
        json_safe(app_name->valuestring, 127) && strlen(app_sha->valuestring) == 64;
    if (!valid)
    {
        snprintf(err, errlen, "manifest is missing required fields");
        cJSON_Delete(root);
        return false;
    }
    // Canonical bytes first (field-validated above), then the signature over
    // them. Nothing below this point trusts the network. Heap, not stack:
    // this runs beside a TLS session that already eats kilobytes.
    char *canonical = malloc(4096);
    if (!canonical)
    {
        snprintf(err, errlen, "out of memory");
        cJSON_Delete(root);
        return false;
    }
    if (!ota_canonical(app_name->valuestring, app_sha->valuestring, app_size->valueint, files,
                       version->valuestring, chip->valuestring, canonical, 4096))
    {
        snprintf(err, errlen, "manifest files section malformed");
        free(canonical);
        cJSON_Delete(root);
        return false;
    }
    if (!ota_verify_signature(canonical, sig->valuestring, err, errlen))
    {
        free(canonical);
        cJSON_Delete(root);
        return false;
    }
    free(canonical);
    snprintf(out->version, sizeof(out->version), "%s", version->valuestring);
    snprintf(out->chip, sizeof(out->chip), "%s", chip->valuestring);
    snprintf(out->app_name, sizeof(out->app_name), "%s", app_name->valuestring);
    snprintf(out->app_sha256, sizeof(out->app_sha256), "%s", app_sha->valuestring);
    out->app_size = (size_t)app_size->valueint;
    if (strcmp(out->chip, ota_chip_name()) != 0)
    {
        snprintf(err, errlen, "manifest targets %s, this is %s", out->chip, ota_chip_name());
        cJSON_Delete(root);
        return false;
    }
    if (out->app_size == 0 || out->app_size > 0x1C0000)
    {
        snprintf(err, errlen, "app image size implausible (%d)", (int)out->app_size);
        cJSON_Delete(root);
        return false;
    }
    cJSON_Delete(root);
    return true;
}

static bool s_updating = false;
static size_t s_progress_written = 0;
static size_t s_progress_total = 0;

bool ota_update_in_progress(void)
{
    return s_updating;
}

bool ota_update_progress(size_t *written, size_t *total)
{
    if (!s_updating)
    {
        return false;
    }
    if (written)
    {
        *written = s_progress_written;
    }
    if (total)
    {
        *total = s_progress_total;
    }
    return true;
}

static void ota_progress(const char *phase, size_t written, size_t total, bool done, bool success,
                         const char *message)
{
    char json[256];
    if (done)
    {
        if (message)
        {
            snprintf(json, sizeof(json),
                     "{\"type\":\"ota_progress\",\"phase\":\"done\",\"success\":%s,\"message\":\"%.127s\"}",
                     success ? "true" : "false", message);
        }
        else
        {
            snprintf(json, sizeof(json),
                     "{\"type\":\"ota_progress\",\"phase\":\"done\",\"success\":%s}",
                     success ? "true" : "false");
        }
    }
    else
    {
        snprintf(json, sizeof(json),
                 "{\"type\":\"ota_progress\",\"phase\":\"%s\",\"written\":%d,\"total\":%d}", phase,
                 (int)written, (int)total);
    }
    broadcast_message(json);
}

// Manifest directory + app file name = download URL (same release).
static void ota_app_url(const ota_manifest_t *m, char *out, size_t outlen)
{
    int w = snprintf(out, outlen, "%s/sesamemaker-%s-manifest.json", ota_base(), CONFIG_IDF_TARGET);
    if (w <= 0 || (size_t)w >= outlen)
    {
        out[0] = '\0';
        return;
    }
    char *slash = strrchr(out, '/');
    if (slash)
    {
        snprintf(slash + 1, outlen - (size_t)(slash + 1 - out), "%s", m->app_name);
    }
}

static void ota_update_task(void *arg)
{
    (void)arg;
    ota_manifest_t m;
    char err[128];
    if (!ota_fetch_manifest(&m, err, sizeof(err)))
    {
        ota_progress(NULL, 0, 0, true, false, err);
        s_updating = false;
        vTaskDelete(NULL);
        return;
    }
    if (!ota_is_newer(m.version))
    {
        ota_progress(NULL, 0, 0, true, false, "already up to date");
        s_updating = false;
        vTaskDelete(NULL);
        return;
    }

    char url[256];
    ota_app_url(&m, url, sizeof(url));
    esp_http_client_config_t config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client || esp_http_client_open(client, 0) != ESP_OK)
    {
        ota_progress(NULL, 0, 0, true, false, "cannot download firmware image");
        if (client)
        {
            esp_http_client_cleanup(client);
        }
        s_updating = false;
        vTaskDelete(NULL);
        return;
    }
    esp_http_client_fetch_headers(client);
    if (esp_http_client_get_status_code(client) != 200)
    {
        char msg[64];
        snprintf(msg, sizeof(msg), "firmware download answered HTTP %d",
                 esp_http_client_get_status_code(client));
        ota_progress(NULL, 0, 0, true, false, msg);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        s_updating = false;
        vTaskDelete(NULL);
        return;
    }

    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part)
    {
        ota_progress(NULL, 0, 0, true, false, "no OTA slot available");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        s_updating = false;
        vTaskDelete(NULL);
        return;
    }
    esp_ota_handle_t handle;
    if (esp_ota_begin(part, m.app_size, &handle) != ESP_OK)
    {
        ota_progress(NULL, 0, 0, true, false, "cannot start OTA write");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        s_updating = false;
        vTaskDelete(NULL);
        return;
    }

    uint8_t chunk[OTA_HTTP_BUF];
    size_t written = 0;
    size_t last_report = 0;
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    bool failed = false;
    int n;
    while ((n = esp_http_client_read(client, (char *)chunk, sizeof(chunk))) > 0)
    {
        if (esp_ota_write(handle, chunk, (size_t)n) != ESP_OK)
        {
            failed = true;
            break;
        }
        mbedtls_sha256_update(&sha, chunk, (size_t)n);
        written += (size_t)n;
        s_progress_written = written;
        s_progress_total = m.app_size;
        if (written - last_report >= m.app_size / 20 || written == m.app_size)
        {
            ota_progress("download", written, m.app_size, false, false, NULL);
            last_report = written;
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);

    char hex[65];
    for (int i = 0; i < 32; i++)
    {
        snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    }
    if (failed || n < 0 || written != m.app_size || strcmp(hex, m.app_sha256) != 0)
    {
        esp_ota_abort(handle);
        ota_progress(NULL, 0, 0, true, false, failed ? "flash write failed" : "image hash mismatch");
        s_updating = false;
        vTaskDelete(NULL);
        return;
    }
    if (esp_ota_end(handle) != ESP_OK || esp_ota_set_boot_partition(part) != ESP_OK)
    {
        ota_progress(NULL, 0, 0, true, false, "cannot finalize update");
        s_updating = false;
        vTaskDelete(NULL);
        return;
    }
    ota_progress(NULL, 0, 0, true, true, NULL);
    ESP_LOGI(TAG, "Self-update to %s staged, rebooting", m.version);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

void ota_start_task(void)
{
    if (s_updating)
    {
        return;
    }
    s_updating = true;
    xTaskCreate(ota_update_task, "ota_update", OTA_TASK_STACK, NULL, 5, NULL);
}

// Health = WiFi up (reachable for the next update and serving the UI) plus
// 30 s of stable runtime — or 10 min of plain uptime, whichever comes first.
// The fallback matters for USB-flashed boards with no WiFi yet: health is
// "runs without crashing", and a crash loop never reaches either bar, so it
// still rolls back. A power cut inside the window rolls back a healthy image
// too — accepted, same tradeoff as ESP-IDF's own rollback examples.
#define OTA_CONFIRM_GRACE_MS 30000
#define OTA_CONFIRM_UPTIME_FALLBACK_MS 600000

static void ota_confirm_task(void *arg)
{
    (void)arg;
    const esp_partition_t *running = esp_ota_get_boot_partition();
    esp_ota_img_states_t state;
    // Serial/factory images boot valid (or undefined) — nothing to confirm,
    // so USB flows without WiFi are unaffected. Only OTA-staged slots boot
    // pending and need the health check below.
    if (!running || esp_ota_get_state_partition(running, &state) != ESP_OK ||
        state != ESP_OTA_IMG_PENDING_VERIFY)
    {
        ESP_LOGD(TAG, "App slot not pending (%d), no confirmation needed", (int)state);
        vTaskDelete(NULL);
        return;
    }
    uint32_t boot_ms = 0;
    bool wifi_ok = false;
    uint32_t wifi_since = 0;
    ESP_LOGI(TAG, "App slot pending verify, watching health");
    while (1)
    {
        if (!wifi_ok && is_wifi_connected())
        {
            wifi_ok = true;
            wifi_since = boot_ms;
        }
        if ((wifi_ok && boot_ms - wifi_since >= OTA_CONFIRM_GRACE_MS) ||
            boot_ms >= OTA_CONFIRM_UPTIME_FALLBACK_MS)
        {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        boot_ms += 1000;
    }
    if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY)
    {
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK)
        {
            ESP_LOGI(TAG, "Boot healthy, app slot confirmed valid");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to confirm app slot");
        }
    }
    vTaskDelete(NULL);
}

void ota_confirm_boot(void)
{
    xTaskCreate(ota_confirm_task, "ota_confirm", 3072, NULL, 5, NULL);
}

static void ota_check_worker(void *arg)
{
    (void)arg;
    char json[256];
    ota_manifest_t m;
    char err[128];
    if (!ota_fetch_manifest(&m, err, sizeof(err)))
    {
        snprintf(json, sizeof(json),
                 "{\"type\":\"update_status\",\"available\":false,\"error\":\"%.127s\"}", err);
    }
    else
    {
        snprintf(json, sizeof(json),
                 "{\"type\":\"update_status\",\"available\":%s,\"current\":\"%.31s\",\"latest\":\"%.31s\"}",
                 ota_is_newer(m.version) ? "true" : "false", ota_current_version(), m.version);
    }
    broadcast_message(json);
    vTaskDelete(NULL);
}

void ota_check_task(void)
{
    xTaskCreate(ota_check_worker, "ota_check", OTA_TASK_STACK, NULL, 5, NULL);
}
