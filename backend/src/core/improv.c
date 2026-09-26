// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#include "improv.h"

#include <string.h>

#define IMPROV_MAGIC "IMPROV"
#define IMPROV_MAGIC_LEN 6
#define IMPROV_VERSION 1
// Header: magic + version + type + length.
#define IMPROV_HEADER_LEN 9

size_t improv_frame(uint8_t type, const uint8_t *data, size_t len,
                    uint8_t *out, size_t max)
{
    if (len > 255)
    {
        return 0;
    }
    // Header + data + checksum + newline.
    size_t need = (size_t)IMPROV_HEADER_LEN + len + 2;
    if (need > max)
    {
        return 0;
    }
    memcpy(out, IMPROV_MAGIC, IMPROV_MAGIC_LEN);
    out[6] = IMPROV_VERSION;
    out[7] = type;
    out[8] = (uint8_t)len;
    if (len > 0)
    {
        memcpy(out + IMPROV_HEADER_LEN, data, len);
    }
    uint8_t cksum = 0;
    for (size_t i = 0; i < IMPROV_HEADER_LEN + len; i++)
    {
        cksum += out[i];
    }
    out[IMPROV_HEADER_LEN + len] = cksum;
    out[IMPROV_HEADER_LEN + len + 1] = '\n';
    return need;
}

void improv_parser_init(improv_parser_t *p)
{
    memset(p, 0, sizeof(*p));
}

// Resync after a mismatch: the offending byte may itself open the magic.
static void improv_resync(improv_parser_t *p, uint8_t byte)
{
    if (byte == (uint8_t)'I')
    {
        p->buf[0] = byte;
        p->pos = 1;
    }
    else
    {
        p->pos = 0;
    }
    p->ready = false;
}

bool improv_parser_feed(improv_parser_t *p, uint8_t byte)
{
    // NOTE: no reset here — a completed result stays latched until the next
    // completed frame replaces it, so trailing bytes (every frame ends with
    // '\n', and chunks hold several frames) can never wipe it.
    if (p->pos < IMPROV_MAGIC_LEN)
    {
        if (byte == (uint8_t)IMPROV_MAGIC[p->pos])
        {
            p->buf[p->pos++] = byte;
        }
        else
        {
            improv_resync(p, byte);
        }
        return false;
    }
    if (p->pos == IMPROV_MAGIC_LEN)
    {
        // Version byte.
        if (byte != IMPROV_VERSION)
        {
            improv_resync(p, byte);
            return false;
        }
        p->buf[p->pos++] = byte;
        return false;
    }
    if (p->pos == IMPROV_MAGIC_LEN + 1)
    {
        p->type = byte;
        p->buf[p->pos++] = byte;
        return false;
    }
    if (p->pos == IMPROV_MAGIC_LEN + 2)
    {
        p->len = byte;
        p->buf[p->pos++] = byte;
        return false;
    }
    if (p->pos < (size_t)IMPROV_HEADER_LEN + p->len)
    {
        // Data bytes (none when len == 0: falls straight through).
        if (p->pos >= sizeof(p->buf))
        {
            improv_resync(p, byte);
            return false;
        }
        p->buf[p->pos++] = byte;
        return false;
    }
    // Checksum byte.
    uint8_t cksum = 0;
    for (size_t i = 0; i < p->pos; i++)
    {
        cksum += p->buf[i];
    }
    if (cksum != byte)
    {
        improv_resync(p, byte);
        return false;
    }
    p->pos = 0;
    p->ready = true;
    return true;
}

uint8_t improv_frame_type(const improv_parser_t *p)
{
    return p->type;
}

const uint8_t *improv_frame_data(const improv_parser_t *p)
{
    return p->buf + IMPROV_HEADER_LEN;
}

size_t improv_frame_len(const improv_parser_t *p)
{
    return p->len;
}

bool improv_rpc_parse(const uint8_t *data, size_t len, uint8_t *cmd_out,
                      const uint8_t **payload, size_t *payload_len)
{
    if (len < 2)
    {
        return false;
    }
    size_t plen = data[1];
    if ((size_t)2 + plen != len)
    {
        return false;
    }
    *cmd_out = data[0];
    *payload = data + 2;
    *payload_len = plen;
    return true;
}

bool improv_wifi_settings_parse(const uint8_t *data, size_t len, char *ssid,
                                size_t ssid_max, char *pass, size_t pass_max)
{
    if (len < 2)
    {
        return false;
    }
    size_t slen = data[0];
    if (slen == 0 || slen > ssid_max || (size_t)1 + slen + 1 > len)
    {
        return false;
    }
    size_t plen = data[1 + slen];
    if (plen > pass_max || (size_t)1 + slen + 1 + plen != len)
    {
        return false;
    }
    memcpy(ssid, data + 1, slen);
    ssid[slen] = '\0';
    memcpy(pass, data + 1 + slen + 1, plen);
    pass[plen] = '\0';
    return true;
}

bool improv_ota_password_parse(const uint8_t *data, size_t len, char *pass,
                               size_t pass_max)
{
    if (len < 1)
    {
        return false;
    }
    size_t plen = data[0];
    if (plen > pass_max || (size_t)1 + plen != len)
    {
        return false;
    }
    memcpy(pass, data + 1, plen);
    pass[plen] = '\0';
    return true;
}

size_t improv_result_payload(uint8_t cmd, const char *const *strs, size_t n,
                             uint8_t *out, size_t max)
{
    // [cmd][len][slen str]...: 2 header bytes + per-string overhead.
    size_t need = 2;
    for (size_t i = 0; i < n; i++)
    {
        size_t slen = strlen(strs[i]);
        if (slen > 255)
        {
            return 0;
        }
        need += 1 + slen;
    }
    if (need - 2 > 255 || need > max)
    {
        return 0;
    }
    out[0] = cmd;
    out[1] = (uint8_t)(need - 2);
    size_t pos = 2;
    for (size_t i = 0; i < n; i++)
    {
        size_t slen = strlen(strs[i]);
        out[pos++] = (uint8_t)slen;
        memcpy(out + pos, strs[i], slen);
        pos += slen;
    }
    return need;
}
