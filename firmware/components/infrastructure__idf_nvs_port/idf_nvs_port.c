#include "ports/kv_port.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char* TAG = "NVS_ADP";

/* Definicja struktury ukrytej za uchwytem */
struct kv_handle {
    nvs_handle_t h;
    char partition[16]; /* Przechowujemy nazwę partycji dla celów diagnostycznych */
};

/* --- Helpers --- */

static port_err_t map_err(esp_err_t err) {
    switch(err) {
        case ESP_OK:                    return PORT_OK;
        case ESP_ERR_NVS_NOT_FOUND:     return PORT_FAIL; /* Klucz nie istnieje - to nie jest krytyczny błąd */
        case ESP_ERR_NVS_INVALID_HANDLE: return PORT_ERR_INVALID_ARG;
        case ESP_ERR_NVS_INVALID_NAME:   return PORT_ERR_INVALID_ARG;
        case ESP_ERR_NVS_NOT_ENOUGH_SPACE: return PORT_FAIL;
        case ESP_ERR_NVS_VALUE_TOO_LONG: return PORT_FAIL;
        default:                        return PORT_FAIL;
    }
}

/* --- Lifecycle --- */

port_err_t kv_open(const kv_cfg_t* cfg, kv_handle_t** out_handle) {
    if (!cfg || !cfg->namespace_name || !out_handle) {
        return PORT_ERR_INVALID_ARG;
    }

    /* Safety Guard: Limit nazwy namespace w NVS to 15 znaków (plus null) */
    if (strnlen(cfg->namespace_name, 16) > 15) {
        ESP_LOGE(TAG, "Namespace '%s' too long (max 15 chars)", cfg->namespace_name);
        return PORT_ERR_INVALID_ARG;
    }

    const char* part = cfg->partition_name ? cfg->partition_name : NVS_DEFAULT_PART_NAME;
    nvs_open_mode_t mode = cfg->read_only ? NVS_READONLY : NVS_READWRITE;
    nvs_handle_t h_nvs;
    esp_err_t err;

    /* Wybór API w zależności od tego, czy używamy domyślnej partycji */
    if (strcmp(part, NVS_DEFAULT_PART_NAME) == 0) {
        err = nvs_open(cfg->namespace_name, mode, &h_nvs);
    } else {
        err = nvs_open_from_partition(part, cfg->namespace_name, mode, &h_nvs);
    }

    if (err != ESP_OK) {
        /* Loguj błędy, ale ignoruj "NOT_FOUND" przy pierwszym uruchomieniu, to normalne */
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "nvs_open failed part='%s' ns='%s' err=0x%x", part, cfg->namespace_name, err);
        }
        return map_err(err);
    }

    /* Alokacja wrappera */
    struct kv_handle* k = (struct kv_handle*)malloc(sizeof(struct kv_handle));
    if (!k) {
        nvs_close(h_nvs);
        return PORT_FAIL; /* OOM */
    }

    k->h = h_nvs;
    /* Kopiuj nazwę partycji bezpiecznie */
    strncpy(k->partition, part, sizeof(k->partition) - 1);
    k->partition[sizeof(k->partition) - 1] = '\0';

    *out_handle = k;
    return PORT_OK;
}

void kv_close(kv_handle_t* handle) {
    if (handle) {
        nvs_close(handle->h);
        free(handle);
    }
}

port_err_t kv_commit(kv_handle_t* handle) {
    if (!handle) return PORT_ERR_INVALID_ARG;
    return map_err(nvs_commit(handle->h));
}

/* --- Setters --- */

port_err_t kv_set_int(kv_handle_t* h, const char* key, int32_t val) {
    if (!h || !key) return PORT_ERR_INVALID_ARG;
    return map_err(nvs_set_i32(h->h, key, val));
}

port_err_t kv_set_string(kv_handle_t* h, const char* key, const char* val) {
    if (!h || !key || !val) return PORT_ERR_INVALID_ARG;
    return map_err(nvs_set_str(h->h, key, val));
}

port_err_t kv_set_blob(kv_handle_t* h, const char* key, const void* data, size_t len) {
    if (!h || !key || !data) return PORT_ERR_INVALID_ARG;
    return map_err(nvs_set_blob(h->h, key, data, len));
}

/* --- Getters --- */

port_err_t kv_get_int(kv_handle_t* h, const char* key, int32_t* out_val) {
    if (!h || !key || !out_val) return PORT_ERR_INVALID_ARG;
    return map_err(nvs_get_i32(h->h, key, out_val));
}

port_err_t kv_get_string(kv_handle_t* h, const char* key, char* out_buf, size_t cap, size_t* out_len) {
    if (!h || !key) return PORT_ERR_INVALID_ARG;

    size_t required_size = 0;
    /* 1. Pobierz wymagany rozmiar (przekazując NULL jako bufor) */
    esp_err_t err = nvs_get_str(h->h, key, NULL, &required_size);
    if (err != ESP_OK) return map_err(err);

    /* NVS zwraca rozmiar z null-terminatorem. out_len ma zwrócić długość stringa. */
    if (out_len) {
        *out_len = (required_size > 0) ? (required_size - 1) : 0;
    }

    /* Jeśli użytkownik chciał tylko długość, wychodzimy */
    if (!out_buf) return PORT_OK;

    /* Sprawdź pojemność bufora */
    if (cap < required_size) {
        return PORT_FAIL; /* Buffer too small */
    }

    /* 2. Pobierz faktyczne dane */
    return map_err(nvs_get_str(h->h, key, out_buf, &required_size));
}

port_err_t kv_get_blob(kv_handle_t* h, const char* key, void* out_buf, size_t cap, size_t* out_len) {
    if (!h || !key) return PORT_ERR_INVALID_ARG;

    size_t required_size = 0;
    esp_err_t err = nvs_get_blob(h->h, key, NULL, &required_size);
    if (err != ESP_OK) return map_err(err);

    if (out_len) {
        *out_len = required_size;
    }

    if (!out_buf) return PORT_OK;

    if (cap < required_size) {
        return PORT_FAIL;
    }

    return map_err(nvs_get_blob(h->h, key, out_buf, &required_size));
}

/* --- Erasure --- */

port_err_t kv_erase(kv_handle_t* h, const char* key) {
    if (!h || !key) return PORT_ERR_INVALID_ARG;
    return map_err(nvs_erase_key(h->h, key));
}

port_err_t kv_erase_all(kv_handle_t* h) {
    if (!h) return PORT_ERR_INVALID_ARG;
    return map_err(nvs_erase_all(h->h));
}

/* --- Diagnostics --- */

port_err_t kv_get_stats(kv_handle_t* h, kv_stats_t* out_stats) {
    if (!h || !out_stats) return PORT_ERR_INVALID_ARG;

    nvs_stats_t stats;
    esp_err_t err = nvs_get_stats(h->partition, &stats);
    
    if (err == ESP_OK) {
        out_stats->used_entries = stats.used_entries;
        out_stats->free_entries = stats.free_entries;
        out_stats->total_entries = stats.total_entries;
        out_stats->namespace_count = stats.namespace_count;
    }
    return map_err(err);
}

