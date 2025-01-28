#include "webui.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include <stddef.h>
#include <string.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX_URL_LENGTH 256

static const char *TAG = "WEB_UI";
char stream_url[MAX_URL_LENGTH] = "http://192.168.9.184:8001";

static esp_err_t authenticated_handler(httpd_req_t *req, esp_err_t (*handler)(httpd_req_t *));


/* HTML content for the admin panel */
static const char admin_panel_html[] = "<!DOCTYPE html>\n"
"<html>\n"
"<head>\n"
"    <title>ESP32 Admin Panel</title>\n"
"</head>\n"
"<body>\n"
"    <h1>ESP32 Admin Panel</h1>\n"
"    <form action='/set_url' method='post'>\n"
"        <label for='url'>Stream URL:</label><br>\n"
"        <input type='text' id='url' name='url' value='http://192.168.9.184:8001'><br><br>\n"
"        <input type='submit' value='Update URL'>\n"
"    </form>\n"
"    <br><br>\n"
"    <form action='/restart' method='get'>\n"
"        <input type='submit' value='Restart ESP32'>\n"
"    </form>\n"
"</body>\n"
"</html>";

/* Handler to serve the admin panel */
static esp_err_t admin_panel_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, admin_panel_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* Handler to set a new stream URL */
static esp_err_t set_stream_url_handler(httpd_req_t *req) {
    char content[MAX_URL_LENGTH] = {0};
    int ret = httpd_req_recv(req, content, MIN(req->content_len, sizeof(content) - 1));
    if (ret <= 0) {
        ESP_LOGE(TAG, "Failed to receive URL");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive URL");
        return ESP_FAIL;
    }

    content[ret] = '\0'; // Null-terminate received data
    ESP_LOGI(TAG, "Received content: %s", content);

    char *url_start = strstr(content, "url=");
    if (url_start) {
        url_start += 4; // Skip "url="
        snprintf(stream_url, sizeof(stream_url), "%s", url_start);
        ESP_LOGI(TAG, "New stream URL set: %s", stream_url);

        httpd_resp_sendstr(req, "Stream URL updated successfully. Restarting ESP32...");
        vTaskDelay(pdMS_TO_TICKS(100)); // Delay to ensure response is sent
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Invalid URL format");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid URL format");
        return ESP_FAIL;
    }

    return ESP_OK;
}

/* Handler to restart the ESP32 */
static esp_err_t restart_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Restarting ESP32...");
    httpd_resp_sendstr(req, "ESP32 is restarting...");
    vTaskDelay(pdMS_TO_TICKS(100)); // Delay to ensure response is sent
    esp_restart();
    return ESP_OK;
}

/* Basic HTTP authentication */
static esp_err_t basic_auth(httpd_req_t *req) {
    char auth_header[128] = {0};
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header)) != ESP_OK) {
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"ESP32 Admin\"");
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    const char *expected_auth = "Basic YWRtaW46YWRtaW4="; // Base64 encoded "admin:admin"
    if (strcmp(auth_header, expected_auth) != 0) {
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"ESP32 Admin\"");
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* Wrapper for the admin panel handler with authentication */
static esp_err_t admin_panel_authenticated_handler(httpd_req_t *req) {
    return authenticated_handler(req, admin_panel_handler);
}

/* Wrapper for the set stream URL handler with authentication */
static esp_err_t set_stream_url_authenticated_handler(httpd_req_t *req) {
    return authenticated_handler(req, set_stream_url_handler);
}

/* Wrapper for the restart handler with authentication */
static esp_err_t restart_authenticated_handler(httpd_req_t *req) {
    return authenticated_handler(req, restart_handler);
}

/* Authentication wrapper function */
static esp_err_t authenticated_handler(httpd_req_t *req, esp_err_t (*handler)(httpd_req_t *)) {
    if (basic_auth(req) == ESP_OK) {
        return handler(req);
    }
    return ESP_FAIL;
}

/* Start the HTTP server */
static httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        // URI handler to serve the admin panel
        httpd_uri_t admin_panel_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = admin_panel_authenticated_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &admin_panel_uri);

        // URI handler to set the stream URL
        httpd_uri_t set_url_uri = {
            .uri = "/set_url",
            .method = HTTP_POST,
            .handler = set_stream_url_authenticated_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &set_url_uri);

        // URI handler to restart the ESP32
        httpd_uri_t restart_uri = {
            .uri = "/restart",
            .method = HTTP_GET,
            .handler = restart_authenticated_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &restart_uri);

        return server;
    }

    ESP_LOGE(TAG, "Failed to start web server");
    return NULL;
}

void start_webui(void) {
    ESP_LOGI(TAG, "Starting web server...");
    start_webserver();
}
