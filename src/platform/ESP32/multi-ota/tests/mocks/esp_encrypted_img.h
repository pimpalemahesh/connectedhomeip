// Mock of ESP-IDF <esp_encrypted_img.h> for host unit tests. NOT the real header.
//
// Implements just the surface EncryptedOTAHelper uses. Behavior is driven by the
// test through the control block in esp_encrypted_img_mock.h.
#pragma once

#include "esp_err.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void * esp_decrypt_handle_t;

typedef struct
{
    const char * rsa_priv_key;
    size_t rsa_priv_key_len;
} esp_decrypt_cfg_t;

typedef struct
{
    const char * data_in;
    size_t data_in_len;
    char * data_out;
    size_t data_out_len;
} pre_enc_decrypt_arg_t;

esp_decrypt_handle_t esp_encrypted_img_decrypt_start(const esp_decrypt_cfg_t * cfg);
esp_err_t esp_encrypted_img_decrypt_data(esp_decrypt_handle_t handle, pre_enc_decrypt_arg_t * args);
esp_err_t esp_encrypted_img_decrypt_end(esp_decrypt_handle_t handle);
esp_err_t esp_encrypted_img_decrypt_abort(esp_decrypt_handle_t handle);

#ifdef __cplusplus
}
#endif
