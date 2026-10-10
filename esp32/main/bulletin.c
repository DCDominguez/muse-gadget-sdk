/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "bulletin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

static const char *TAG = "bulletin";

// The notes, null-terminated (EMBED_TXTFILES in CMakeLists.txt).
extern const char bulletin_text[] asm("_binary_bulletin_md_start");

#define NVS_NS "bulletin"
#define NVS_KEY "ideas"
#define IDEAS_MAX 20
#define IDEAS_BYTES 3500    // the JSON kept in NVS

static SemaphoreHandle_t s_lock;
static portMUX_TYPE s_init = portMUX_INITIALIZER_UNLOCKED;

static void lock(void) {
    if (!s_lock) {
        SemaphoreHandle_t made = xSemaphoreCreateMutex();
        taskENTER_CRITICAL(&s_init);
        if (!s_lock) {
            s_lock = made;
            made = NULL;
        }
        taskEXIT_CRITICAL(&s_init);
        if (made) {
            vSemaphoreDelete(made);
        }
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void) {
    xSemaphoreGive(s_lock);
}

// The pinned ideas, an array (empty when there are none or NVS can't say).
static cJSON *load_ideas(void) {
    cJSON *ideas = NULL;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len = 0;
        if (nvs_get_str(h, NVS_KEY, NULL, &len) == ESP_OK && len > 1) {
            char *json = malloc(len);
            if (json && nvs_get_str(h, NVS_KEY, json, &len) == ESP_OK) {
                ideas = cJSON_Parse(json);
            }
            free(json);
        }
        nvs_close(h);
    }
    if (!cJSON_IsArray(ideas)) {
        cJSON_Delete(ideas);
        ideas = cJSON_CreateArray();
    }
    return ideas;
}

// Saves them, the oldest dropped until they fit. NULL ideas clears the board.
static esp_err_t save_ideas(cJSON *ideas) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    if (!ideas) {
        err = nvs_erase_key(h, NVS_KEY);
        err = err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
    } else {
        char *json = NULL;
        while (true) {
            json = cJSON_PrintUnformatted(ideas);
            if (!json || strlen(json) < IDEAS_BYTES || cJSON_GetArraySize(ideas) <= 1) {
                break;
            }
            cJSON_free(json);
            json = NULL;
            cJSON_DeleteItemFromArray(ideas, 0);
        }
        err = json ? nvs_set_str(h, NVS_KEY, json) : ESP_ERR_NO_MEM;
        cJSON_free(json);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

cJSON *bulletin_command(void) {
    cJSON *result = cJSON_CreateObject();
    cJSON *payload = cJSON_CreateObject();
    if (!result || !payload || !cJSON_AddBoolToObject(result, "ok", true)
        || !cJSON_AddStringToObject(payload, "text", bulletin_text)
        || !cJSON_AddStringToObject(payload, "firmware", esp_app_get_description()->version)) {
        cJSON_Delete(result);
        cJSON_Delete(payload);
        return NULL;
    }
    lock();
    cJSON_AddItemToObject(payload, "ideas", load_ideas());
    unlock();
    cJSON_AddItemToObject(result, "payload", payload);
    return result;
}

static cJSON *post_error(const char *code, const char *message) {
    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", false);
    cJSON *error = cJSON_AddObjectToObject(result, "error");
    cJSON_AddStringToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", message);
    return result;
}

cJSON *bulletin_post_command(const cJSON *params) {
    const char *idea = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(params, "idea"));
    size_t len = idea ? strlen(idea) : 0;
    while (len && (idea[0] == ' ' || idea[0] == '\n')) {
        idea++;
        len--;
    }
    if (!len) {
        return post_error("invalid_params", "idea: the text to pin, required");
    }
    if (len > BULLETIN_IDEA_MAX) {
        return post_error("invalid_params", "idea: 600 bytes at most; make it shorter");
    }
    cJSON *entry = cJSON_CreateObject();
    cJSON_AddStringToObject(entry, "idea", idea);
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    if (tm.tm_year + 1900 >= 2025) {   // the clock is set (SNTP)
        char at[24];
        strftime(at, sizeof(at), "%Y-%m-%d %H:%M", &tm);
        cJSON_AddStringToObject(entry, "at", at);
    } else {
        cJSON_AddNullToObject(entry, "at");
    }

    lock();
    cJSON *ideas = load_ideas();
    cJSON_AddItemToArray(ideas, entry);
    while (cJSON_GetArraySize(ideas) > IDEAS_MAX) {
        cJSON_DeleteItemFromArray(ideas, 0);
    }
    esp_err_t err = save_ideas(ideas);
    int pinned = cJSON_GetArraySize(ideas);
    cJSON_Delete(ideas);
    unlock();

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "couldn't save an idea: %s", esp_err_to_name(err));
        return post_error("storage_failed", "couldn't save the idea");
    }
    ESP_LOGI(TAG, "idea pinned (%d on the board)", pinned);
    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", true);
    cJSON *payload = cJSON_AddObjectToObject(result, "payload");
    cJSON_AddNumberToObject(payload, "pinned", pinned);
    return result;
}

void bulletin_console_ideas(bool clear) {
    lock();
    cJSON *ideas = load_ideas();
    if (clear) {
        save_ideas(NULL);
    }
    unlock();
    char *json = cJSON_PrintUnformatted(ideas);
    printf("@ideas {\"cleared\":%s,\"ideas\":%s}\n", clear ? "true" : "false", json ? json : "[]");
    fflush(stdout);
    cJSON_free(json);
    cJSON_Delete(ideas);
}
