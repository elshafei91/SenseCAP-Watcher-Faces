#include "esp_check.h"
#include "sscma_client_proto.h"

static const char *TAG = "sscma_client.proto";

esp_err_t sscma_client_proto_start(sscma_client_proto_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    ESP_RETURN_ON_FALSE(handle->start, ESP_ERR_NOT_SUPPORTED, TAG, "not supported");
    return handle->start(handle);
}

esp_err_t sscma_client_proto_write(sscma_client_proto_handle_t handle, const void *data, size_t len)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    ESP_RETURN_ON_FALSE(handle->write, ESP_ERR_NOT_SUPPORTED, TAG, "not supported");
    return handle->write(handle, data, len);
}

esp_err_t sscma_client_proto_finish(sscma_client_proto_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    ESP_RETURN_ON_FALSE(handle->finish, ESP_ERR_NOT_SUPPORTED, TAG, "not supported");
    return handle->finish(handle);
}

esp_err_t sscma_client_proto_abort(sscma_client_proto_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    ESP_RETURN_ON_FALSE(handle->abort, ESP_ERR_NOT_SUPPORTED, TAG, "not supported");
    return handle->abort(handle);
}

esp_err_t sscma_client_proto_delete(sscma_client_proto_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    ESP_RETURN_ON_FALSE(handle->del, ESP_ERR_NOT_SUPPORTED, TAG, "not supported");
    return handle->del(handle);
}
