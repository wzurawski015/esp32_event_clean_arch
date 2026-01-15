#include "infra_log_stream.h"

#include "sdkconfig.h"

#if CONFIG_INFRA_LOG_STREAM

#include "core/spsc_ring.h"

#include <string.h>

#ifndef CONFIG_INFRA_LOG_STREAM_RING_SIZE
#define CONFIG_INFRA_LOG_STREAM_RING_SIZE 4096
#endif

#if ((CONFIG_INFRA_LOG_STREAM_RING_SIZE & (CONFIG_INFRA_LOG_STREAM_RING_SIZE - 1)) != 0)
#error "CONFIG_INFRA_LOG_STREAM_RING_SIZE musi być potęgą 2 (np. 1024, 2048, 4096)"
#endif

static uint8_t s_storage_[CONFIG_INFRA_LOG_STREAM_RING_SIZE];
static spsc_ring_t s_rb_;
static bool s_init_ = false;
static uint32_t s_drop_ = 0;

void infra_log_stream_init(void)
{
    if (s_init_)
    {
        return;
    }

    s_init_ = spsc_ring_init(&s_rb_, s_storage_, (uint32_t)sizeof(s_storage_));
}

bool infra_log_stream_write_all(const void* data, const size_t len)
{
    if (!s_init_)
    {
        infra_log_stream_init();
    }

    if (!s_init_ || (data == NULL) || (len == 0u))
    {
        return false;
    }

    if (spsc_ring_free(&s_rb_) < len)
    {
        s_drop_++;
        return false;
    }

    const uint8_t* p = (const uint8_t*)data;
    size_t rem = len;

    while (rem > 0u)
    {
        size_t n = 0u;
        uint8_t* dst = spsc_ring_reserve(&s_rb_, rem, &n);
        if ((dst == NULL) || (n == 0u))
        {
            s_drop_++;
            return false;
        }

        memcpy(dst, p, n);
        spsc_ring_commit(&s_rb_, n);

        p += n;
        rem -= n;
    }

    return true;
}

const uint8_t* infra_log_stream_peek(size_t* out_len)
{
    if (!s_init_)
    {
        if (out_len != NULL)
        {
            *out_len = 0u;
        }
        return NULL;
    }

    return spsc_ring_peek(&s_rb_, out_len);
}

void infra_log_stream_consume(const size_t len)
{
    if (!s_init_)
    {
        return;
    }

    spsc_ring_consume(&s_rb_, len);
}

size_t infra_log_stream_capacity(void)
{
    return (size_t)sizeof(s_storage_);
}

size_t infra_log_stream_used(void)
{
    if (!s_init_)
    {
        return 0u;
    }

    return spsc_ring_used(&s_rb_);
}

uint32_t infra_log_stream_drop_count(void)
{
    return s_drop_;
}

#else // !CONFIG_INFRA_LOG_STREAM

void infra_log_stream_init(void) {}

bool infra_log_stream_write_all(const void* data, const size_t len)
{
    (void)data;
    (void)len;
    return false;
}

const uint8_t* infra_log_stream_peek(size_t* out_len)
{
    if (out_len != NULL)
    {
        *out_len = 0u;
    }
    return NULL;
}

void infra_log_stream_consume(const size_t len)
{
    (void)len;
}

size_t infra_log_stream_capacity(void)
{
    return 0u;
}

size_t infra_log_stream_used(void)
{
    return 0u;
}

uint32_t infra_log_stream_drop_count(void)
{
    return 0u;
}

#endif
