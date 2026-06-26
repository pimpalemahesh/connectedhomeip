// Implementation of the esp_encrypted_img mock (see esp_encrypted_img_mock.h).
#include "esp_encrypted_img_mock.h"

#include <cstdlib>
#include <cstring>

EspEncMock gEspEncMock;

namespace {
// A non-null sentinel returned as the opaque handle.
int sHandleToken = 0;
} // namespace

esp_decrypt_handle_t esp_encrypted_img_decrypt_start(const esp_decrypt_cfg_t * cfg)
{
    gEspEncMock.startCalls++;
    (void) cfg;
    if (gEspEncMock.startReturnsNull)
    {
        return nullptr;
    }
    return &sHandleToken;
}

esp_err_t esp_encrypted_img_decrypt_data(esp_decrypt_handle_t handle, pre_enc_decrypt_arg_t * args)
{
    gEspEncMock.dataCalls++;
    (void) handle;
    args->data_out     = nullptr;
    args->data_out_len = 0;

    // Hard error: emit nothing.
    if (gEspEncMock.dataReturn != ESP_OK && gEspEncMock.dataReturn != ESP_ERR_NOT_FINISHED)
    {
        return gEspEncMock.dataReturn;
    }

    if (gEspEncMock.produceOutput && args->data_in_len > 0)
    {
        char * out = static_cast<char *>(malloc(args->data_in_len)); // caller takes ownership
        for (size_t i = 0; i < args->data_in_len; i++)
        {
            out[i] = static_cast<char>(static_cast<unsigned char>(args->data_in[i]) ^ gEspEncMock.byteXor);
        }
        args->data_out     = out;
        args->data_out_len = args->data_in_len;
        gEspEncMock.allocCount++;
    }
    return gEspEncMock.dataReturn;
}

esp_err_t esp_encrypted_img_decrypt_end(esp_decrypt_handle_t handle)
{
    gEspEncMock.endCalls++;
    (void) handle;
    return gEspEncMock.endReturn;
}

esp_err_t esp_encrypted_img_decrypt_abort(esp_decrypt_handle_t handle)
{
    gEspEncMock.abortCalls++;
    (void) handle;
    return ESP_OK;
}
