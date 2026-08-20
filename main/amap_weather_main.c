/*
 * AMap IP location and live weather client.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "sdkconfig.h"

#define AMAP_IP_LOCATION_URL "https://restapi.amap.com/v3/ip"
#define AMAP_WEATHER_URL "https://restapi.amap.com/v3/weather/weatherInfo"
#define AMAP_REQUEST_TIMEOUT_MS 10000
#define DEFAULT_CITY_NAME "广州"
#define DEFAULT_ADCODE "440100"
#define ADCODE_BUFFER_SIZE 16
#define RESPONSE_INITIAL_CAPACITY 1024

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} http_response_t;

static const char *TAG = "amap_weather";

static esp_err_t append_response_data(http_response_t *response, const char *data, size_t data_len)
{
    if (data_len > SIZE_MAX - response->length - 1) {
        return ESP_ERR_NO_MEM;
    }

    size_t required = response->length + data_len + 1;
    if (required > response->capacity) {
        size_t new_capacity = response->capacity == 0 ? RESPONSE_INITIAL_CAPACITY : response->capacity;
        while (new_capacity < required) {
            if (new_capacity > SIZE_MAX / 2) {
                new_capacity = required;
                break;
            }
            new_capacity *= 2;
        }

        char *new_data = realloc(response->data, new_capacity);
        if (new_data == NULL) {
            return ESP_ERR_NO_MEM;
        }
        response->data = new_data;
        response->capacity = new_capacity;
    }

    memcpy(response->data + response->length, data, data_len);
    response->length += data_len;
    response->data[response->length] = '\0';
    return ESP_OK;
}

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    http_response_t *response = event->user_data;

    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        esp_err_t err = append_response_data(response, event->data, event->data_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to allocate HTTP response buffer");
            return err;
        }
    }

    return ESP_OK;
}

static esp_err_t request_json(const char *url, const char *response_name, cJSON **json)
{
    http_response_t response = {0};
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = AMAP_REQUEST_TIMEOUT_MS,
        .user_agent = "amap-weather-esp-idf/1.0",
        .event_handler = http_event_handler,
        .user_data = &response,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    *json = NULL;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_header(client, "Accept", "application/json");
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    int status_code = esp_http_client_get_status_code(client);
    if (status_code < 200 || status_code >= 300) {
        ESP_LOGE(TAG, "HTTP request returned status code %d", status_code);
        err = ESP_FAIL;
        goto cleanup;
    }

    if (response.data == NULL || response.length == 0) {
        ESP_LOGE(TAG, "HTTP response body is empty");
        err = ESP_FAIL;
        goto cleanup;
    }

    ESP_LOGI(TAG, "%s: %s", response_name, response.data);

    *json = cJSON_ParseWithLength(response.data, response.length);
    if (*json == NULL) {
        ESP_LOGE(TAG, "HTTP response is not valid JSON");
        err = ESP_FAIL;
        goto cleanup;
    }

cleanup:
    esp_http_client_cleanup(client);
    free(response.data);
    return err;
}

static esp_err_t require_amap_success(const cJSON *json, const char *api_name)
{
    if (!cJSON_IsObject(json)) {
        ESP_LOGE(TAG, "%s response is not a JSON object", api_name);
        return ESP_FAIL;
    }

    const cJSON *status = cJSON_GetObjectItemCaseSensitive(json, "status");
    if (!cJSON_IsString(status) || strcmp(status->valuestring, "1") != 0) {
        const cJSON *info = cJSON_GetObjectItemCaseSensitive(json, "info");
        const cJSON *infocode = cJSON_GetObjectItemCaseSensitive(json, "infocode");
        ESP_LOGE(TAG, "%s failed: %s (infocode=%s)",
                 api_name,
                 cJSON_IsString(info) ? info->valuestring : "unknown error",
                 cJSON_IsString(infocode) ? infocode->valuestring : "unknown");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t query_ip_location(char *adcode, size_t adcode_size)
{
    char url[256];
    int url_len = snprintf(url, sizeof(url), "%s?key=%s", AMAP_IP_LOCATION_URL, CONFIG_AMAP_API_KEY);
    if (url_len < 0 || (size_t)url_len >= sizeof(url)) {
        ESP_LOGE(TAG, "IP location request URL is too long");
        return ESP_ERR_INVALID_SIZE;
    }

    cJSON *json = NULL;
    esp_err_t err = request_json(url, "AMap IP location response", &json);
    if (err != ESP_OK) {
        return err;
    }

    err = require_amap_success(json, "IP location");
    if (err != ESP_OK) {
        goto cleanup;
    }

    const cJSON *adcode_item = cJSON_GetObjectItemCaseSensitive(json, "adcode");
    if (!cJSON_IsString(adcode_item) || adcode_item->valuestring[0] == '\0') {
        ESP_LOGE(TAG, "IP location response does not contain a usable adcode");
        err = ESP_FAIL;
        goto cleanup;
    }

    size_t adcode_len = strlen(adcode_item->valuestring);
    if (adcode_len >= adcode_size) {
        ESP_LOGE(TAG, "IP location adcode is too long");
        err = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }

    memcpy(adcode, adcode_item->valuestring, adcode_len + 1);

cleanup:
    cJSON_Delete(json);
    return err;
}

static esp_err_t query_live_weather(const char *adcode)
{
    char url[320];
    int url_len = snprintf(url, sizeof(url), "%s?key=%s&city=%s",
                           AMAP_WEATHER_URL, CONFIG_AMAP_API_KEY, adcode);
    if (url_len < 0 || (size_t)url_len >= sizeof(url)) {
        ESP_LOGE(TAG, "Weather request URL is too long");
        return ESP_ERR_INVALID_SIZE;
    }

    cJSON *json = NULL;
    esp_err_t err = request_json(url, "AMap live weather response", &json);
    if (err != ESP_OK) {
        return err;
    }

    err = require_amap_success(json, "Weather query");
    if (err != ESP_OK) {
        goto cleanup;
    }

    const cJSON *lives = cJSON_GetObjectItemCaseSensitive(json, "lives");
    if (!cJSON_IsArray(lives) || cJSON_GetArraySize(lives) == 0) {
        ESP_LOGE(TAG, "Weather response contains an empty lives array");
        err = ESP_FAIL;
    }

cleanup:
    cJSON_Delete(json);
    return err;
}

static void amap_weather_task(void *arg)
{
    (void)arg;

    if (CONFIG_AMAP_API_KEY[0] == '\0') {
        ESP_LOGE(TAG, "AMap API Key is not configured");
        vTaskDelete(NULL);
        return;
    }

    char adcode[ADCODE_BUFFER_SIZE] = DEFAULT_ADCODE;
    if (query_ip_location(adcode, sizeof(adcode)) != ESP_OK) {
        memcpy(adcode, DEFAULT_ADCODE, sizeof(DEFAULT_ADCODE));
        ESP_LOGW(TAG, "IP location is unavailable, using %s (adcode=%s)", DEFAULT_CITY_NAME, adcode);
    }

    if (query_live_weather(adcode) != ESP_OK) {
        ESP_LOGE(TAG, "AMap live weather query failed");
    } else {
        ESP_LOGI(TAG, "AMap live weather query completed");
    }

    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_log_level_set("wifi", ESP_LOG_ERROR);
    ESP_ERROR_CHECK(example_connect());

    BaseType_t task_created = xTaskCreate(amap_weather_task, "amap_weather", 8192, NULL, 5, NULL);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create AMap weather task");
    }
}
