/*
Copyright (c) 2015, Plume Design Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Plume Design Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdint.h>

#include <jansson.h>

#include "log.h"
#include "json_util.h"

#if defined(JSON_MEMDBG)

#define JSON_MEMDBG_MAGIC 0xCCCCCCCC
#define JSON_MEMDBG_TIMER 20.0

struct json_memdbg
{
    size_t      magic;
    size_t      sz;
};

size_t json_memdbg_count = 0;
size_t json_memdbg_total = 0;
size_t json_memdbg_reported = 0;

void *json_memdbg_malloc(size_t sz)
{
    struct json_memdbg *md;

    json_memdbg_total += sz;
    json_memdbg_count++;

    md = malloc(sz + sizeof(struct json_memdbg));
    if (!md)
    {
        LOG(ERR, "MEMDBG: Failed to allocate %zu bytes", (sz + sizeof(struct json_memdbg)));
        return NULL;
    }

    md->magic   = JSON_MEMDBG_MAGIC;
    md->sz      = sz;

    return (uint8_t *)md + sizeof(struct json_memdbg);
}

void json_memdbg_free(void *p)
{
    struct json_memdbg *md;

    if (p == NULL)
    {
        LOG(ERR, "MEMDBG: attempted to free NULL pointer");
        return;
    }

    md = (struct json_memdbg *)(((uint8_t *)p) - sizeof(struct json_memdbg));

    if (md->magic != JSON_MEMDBG_MAGIC)
    {
        LOG(WARNING, "MEMDBG: Invalid MAGIC number when freeing jansson memory block.");
        return;
    }

    md->magic = 0;

    json_memdbg_total -= md->sz;
    json_memdbg_count--;

    free(md);
}

void json_memdbg_get_stats(size_t *total, size_t *count)
{
    if (total)
        *total = json_memdbg_total;
    if (count)
        *count = json_memdbg_count;
}

void json_memdbg_report(bool diff_only)
{
    if (diff_only && json_memdbg_total == json_memdbg_reported)
        return;

    LOG(INFO, "MEMDBG: Jansson memory used %zu bytes in %zu allocations.",
                                                json_memdbg_total, json_memdbg_count);
    json_memdbg_reported = json_memdbg_total;
}

void json_memdbg_do_report(EV_P_ ev_timer *w, int revents)
{
    (void)loop;
    (void)w;
    (void)revents;

    json_memdbg_report(true);
}

void json_memdbg_init(struct ev_loop *loop)
{
    static ev_timer report_timer;

    json_set_alloc_funcs(json_memdbg_malloc, json_memdbg_free);

    if (loop != NULL)
    {
        ev_timer_init(
                &report_timer,
                json_memdbg_do_report,
                JSON_MEMDBG_TIMER,
                JSON_MEMDBG_TIMER);

        ev_timer_start(loop, &report_timer);
    }

    LOG(INFO, "MEMDBG: Jansson memory debugger initialized.");
}

#else

void json_memdbg_init(struct ev_loop *loop)
{
    (void)loop;
}

void json_memdbg_report(bool diff_only)
{
    (void)diff_only;
    return;
}

void json_memdbg_get_stats(size_t *total, size_t *count)
{
    if (total)
        *total = 0;
    if (count)
        *count = 0;
}

#endif

