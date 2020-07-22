/* ESP HTTP Client Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "app_wifi.h"


#include "Sd.h"
#include "Debug.h"
#include "defines.h"
#include "State.h"
#include "http_client.h"

#include "esp_http_client.h"

#define MAX_HTTP_RECV_BUFFER 512
extern char cConfigAndData[RX_BUF_SIZE];
extern unsigned long ulTimestamp;
/*extern unsigned char ucCurrentStateGsm;*/
sMessageType stHttpCliMsg;

static unsigned char ucCurrentStateHttpCli = TASKHTTPCLI_IDLING;
static const char *TAG = "HTTP_CLIENT";

/* Root cert for howsmyssl.com, taken from howsmyssl_com_root_cert.pem

   The PEM file was extracted from the output of this command:
   openssl s_client -showcerts -connect www.howsmyssl.com:443 </dev/null

   The CA root cert is the last cert given in the chain of certs.

   To embed it in the app binary, the PEM file is named
   in the component.mk COMPONENT_EMBED_TXTFILES variable.
*/
extern const char api_telegram_org_pem_start[] asm("_binary_api_telegram_org_pem_start");
extern const char api_telegra_org_pem_end[]   asm("_binary_api_telegram_org_pem_end");

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // Write out data
                // printf("%.*s", evt->data_len, (char*)evt->data);
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}

static void http_rest()
{
    esp_http_client_config_t config = {
        .url = "http://httpbin.org/get",
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // GET
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    // POST
    const char *post_data = "field1=value1&field2=value2";
    esp_http_client_set_url(client, "http://httpbin.org/post");
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    //PUT
    esp_http_client_set_url(client, "http://httpbin.org/put");
    esp_http_client_set_method(client, HTTP_METHOD_PUT);
    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP PUT Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP PUT request failed: %s", esp_err_to_name(err));
    }

    //PATCH
    esp_http_client_set_url(client, "http://httpbin.org/patch");
    esp_http_client_set_method(client, HTTP_METHOD_PATCH);
    esp_http_client_set_post_field(client, NULL, 0);
    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP PATCH Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP PATCH request failed: %s", esp_err_to_name(err));
    }

    //DELETE
    esp_http_client_set_url(client, "http://httpbin.org/delete");
    esp_http_client_set_method(client, HTTP_METHOD_DELETE);
    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP DELETE Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP DELETE request failed: %s", esp_err_to_name(err));
    }

    //HEAD
    esp_http_client_set_url(client, "http://httpbin.org/get");
    esp_http_client_set_method(client, HTTP_METHOD_HEAD);
    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP HEAD Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP HEAD request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

static void http_auth_basic()
{
    esp_http_client_config_t config = {
        .url = "http://user:passwd@httpbin.org/basic-auth/user/passwd",
        .event_handler = _http_event_handler,
        .auth_type = HTTP_AUTH_TYPE_BASIC,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP Basic Auth Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

static void http_auth_basic_redirect()
{
    esp_http_client_config_t config = {
        .url = "http://user:passwd@httpbin.org/basic-auth/user/passwd",
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP Basic Auth redirect Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

static void http_auth_digest()
{
    esp_http_client_config_t config = {
        .url = "http://user:passwd@httpbin.org/digest-auth/auth/user/passwd/MD5/never",
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP Digest Auth Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

static void https()
{
    esp_http_client_config_t config = {
        .url = "https://api.telegram.org/bot1318631102:AAHQZ0cHw_BEssmSUMpuVVePRZfp72AKgXA/sendMessage?chat_id=-408374433&text=nicole",
        .event_handler = _http_event_handler,
		.cert_pem = api_telegram_org_pem_start,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTPS Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

static void http_relative_redirect()
{
    esp_http_client_config_t config = {
        .url = "http://httpbin.org/relative-redirect/3",
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP Relative path redirect Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

static void http_absolute_redirect()
{
    esp_http_client_config_t config = {
        .url = "http://httpbin.org/absolute-redirect/3",
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP Absolute path redirect Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

static void http_redirect_to_https()
{
    esp_http_client_config_t config = {
        .url = "http://httpbin.org/redirect-to?url=https%3A%2F%2Fwww.howsmyssl.com",
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP redirect to HTTPS Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}


static void http_download_chunk()
{
    esp_http_client_config_t config = {
        .url = "http://httpbin.org/stream-bytes/8912",
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP chunk encoding Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

static void http_perform_as_stream_reader()
{
    char *buffer = malloc(MAX_HTTP_RECV_BUFFER + 1);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Cannot malloc http receive buffer");
        return;
    }
    esp_http_client_config_t config = {
        .url = "http://httpbin.org/get",
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err;
    if ((err = esp_http_client_open(client, 0)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        free(buffer);
        return;
    }
    int content_length =  esp_http_client_fetch_headers(client);
    int total_read_len = 0, read_len;
    if (total_read_len < content_length && content_length <= MAX_HTTP_RECV_BUFFER) {
        read_len = esp_http_client_read(client, buffer, content_length);
        if (read_len <= 0) {
            ESP_LOGE(TAG, "Error read data");
        }
        buffer[read_len] = 0;
        ESP_LOGD(TAG, "read_len = %d", read_len);
    }
    ESP_LOGI(TAG, "HTTP Stream reader Status = %d, content_length = %d",
                    esp_http_client_get_status_code(client),
                    esp_http_client_get_content_length(client));
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    free(buffer);
}


static unsigned char http_get_timestamp(void)
{
	static char cLocalBuffer[256];
	unsigned char boError = true;


    esp_http_client_config_t config = {
        .url = "http://gpslogger.esy.es/pages/upload/timestamp.php",
        .event_handler = _http_event_handler,
        .is_async = false,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err;

    esp_http_client_set_method(client, HTTP_METHOD_GET);
    /*esp_http_client_set_post_field(client, post_data, strlen(post_data));*/
    while (1) {
        err = esp_http_client_perform(client);
        if (err != ESP_ERR_HTTP_EAGAIN) {
            break;
        }
    }

    if (err == ESP_OK)
    {
        memset(cLocalBuffer,0,256);
		esp_http_client_read(client,cLocalBuffer,256);

        ESP_LOGI(TAG, "HTTPS Status = %d, content_length = %d, content = %s\r\n",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client),
				cLocalBuffer);

        char *ptr;
        ptr = strstr((const char*)&cLocalBuffer[0],(const char*)"Timestamp=");
        ESP_LOGI(TAG,"ptr=%s\r\n",ptr);

        if(ptr != NULL)
        {

			ptr +=strlen((const char*)("Timestamp="));
			ulTimestamp = atol(ptr);
			ESP_LOGI(TAG,"Timestamp=%ld\r\n",ulTimestamp);


            stHttpCliMsg.ucSrc = SRC_HTTPCLI;
            stHttpCliMsg.ucDest = SRC_SD;
            stHttpCliMsg.ucEvent = EVENT_SD_OPENING;
            xQueueSend( xQueueSd, ( void * )&stHttpCliMsg, 0);
        }
        else
        {
        	stHttpCliMsg.ucSrc = SRC_HTTPCLI;
        	stHttpCliMsg.ucDest = SRC_HTTPCLI;
        	stHttpCliMsg.ucEvent = EVENT_HTTPCLI_GET_TIMESTAMP;
            xQueueSend( xQueueHttpCli, ( void * )&stHttpCliMsg, 0);

            boError = false;
        }
    }
    else
    {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));

    	stHttpCliMsg.ucSrc = SRC_HTTPCLI;
    	stHttpCliMsg.ucDest = SRC_HTTPCLI;
    	stHttpCliMsg.ucEvent = EVENT_HTTPCLI_GET_TIMESTAMP;
        xQueueSend( xQueueHttpCli, ( void * )&stHttpCliMsg, 0);

        boError = false;
    }

    esp_http_client_cleanup(client);

    return(boError);
}

static unsigned char http_get_telegram(const char *post_data)
{
	static char cLocalBuffer[512];
	unsigned char boError = true;

    esp_http_client_config_t config = {
        .url = post_data,
        .event_handler = _http_event_handler,
        .is_async = true,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err;

    esp_http_client_set_method(client, HTTP_METHOD_GET);
    /*esp_http_client_set_post_field(client, post_data, strlen(post_data));*/
    while (1) {
        err = esp_http_client_perform(client);
        if (err != ESP_ERR_HTTP_EAGAIN) {
            break;
        }
    }

    if (err == ESP_OK)
    {
        memset(cLocalBuffer,0,512);
		esp_http_client_read(client,cLocalBuffer,512);

        ESP_LOGI(TAG, "HTTPS Status = %d, content_length = %d, content = %s\r\n",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client),
				cLocalBuffer);

        char *ptr = NULL;
        ptr = strstr((const char*)&cLocalBuffer[0],(const char*)"\"id\": -408374433");

        if(ptr != NULL)
        {
        	ESP_LOGI(TAG,"ptr=%s\r\n",ptr);

        	stHttpCliMsg.ucSrc = SRC_HTTPCLI;
        	stHttpCliMsg.ucDest = SRC_HTTPCLI;
        	stHttpCliMsg.ucEvent = EVENT_HTTPCLI_GET_TELEGRAM;
        	xQueueSend( xQueueHttpCli, ( void * )&stHttpCliMsg, 0);

        	boError = false;
        }
        else
        {
            stHttpCliMsg.ucSrc = SRC_HTTPCLI;
            stHttpCliMsg.ucDest = SRC_SD;
            stHttpCliMsg.ucEvent = EVENT_SD_MARKING;
        	xQueueSend( xQueueSd, ( void * )&stHttpCliMsg, 0);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));

    	stHttpCliMsg.ucSrc = SRC_HTTPCLI;
    	stHttpCliMsg.ucDest = SRC_HTTPCLI;
    	stHttpCliMsg.ucEvent = EVENT_HTTPCLI_GET_TELEGRAM;
    	xQueueSend( xQueueHttpCli, ( void * )&stHttpCliMsg, 0);

    	boError = false;
    }
    esp_http_client_cleanup(client);

    return(boError);
}


static unsigned char http_post(const char *post_data)
{
	unsigned char boError = true;

	static char cLocalBuffer[128];

    esp_http_client_config_t config = {
        .url = "http://gpslogger.esy.es/pages/upload/rio.php",
        .event_handler = _http_event_handler,
        .is_async = false,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err;

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    while (1) {
        err = esp_http_client_perform(client);
        if (err != ESP_ERR_HTTP_EAGAIN) {
            break;
        }
    }

    if (err == ESP_OK)
    {
        memset(cLocalBuffer,0,128);
		esp_http_client_read(client,cLocalBuffer,128);

        ESP_LOGI(TAG, "HTTPS Status = %d, content_length = %d, content = %s\r\n",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client),
				cLocalBuffer);

        char *ptr;
        ptr = strstr((const char*)&cLocalBuffer[0],(const char*)"ReGcOr");
        ESP_LOGI(TAG,"ptr=%s\r\n",ptr);

        if(ptr != NULL)
        {
        	stHttpCliMsg.ucSrc = SRC_HTTPCLI;
        	stHttpCliMsg.ucDest = SRC_HTTPCLI;
        	stHttpCliMsg.ucEvent = EVENT_HTTPCLI_GET_TELEGRAM;
        	xQueueSend( xQueueHttpCli, ( void * )&stHttpCliMsg, 0);
        }
        else
        {
        	stHttpCliMsg.ucSrc = SRC_HTTPCLI;
        	stHttpCliMsg.ucDest = SRC_HTTPCLI;
        	stHttpCliMsg.ucEvent = EVENT_HTTPCLI_POST;
            xQueueSend( xQueueHttpCli, ( void * )&stHttpCliMsg, 0);

            boError = false;
        }
    }
    else
    {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));

    	stHttpCliMsg.ucSrc = SRC_HTTPCLI;
    	stHttpCliMsg.ucDest = SRC_HTTPCLI;
    	stHttpCliMsg.ucEvent = EVENT_HTTPCLI_POST;
        xQueueSend( xQueueHttpCli, ( void * )&stHttpCliMsg, 0);

        boError = false;

    }
    esp_http_client_cleanup(client);

    return(boError);
}
//////////////////////////////////////////////
//
//
//              TaskHttpCli_Init
//
//
//////////////////////////////////////////////
unsigned char TaskHttpCli_Init(sMessageType *psMessage)
{
    unsigned char boError = true;

    ESP_LOGI(TAG, "<<<<HTTPCLI INIT>>>>\r\n");

	stHttpCliMsg.ucSrc = SRC_HTTPCLI;
	stHttpCliMsg.ucDest = SRC_HTTPCLI;
	stHttpCliMsg.ucEvent = EVENT_HTTPCLI_CONNECTING;
	xQueueSend( xQueueHttpCli, ( void * )&stHttpCliMsg, 0);

    return(boError);
}


//////////////////////////////////////////////
//
//
//              TaskHttpCli_Connecting
//
//
//////////////////////////////////////////////
unsigned char TaskHttpCli_Connecting(sMessageType *psMessage)
{
    unsigned char boError = true;

    ESP_LOGI(TAG, "<<<<HTTPCLI CONNECTING>>>>\r\n");

    esp_wifi_connect();
    /*app_wifi_wait_connected();*/

	stHttpCliMsg.ucSrc = SRC_HTTPCLI;
	stHttpCliMsg.ucDest = SRC_DEBUG;
	stHttpCliMsg.ucEvent = EVENT_IO_GSM_CONNECTING;
	xQueueSend( xQueueDebug, ( void * )&stHttpCliMsg, 0);


    return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskHttpCli_Connected
//
//
//////////////////////////////////////////////
unsigned char TaskHttpCli_Connected(sMessageType *psMessage)
{
    unsigned char boError = true;

    ESP_LOGI(TAG, "<<<<HTTPCLI CONNECTED>>>>\r\n");

    /*stHttpCliMsg.ucSrc = SRC_HTTPCLI;
    stHttpCliMsg.ucDest = SRC_SD;
    stHttpCliMsg.ucEvent = EVENT_SD_OPENING;
    xQueueSend( xQueueSd, ( void * )&stHttpCliMsg, 0);*/

    stHttpCliMsg.ucSrc = SRC_HTTPCLI;
    stHttpCliMsg.ucDest = SRC_HTTPCLI;
    stHttpCliMsg.ucEvent = EVENT_HTTPCLI_GET_TIMESTAMP;
    xQueueSend( xQueueHttpCli, ( void * )&stHttpCliMsg, 0);
    return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskHttpCli_GetTimestamp
//
//
//////////////////////////////////////////////
unsigned char TaskHttpCli_GetTimestamp(sMessageType *psMessage)
{
    unsigned char boError = true;

    ESP_LOGI(TAG, "<<<<HTTPCLI GET TIMESTAMP>>>>\r\n");

    boError = http_get_timestamp();

	stHttpCliMsg.ucSrc = SRC_HTTPCLI;
	stHttpCliMsg.ucDest = SRC_DEBUG;
	stHttpCliMsg.ucEvent = EVENT_IO_GSM_COMMUNICATING;
	xQueueSend( xQueueDebug, ( void * )&stHttpCliMsg, 0);

    return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskHttpCli_Post
//
//
//////////////////////////////////////////////
unsigned char TaskHttpCli_Post(sMessageType *psMessage)
{
    unsigned char boError = true;

	char* cLocalBuffer = (char*) malloc(2048);
    memset(cLocalBuffer,0,2048);

    ESP_LOGI(TAG, "<<<<HTTPCLI POSTING>>>>\r\n");
    ESP_LOGI(TAG, "%s\r\n",cConfigAndData);

    sprintf(cLocalBuffer,"%s",cConfigAndData);

    boError = http_post(cLocalBuffer);

	stHttpCliMsg.ucSrc = SRC_HTTPCLI;
	stHttpCliMsg.ucDest = SRC_DEBUG;
	stHttpCliMsg.ucEvent = EVENT_IO_GSM_COMMUNICATING;
	xQueueSend( xQueueDebug, ( void * )&stHttpCliMsg, 0);

	free(cLocalBuffer);
    return(boError);
}


//////////////////////////////////////////////
//
//
//              TaskHttpCli_GetTelegram
//
//
//////////////////////////////////////////////
unsigned char TaskHttpCli_GetTelegram(sMessageType *psMessage)
{
    unsigned char boError = true;

	char* cLocalBuffer = (char*) malloc(256);
    memset(cLocalBuffer,0,256);

	char* cTempBuffer = (char*) malloc(256);
    memset(cTempBuffer,0,256);

    ESP_LOGI(TAG, "<<<<HTTPCLI GET TELEGRAM>>>>\r\n");
    ESP_LOGI(TAG, "%s\r\n",cConfigAndData);

    strncpy(cTempBuffer,cConfigAndData,strlen(cConfigAndData));
    char *ptr = NULL;

    ptr = strtok(cTempBuffer,",");
    if(ptr !=NULL)
    {
    	ptr = strtok (NULL, ",");
    }

    ESP_LOGI(TAG, "%s\r\n",ptr);

    if(strcmp(ptr,"SENSOR1") == 0)
    {
    	sprintf(cLocalBuffer,"https://api.telegram.org/bot1318631102:AAHQZ0cHw_BEssmSUMpuVVePRZfp72AKgXA/sendMessage?chat_id=-408374433&text=%s","JANELA_FRENTE");
    }
    if(strcmp(ptr,"SENSOR2") == 0)
    {
    	sprintf(cLocalBuffer,"https://api.telegram.org/bot1318631102:AAHQZ0cHw_BEssmSUMpuVVePRZfp72AKgXA/sendMessage?chat_id=-408374433&text=%s","PORTA_FRENTE");
    }
    if(strcmp(ptr,"SENSOR3") == 0)
    {
    	sprintf(cLocalBuffer,"https://api.telegram.org/bot1318631102:AAHQZ0cHw_BEssmSUMpuVVePRZfp72AKgXA/sendMessage?chat_id=-408374433&text=%s","PORTA_COZINHA");
    }
    if(strcmp(ptr,"SENSOR5") == 0)
    {
    	sprintf(cLocalBuffer,"https://api.telegram.org/bot1318631102:AAHQZ0cHw_BEssmSUMpuVVePRZfp72AKgXA/sendMessage?chat_id=-408374433&text=%s","PORTAOZINHO");
    }

    boError = http_get_telegram(cLocalBuffer);

	stHttpCliMsg.ucSrc = SRC_HTTPCLI;
	stHttpCliMsg.ucDest = SRC_DEBUG;
	stHttpCliMsg.ucEvent = EVENT_IO_GSM_COMMUNICATING;
	xQueueSend( xQueueDebug, ( void * )&stHttpCliMsg, 0);

	free(cLocalBuffer);
	free(cTempBuffer);
    return(boError);
}


//////////////////////////////////////////////
//
//
//              TaskHttpCli_Posted
//
//
//////////////////////////////////////////////
unsigned char TaskHttpCli_Posted(sMessageType *psMessage)
{
    unsigned char boError = true;

    ESP_LOGE(TAG, "<<<<HTTP POSTED>>>>\r\n");

	stHttpCliMsg.ucSrc = SRC_HTTPCLI;
	stHttpCliMsg.ucDest = SRC_SD;
	stHttpCliMsg.ucEvent = EVENT_SD_MARKING;
	xQueueSend( xQueueSd, ( void * )&stHttpCliMsg, 0);

	stHttpCliMsg.ucSrc = SRC_HTTPCLI;
	stHttpCliMsg.ucDest = SRC_DEBUG;
	stHttpCliMsg.ucEvent = EVENT_IO_GSM_UPLOAD_DONE;
	xQueueSend( xQueueDebug, ( void * )&stHttpCliMsg, 0);

    return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskHttpCli_Disconnected
//
//
//////////////////////////////////////////////
unsigned char TaskHttpCli_Disconnected(sMessageType *psMessage)
{
    unsigned char boError = true;

    ESP_LOGI(TAG, "<<<<HTTP DISCONNECTED>>>>\r\n");

	stHttpCliMsg.ucSrc = SRC_HTTPCLI;
	stHttpCliMsg.ucDest = SRC_HTTPCLI;
	stHttpCliMsg.ucEvent = EVENT_HTTPCLI_INIT;
	xQueueSend( xQueueHttpCli, ( void * )&stHttpCliMsg, 0);

    return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskHttpCli_Ending
//
//
//////////////////////////////////////////////
unsigned char TaskHttpCli_Ending(sMessageType *psMessage)
{
	unsigned char boError = true;

    ESP_LOGI(TAG, "<<<<HTTPCLI:>ENDING>>>>\r\n");

	vTaskDelay(2000/portTICK_PERIOD_MS);

	stHttpCliMsg.ucSrc = SRC_HTTPCLI;
	stHttpCliMsg.ucDest = SRC_SD;
	stHttpCliMsg.ucEvent = EVENT_SD_OPENING;
	xQueueSend( xQueueSd, ( void * )&stHttpCliMsg, 0);

	return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskHttpCli_IgnoreEvent
//
//
//////////////////////////////////////////////
unsigned char TaskHttpCli_IgnoreEvent(sMessageType *psMessage)
{
    unsigned char boError = false;
    return(boError);
}

//////////////////////////////////////////////
//
//
//             Http State Machine
//
//
//////////////////////////////////////////////
static sStateMachineType const gasTaskHttpCli_Idling[] =
{
    /* Event        Action routine      Next state */
    //  State specific transitions
	{EVENT_HTTPCLI_INIT,      		TaskHttpCli_Init, 	            TASKHTTPCLI_INITIALIZING,        			TASKHTTPCLI_IDLING						},
	{EVENT_HTTPCLI_DISCONNECTED,  	TaskHttpCli_Disconnected, 	    TASKHTTPCLI_IDLING,           				TASKHTTPCLI_IDLING        				},
    {EVENT_HTTPCLI_NULL,      		TaskHttpCli_IgnoreEvent,        TASKHTTPCLI_IDLING,							TASKHTTPCLI_IDLING						}
};

static sStateMachineType const gasTaskHttpCli_Initializing[] =
{
    /* Event        Action routine      Next state */
    //  State specific transitions
	{EVENT_HTTPCLI_CONNECTING,     	TaskHttpCli_Connecting, 	    TASKHTTPCLI_INITIALIZING,           		TASKHTTPCLI_INITIALIZING        		},
	{EVENT_HTTPCLI_CONNECTED,  		TaskHttpCli_Connected, 	        TASKHTTPCLI_SYNCING,           				TASKHTTPCLI_INITIALIZING        		},
	{EVENT_HTTPCLI_DISCONNECTED,  	TaskHttpCli_Disconnected, 	    TASKHTTPCLI_IDLING,           				TASKHTTPCLI_IDLING        				},
    {EVENT_HTTPCLI_NULL,      		TaskHttpCli_IgnoreEvent,        TASKHTTPCLI_INITIALIZING,					TASKHTTPCLI_INITIALIZING				}
};

static sStateMachineType const gasTaskHttpCli_Syncing[] =
{
    /* Event        Action routine      Next state */
    //  State specific transitions
	{EVENT_HTTPCLI_GET_TIMESTAMP,	TaskHttpCli_GetTimestamp, 	    TASKHTTPCLI_COMMUNICATING,           		TASKHTTPCLI_SYNCING        		},
    {EVENT_HTTPCLI_NULL,      		TaskHttpCli_IgnoreEvent,        TASKHTTPCLI_SYNCING,						TASKHTTPCLI_SYNCING		}
};

static sStateMachineType const gasTaskHttpCli_Communicating[] =
{
    /* Event        Action routine      Next state */
    //  State specific transitions

	{EVENT_HTTPCLI_POST,      		TaskHttpCli_Post, 	            TASKHTTPCLI_COMMUNICATING,           		TASKHTTPCLI_COMMUNICATING        				},
	{EVENT_HTTPCLI_GET_TELEGRAM,    TaskHttpCli_GetTelegram,        TASKHTTPCLI_COMMUNICATING,           		TASKHTTPCLI_COMMUNICATING        				},
	{EVENT_HTTPCLI_POSTED,     		TaskHttpCli_Posted, 	        TASKHTTPCLI_COMMUNICATING,           		TASKHTTPCLI_COMMUNICATING        				},
	{EVENT_HTTPCLI_DISCONNECTED,  	TaskHttpCli_Disconnected, 	    TASKHTTPCLI_IDLING,           				TASKHTTPCLI_IDLING        				        },
	{EVENT_HTTPCLI_ENDING,  		TaskHttpCli_Ending, 	    	TASKHTTPCLI_COMMUNICATING,           		TASKHTTPCLI_COMMUNICATING        				},

    {EVENT_HTTPCLI_NULL,      		TaskHttpCli_IgnoreEvent,        TASKHTTPCLI_COMMUNICATING,					TASKHTTPCLI_COMMUNICATING						}
};

static sStateMachineType const * const gpasTaskHttpCli_StateMachine[] =
{
	gasTaskHttpCli_Idling,
	gasTaskHttpCli_Initializing,
	gasTaskHttpCli_Syncing,
	gasTaskHttpCli_Communicating
};


static void http_task(void *pvParameters)
{
	TickType_t elapsed_time;
	uint32_t EventBits_t;

	for( ;; )
	{
		elapsed_time = xTaskGetTickCount();

		EventBits_t = app_wifi_get_status();
		if(EventBits_t & 0x00000001)
		{
				/*CONNECTED*/
		    	ESP_LOGI(TAG, "HTTP CONN!\r\n");
				app_wifi_clear_connected_bit();

				stHttpCliMsg.ucSrc = SRC_HTTPCLI;
				stHttpCliMsg.ucDest = SRC_HTTPCLI;
				stHttpCliMsg.ucEvent = EVENT_HTTPCLI_CONNECTED;
				xQueueSend( xQueueHttpCli, ( void * )&stHttpCliMsg, 0);
		}
		else
		{
			if(EventBits_t & 0x00000002)
			{
				/*DISCONNECTED*/
			    ESP_LOGI(TAG, "HTTP DISCON\r\n");
				app_wifi_clear_disconnected_bit();

				stHttpCliMsg.ucSrc = SRC_HTTPCLI;
				stHttpCliMsg.ucDest = SRC_HTTPCLI;
				stHttpCliMsg.ucEvent = EVENT_HTTPCLI_DISCONNECTED;
				xQueueSend( xQueueHttpCli, ( void * )&stHttpCliMsg, 0);
			}
		}

		if( xQueueReceive( xQueueHttpCli, &(stHttpCliMsg ),0 ))
		{
			(void)eEventHandler ((unsigned char)SRC_HTTPCLI,gpasTaskHttpCli_StateMachine[ucCurrentStateHttpCli], &ucCurrentStateHttpCli, &stHttpCliMsg);
		}

		vTaskDelayUntil(&elapsed_time, 1000 / portTICK_PERIOD_MS);
	}
}

void Http_Init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

	xQueueHttpCli = xQueueCreate(httcliQUEUE_LENGTH,			/* The number of items the queue can hold. */
								sizeof( sMessageType ) );	/* The size of each item the queue holds. */

	stHttpCliMsg.ucSrc = SRC_HTTPCLI;
	stHttpCliMsg.ucDest = SRC_HTTPCLI;
	stHttpCliMsg.ucEvent = EVENT_HTTPCLI_INIT;
	xQueueSend( xQueueHttpCli, ( void * )&stHttpCliMsg, 0);

    app_wifi_initialise();

    xTaskCreate(&http_task, "http_task", 4096, NULL, configMAX_PRIORITIES-5, NULL);

}


