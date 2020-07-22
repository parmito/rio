/*
 * Gsm.c
 *
 *  Created on: 24/09/2018
 *      Author: danilo
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Kernel includes. */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_system.h"

#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"

#include <esp_system.h>
#include <esp_spi_flash.h>
#include <rom/spi_flash.h>
#include "driver/gpio.h"

#include "UartWifi.h"
#include "State.h"
#include "defines.h"
#include "Sd.h"
#include "Debug.h"
#include "Wifi.h"
#include "http_client.h"


//////////////////////////////////////////////
//
//
//            FUNCTION PROTOTYPES
//
//
//////////////////////////////////////////////
void vTaskWifi( void *pvParameters );

//////////////////////////////////////////////
//
//
//            	VARIABLES
//
//
//////////////////////////////////////////////
char cWifiRxBuffer[RX_BUF_SIZE];
char *ptrRxWifi;
extern char cConfigAndData[RX_BUF_SIZE];

char cExpectedResp1Wifi[RX_BUF_SIZE_REDUCED];
char cExpectedResp2Wifi[RX_BUF_SIZE_REDUCED];

unsigned char ucWifiAttempt = MODEM_ATTEMPT;

extern char cWifiApName[RX_BUF_SIZE_REDUCED];
extern char cWifiApPassword[RX_BUF_SIZE_REDUCED];
extern char cHttpDns[RX_BUF_SIZE_REDUCED];
extern char cWebPage1[RX_BUF_SIZE_REDUCED];
extern char cWebPage2[RX_BUF_SIZE_REDUCED];

extern const char cModem_OK[];
extern const char cModem_ERROR[];
extern const char cModem_ACK[];
extern const char cCREG1[];
extern const char cCREG2[];
extern const char cCSQ1[];
extern const char cCSQ2[];
extern const char cCPIN1[];
extern const char cCIICR1[];
extern const char cCGML[];
extern const char cHttpAtStartCIP[];
extern const char cHttpAtEndCIP[];
extern const char cHttpHeader1[];
extern const char cHttpHeader2[];
extern const char cHttpHeader3[];
extern const char cHttpHeader4[];
extern const char cHttpContent[];

extern const char cTEL1[];
extern const char cTEL2[];
extern const char cTEL3[];
extern const char cRESET[];
extern const char cALARM[];

extern const char cSET[];
extern const char cGET[];
extern const char cGETCSQ[];
extern const char cESN[];
extern const char cGETCCID[];

extern const char cSEND[];
extern const char cCIPSEND[];

static const char *WIFI_TASK_TAG = "WIFI_TASK";

sMessageType stWifiMsg;
static unsigned char ucWifiTry = 0;
extern tstIo stIo;
extern unsigned long ulTimestamp;

typedef struct
{
	/* Command : GET or SET  */
	unsigned char ucReponseCode;
	/* action routine invoked for the event */
	const char *cWifiResponse;
}tstWifiTableResponse;

static tstWifiTableResponse const gasTableWifiResponse[] =
{
		{	1,		"OK"				},
		{	2,		"ERROR"				},
		{  255,    "END OF ARRAY"		}
};

//////////////////////////////////////////////
//
//
//              Wifi_Io_Configuration
//
//
//////////////////////////////////////////////
void Wifi_Io_Configuration(void)
{
#if 0
	gpio_config_t io_conf;
	//disable interrupt
	io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
	//set as output mode
	io_conf.mode = GPIO_MODE_OUTPUT;
	//bit mask of the pins that you want to set,e.g.GPIO18/19
	io_conf.pin_bit_mask = GPIO_OUTPUT_WIFI_ENABLE_PIN_SEL ;
	//disable pull-down mode
	io_conf.pull_down_en = 0;
	//disable pull-up mode
	io_conf.pull_up_en = 0;
	//configure GPIO with the given settings
	gpio_config(&io_conf);
#endif

}


//////////////////////////////////////////////
//
//
//              WifiInit
//
//
//////////////////////////////////////////////
void WifiInit(void)
{
	/*esp_log_level_set(WIFI_TASK_TAG, ESP_LOG_INFO);*/
#if DEBUG_WIFI
    ESP_LOGI(WIFI_TASK_TAG, "WIFI INIT");
#endif
	Wifi_Io_Configuration();

	xQueueWifi = xQueueCreate(wifiQUEUE_LENGTH,			/* The number of items the queue can hold. */
								sizeof( sMessageType ) );	/* The size of each item the queue holds. */


    xTaskCreate(vTaskWifi, "vTaskWifi", 1024*4, NULL, configMAX_PRIORITIES-3, NULL);
	/* Create the queue used by the queue send and queue receive tasks.
	http://www.freertos.org/a00116.html */


	stWifiMsg.ucSrc = SRC_WIFI;
	stWifiMsg.ucDest = SRC_WIFI;
	stWifiMsg.ucEvent = EVENT_WIFI_INIT;
	xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);
}

//////////////////////////////////////////////
//
//
//              TaskWifi_SendAtCmd1
//
//
//////////////////////////////////////////////
void TaskWifi_SendAtCmd1(const char *pcDebug,const char *pcAtCmd)
{
#if DEBUG_WIFI
    ESP_LOGI(WIFI_TASK_TAG, "\r\n<<<<SEND WIFI 1>>>>\r\n%s\r\n%s\r\n<<<<>>>>\r\n",pcDebug,pcAtCmd);
#endif
    memset(&cWifiRxBuffer,0x00,RX_BUF_SIZE);
    ptrRxWifi = &cWifiRxBuffer[0];

    UartWifiSendData(WIFI_TASK_TAG,pcAtCmd);
}
//////////////////////////////////////////////
//
//
//              TaskWifi_SendAtCmd
//
//
//////////////////////////////////////////////
void TaskWifi_SendAtCmd(const char *pcDebug,const char *pcAtCmd,\
const char *pcSuccessResp, const char* pcFailedResp)
{
#if DEBUG_WIFI
    ESP_LOGI(WIFI_TASK_TAG, "\r\n<<<<SEND WIFI>>>>\r\n%s\r\n%s\r\n<<<<>>>>\r\n",pcDebug,pcAtCmd);
#endif
    memset(&cExpectedResp1Wifi,0x00,RX_BUF_SIZE_REDUCED);
    strcpy(&cExpectedResp1Wifi[0],pcSuccessResp);

    memset(&cExpectedResp2Wifi,0x00,RX_BUF_SIZE_REDUCED);
    strcpy(&cExpectedResp2Wifi[0],pcFailedResp);

    memset(&cWifiRxBuffer,0x00,RX_BUF_SIZE);
    ptrRxWifi = &cWifiRxBuffer[0];

    UartWifiSendData(WIFI_TASK_TAG,pcAtCmd);
}

//////////////////////////////////////////////
//
//
//              TaskWifi_ParseResp1
//
//
//////////////////////////////////////////////
unsigned char TaskWifi_ParseResp1(tstWifiTableResponse const *pstWifiTableResponse)
{
    unsigned char ucResp = 0;
    const char *ptr=NULL;

    tstWifiTableResponse const *pst = pstWifiTableResponse;

    while(pst->ucReponseCode != 255){

    	ptr = strstr((const char*)&cWifiRxBuffer[0],pst->cWifiResponse);
        if(ptr != NULL)
        {
        	ucResp = pst->ucReponseCode;
#if DEBUG_WIFI
        	ESP_LOGI(WIFI_TASK_TAG, "\r\n<<<<PARSE1 RESP:%d WIFI>>>>\r\n%s\r\n<<<<>>>>\r\n",ucResp,cWifiRxBuffer/*pst->cWifiResponse*/);
#endif
        	break;
        }
        pst++;
    }

    return(ucResp);
}

//////////////////////////////////////////////
//
//
//              TaskWifi_ParseResp
//
//
//////////////////////////////////////////////
unsigned char TaskWifi_ParseResp(void)
{
    unsigned char ucResp = 0;
    const char *ptr=NULL;

    ptr = strstr((const char*)&cWifiRxBuffer[0],(const char*)&cExpectedResp1Wifi[0]);
    if(ptr != NULL)
    {
        ucResp = 1;
#if DEBUG_WIFI
        ESP_LOGI(WIFI_TASK_TAG, "\r\n<<<<RESP:1 WIFI>>>>\r\n%s\r\n<<<<>>>>\r\n",cWifiRxBuffer);
#endif
    }
    else
    {
        ptr = strstr((const char*)&cWifiRxBuffer[0],(const char*)&cExpectedResp2Wifi[0]);
        if(ptr != NULL)
        {
            ucResp = 2;
#if DEBUG_WIFI
            ESP_LOGI(WIFI_TASK_TAG, "\r\n<<<<RESP:2 WIFI>>>>\r\n%s\r\n<<<<>>>>\r\n",cWifiRxBuffer);
#endif
        }
        else
        {
#if DEBUG_WIFI
       		ESP_LOGI(WIFI_TASK_TAG, "\r\n<<<<RESP:X WIFI>>>>\r\n%s\r\n<<<<>>>>\r\n",cWifiRxBuffer);
#endif
        }
    }

    return(ucResp);
}

//////////////////////////////////////////////
//
//
//        TaskWifi_WaitResponse1
//
//
//////////////////////////////////////////////
unsigned char TaskWifi_WaitResponse1(const tstWifiTableResponse *pstWifiTableResponse, unsigned char (*ucFunc)(const tstWifiTableResponse *),unsigned char ucPeriod, unsigned char ucTimeout)
{
    unsigned char ucResp = 0;

    while((ucResp == 0) && (ucTimeout > 0))
    {
        vTaskDelay((ucPeriod*500)/portTICK_PERIOD_MS);
        ucResp = (*ucFunc)(pstWifiTableResponse);
        if(ucTimeout > 0 ) ucTimeout--;
    }

    return(ucResp);
}

//////////////////////////////////////////////
//
//
//        TaskWifi_WaitResponse
//
//
//////////////////////////////////////////////
unsigned char TaskWifi_WaitResponse(unsigned char (*ucFunc)(void),unsigned char ucPeriod, unsigned char ucTimeout)
{
    unsigned char ucResp = 0;

    while((ucResp == 0) && (ucTimeout > 0))
    {
        vTaskDelay((ucPeriod*500)/portTICK_PERIOD_MS);
        ucResp = ucFunc();
        if(ucTimeout > 0 ) ucTimeout--;
    }

    return(ucResp);
}


//////////////////////////////////////////////
//
//
//              TaskWifi_Init
//
//
//////////////////////////////////////////////
unsigned char TaskWifi_Init(sMessageType *psMessage)
{
    unsigned char boError = true;
#if DEBUG_WIFI
    ESP_LOGI(WIFI_TASK_TAG, "\r\n WIFI INIT\r\n");
#endif
	stWifiMsg.ucSrc = SRC_WIFI;
	stWifiMsg.ucDest = SRC_WIFI;
	stWifiMsg.ucEvent = EVENT_WIFI_SETBAUD;
	xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);

    return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskWifi_SetBaudRate
//
//
//////////////////////////////////////////////
unsigned char TaskWifi_SetBaudRate(sMessageType *psMessage)
{
	unsigned char boError = false;
	unsigned char ucResp;

	TaskWifi_SendAtCmd1("WF:>BAUD\r\n","AT\r\n");

	ucResp = TaskWifi_WaitResponse1(&gasTableWifiResponse[0],TaskWifi_ParseResp1,1,20);

	switch(ucResp)
	{
		case 0:
		case 2:
			stWifiMsg.ucSrc = SRC_WIFI;
			stWifiMsg.ucDest = SRC_WIFI;
			stWifiMsg.ucEvent = EVENT_WIFI_SETBAUD;
			xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);
			boError = false;
		break;

		case 1:
			stWifiMsg.ucSrc = SRC_WIFI;
			stWifiMsg.ucDest = SRC_WIFI;
			stWifiMsg.ucEvent = EVENT_WIFI_SEND_RESET;
			xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);
			boError = true;
		break;

		default:
		break;
	}

	return(boError);

}

//////////////////////////////////////////////
//
//
//              TaskWifi_SendReset
//
//
//////////////////////////////////////////////
unsigned char TaskWifi_SendReset(sMessageType *psMessage)
{
    unsigned char boError = false;
    unsigned char ucResp;
    /*TaskWifi_SendAtCmd("WF:>AT\r\n","AT+RST\r\n","ready",cModem_ERROR);


    ucResp = TaskWifi_WaitResponse(TaskWifi_ParseResp,1,20);*/



	static tstWifiTableResponse const gasTableWifiResponseRst[] =
	{
			{	1,		"ready"				},
			{	2,		"ERROR"				},
			{  255,    "END OF ARRAY"		}
	};


	TaskWifi_SendAtCmd1("WF:>AT\r\n","AT+RST\r\n");

	ucResp = TaskWifi_WaitResponse1(&gasTableWifiResponseRst[0],TaskWifi_ParseResp1,1,20);

    switch(ucResp)
    {
        case 0:
        case 2:
			stWifiMsg.ucSrc = SRC_WIFI;
			stWifiMsg.ucDest = SRC_WIFI;
			stWifiMsg.ucEvent = EVENT_WIFI_SEND_RESET;
			xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);
            boError = false;
        break;

        case 1:
			stWifiMsg.ucSrc = SRC_WIFI;
			stWifiMsg.ucDest = SRC_WIFI;
			stWifiMsg.ucEvent = EVENT_WIFI_SEND_MODE;
			xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);
            boError = true;
        break;

    }
    return(boError);
}


//////////////////////////////////////////////
//
//
//              TaskWifi_SendMode
//
//
//////////////////////////////////////////////
unsigned char TaskWifi_SendMode(sMessageType *psMessage)
{
    unsigned char boError = false;
    unsigned char ucResp;
/*
    TaskWifi_SendAtCmd("WF:>CWMODE\r\n","AT+CWMODE=1\r\n",cModem_OK,cModem_ERROR);

    ucResp = TaskWifi_WaitResponse(TaskWifi_ParseResp,1,5);
*/

	TaskWifi_SendAtCmd1("WF:>CWMODE\r\n","AT+CWMODE=1\r\n");

	ucResp = TaskWifi_WaitResponse1(&gasTableWifiResponse[0],TaskWifi_ParseResp1,1,5);


    switch(ucResp)
    {
        case 0:
        case 2:
			stWifiMsg.ucSrc = SRC_WIFI;
			stWifiMsg.ucDest = SRC_WIFI;
			stWifiMsg.ucEvent = EVENT_WIFI_SEND_MODE;
			xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);
            boError = false;
        break;

        case 1:
			stWifiMsg.ucSrc = SRC_WIFI;
			stWifiMsg.ucDest = SRC_WIFI;
			stWifiMsg.ucEvent = EVENT_WIFI_SEND_MUX;
			xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);
            boError = true;
        break;

        default:
        break;
    }
    return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskWifi_SendMux
//
//
//////////////////////////////////////////////
unsigned char TaskWifi_SendMux(sMessageType *psMessage)
{
    unsigned char boError = false;
    unsigned char ucResp;
/*
    TaskWifi_SendAtCmd("WF:>MUX\r\n","AT+CIPMUX=0\r\n",cModem_OK,cModem_ERROR);

    ucResp = TaskWifi_WaitResponse(TaskWifi_ParseResp,1,20);
*/

	TaskWifi_SendAtCmd1("WF:>MUX\r\n","AT+CIPMUX=0\r\n");

	ucResp = TaskWifi_WaitResponse1(&gasTableWifiResponse[0],TaskWifi_ParseResp1,1,5);

    switch(ucResp)
    {
    	case 0:
        case 2:
			stWifiMsg.ucSrc = SRC_WIFI;
			stWifiMsg.ucDest = SRC_WIFI;
			stWifiMsg.ucEvent = EVENT_WIFI_SEND_MUX;
			xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);
            boError = false;
            break;

        case 1:
			stWifiMsg.ucSrc = SRC_WIFI;
			stWifiMsg.ucDest = SRC_WIFI;
			stWifiMsg.ucEvent = EVENT_WIFI_SEND_CIP;
			xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);
            boError = true;
            break;

        default:
        	break;
    }
    return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskWifi_SendCip
//
//
//////////////////////////////////////////////
unsigned char TaskWifi_SendCip(sMessageType *psMessage)
{
    unsigned char boError = true;
    unsigned char ucResp;

    /*TaskWifi_SendAtCmd("WF:>CIP\r\n","AT+CIPMODE=0\r\n",cModem_OK,cModem_ERROR);

    ucResp = TaskWifi_WaitResponse(TaskWifi_ParseResp,5,20);
*/

	TaskWifi_SendAtCmd1("WF:>CIP\r\n","AT+CIPMODE=0\r\n");

	ucResp = TaskWifi_WaitResponse1(&gasTableWifiResponse[0],TaskWifi_ParseResp1,1,5);

    switch(ucResp)
    {
    	case 0:
        case 2:
			stWifiMsg.ucSrc = SRC_WIFI;
			stWifiMsg.ucDest = SRC_WIFI;
			stWifiMsg.ucEvent = EVENT_WIFI_SEND_CIP;
			xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);
            boError = false;
            break;

        case 1:
			stWifiMsg.ucSrc = SRC_WIFI;
			stWifiMsg.ucDest = SRC_WIFI;
			stWifiMsg.ucEvent = EVENT_WIFI_SEND_AP;
			xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);
        	ucWifiTry = 0;
            boError = true;
        break;

        default:
        break;
    }
    return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskWifi_SendAp
//
//
//////////////////////////////////////////////
unsigned char TaskWifi_SendAp(sMessageType *psMessage)
{
    unsigned char boError = false;
    unsigned char ucResp;
	char* cLocalBuffer = (char*) malloc(RX_BUF_SIZE+1);

    memset(cLocalBuffer,0,RX_BUF_SIZE+1);
    sprintf(cLocalBuffer,"AT+CWJAP=\"%s\",\"%s\"\r\n",cWifiApName,cWifiApPassword);

/*    TaskWifi_SendAtCmd("WF:>CWJAP\r\n",cLocalBuffer,cModem_OK,cModem_ERROR);


    ucResp = TaskWifi_WaitResponse(TaskWifi_ParseResp,1, 20);
*/

	TaskWifi_SendAtCmd1("WF:>CWJAP\r\n",cLocalBuffer);

	ucResp = TaskWifi_WaitResponse1(&gasTableWifiResponse[0],TaskWifi_ParseResp1,1,5);


    switch(ucResp)
    {
    	case 0:
        case 2:
			stWifiMsg.ucSrc = SRC_WIFI;
			stWifiMsg.ucDest = SRC_WIFI;
			stWifiMsg.ucEvent = EVENT_WIFI_SEND_AP;
			xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);
            boError = false;
            break;

        case 1:
			stWifiMsg.ucSrc = SRC_WIFI;
			stWifiMsg.ucDest = SRC_WIFI;
			stWifiMsg.ucEvent = EVENT_WIFI_SEND_START;
			xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);
			ucWifiTry = 0;
            boError = true;
            break;

        default:
        	break;
    }
    free(cLocalBuffer);
    return(boError);
}
//////////////////////////////////////////////
//
//
//              TaskWifi_ApConnected
//
//
//////////////////////////////////////////////
unsigned char TaskWifi_ApConnected(sMessageType *psMessage)
{
	unsigned char boError = true;
#if 0
	Debug("WF:>AP CONNECTED\r\n");

	stWifiMsg.ucSrc = SRC_WIFI;
	stWifiMsg.ucDest = SRC_SD;
	stWifiMsg.ucEvent = EVENT_SD_OPENING;
	xQueueSend( xQueueSd, ( void * )&stWifiMsg, 0);
#endif
	return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskWifi_ApDisconnected
//
//
//////////////////////////////////////////////
unsigned char TaskWifi_ApDisconnected(sMessageType *psMessage)
{
	unsigned char boError = true;
#if 0
	Debug("WF:>AP DISCONNECTED\r\n");

	stWifiMsg.ucSrc = SRC_WIFI;
	stWifiMsg.ucDest = SRC_DEBUG;
	stWifiMsg.ucEvent = EVENT_IO_GSM_INIT;
	xQueueSend( xQueueSd, ( void * )&stWifiMsg, 0);
#endif
	return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskWifi_OpeningFileOk
//
//
//////////////////////////////////////////////
unsigned char TaskWifi_OpeningFileOk(sMessageType *psMessage)
{
	unsigned char boError = true;
#if 0
	Debug("WF:>OPENING FILE OK\r\n");


	stWifiMsg.ucSrc = SRC_WIFI;
	stWifiMsg.ucDest = SRC_SD;
	stWifiMsg.ucEvent = EVENT_SD_READING;
	xQueueSend( xQueueSd, ( void * )&stWifiMsg, 0);
	ucWifiTry = 0;
#endif
	return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskWifi_SendStart
//
//
//////////////////////////////////////////////
unsigned char TaskWifi_SendStart(sMessageType *psMessage)
{
    unsigned char boError = true;
    unsigned char ucResp;
	char* cLocalBuffer = (char*) malloc(RX_BUF_SIZE+1);

    memset(cLocalBuffer,0,RX_BUF_SIZE+1);
    sprintf(cLocalBuffer,"%s%s%s\r\n",cHttpAtStartCIP,cHttpDns,cHttpAtEndCIP);

    /*TaskWifi_SendAtCmd("WF:>START\r\n",cLocalBuffer,cModem_OK,cModem_ERROR);
    ucResp = TaskWifi_WaitResponse(TaskWifi_ParseResp,5, 20);
*/

	TaskWifi_SendAtCmd1("WF:>START\r\n",cLocalBuffer);

	ucResp = TaskWifi_WaitResponse1(&gasTableWifiResponse[0],TaskWifi_ParseResp1,1,5);

    switch(ucResp)
    {
        case 0:
        	if(++ucWifiTry<=3)
			{
        		stWifiMsg.ucSrc = SRC_WIFI;
    			stWifiMsg.ucDest = SRC_WIFI;
    			stWifiMsg.ucEvent = EVENT_WIFI_SEND_START;
			}
        	else
        	{
    			stWifiMsg.ucSrc = SRC_WIFI;
    			stWifiMsg.ucDest = SRC_WIFI;
    			stWifiMsg.ucEvent = EVENT_WIFI_ERROR;
        	}
			xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);
            boError = false;
        break;

        case 1:
        case 2:
			stWifiMsg.ucSrc = SRC_WIFI;
			stWifiMsg.ucDest = SRC_WIFI;
			stWifiMsg.ucEvent = EVENT_WIFI_SEND_CIPSEND_TIMESTAMP;
			xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);
			ucWifiTry = 0;
            boError = true;
        break;

        default:
        break;
    }
    free(cLocalBuffer);
    return(boError);
}

//////////////////////////////////////////////
//
//
//       TaskWifi_SendCipsendTimestamp
//
//
//////////////////////////////////////////////
/*const char cLINKINVALID[]  		= {"link is not"};*/
unsigned char TaskWifi_SendCipsendTimestamp(sMessageType *psMessage)
{
	unsigned char boError = true;
	unsigned int u16PayloadLen;
    unsigned char ucResp = 0;

	char* cLocalBuffer = (char*) malloc(RX_BUF_SIZE+1);
    memset(cLocalBuffer,0,RX_BUF_SIZE+1);

    /*sprintf(cLocalBuffer,"%s","GET http://gpslogger.esy.es/pages/upload/timestamp.php HTTP/1.1\r\nHost: gpslogger.esy.es\r\nContent-Type: application/x-www-form-urlencoded\r\n\r\n");*/
    sprintf(cLocalBuffer,"GET %s %s",cWebPage1,"HTTP/1.1\r\nHost: gpslogger.esy.es\r\nContent-Type: application/x-www-form-urlencoded\r\n\r\n");

    u16PayloadLen = strlen(cLocalBuffer);

	memset(cLocalBuffer,0x00,RX_BUF_SIZE+1);
	sprintf(cLocalBuffer,"AT+CIPSEND=%d\r\n",u16PayloadLen);

    /*TaskWifi_SendAtCmd("WF:>CIPSEND\r\n",cLocalBuffer,">",cLINKINVALID);

    ucResp = TaskWifi_WaitResponse(TaskWifi_ParseResp,1, 10);
*/

	static tstWifiTableResponse const gasTableWifiResponseSendCipTimestamp[] =
	{
			{	1,		">"				},
			{	2,		"link is not"	},
			{  255,    "END OF ARRAY"	}
	};

	TaskWifi_SendAtCmd1("WF:>CIPSEND\r\n",cLocalBuffer);

	ucResp = TaskWifi_WaitResponse1(&gasTableWifiResponseSendCipTimestamp[0],TaskWifi_ParseResp1,1,5);

    switch(ucResp)
    {
        case 0:
        case 2:
        	if(++ucWifiTry<=3)
			{
				stWifiMsg.ucSrc = SRC_WIFI;
				stWifiMsg.ucDest = SRC_WIFI;
				stWifiMsg.ucEvent = EVENT_WIFI_SEND_CIPSEND_TIMESTAMP;
			}
        	else
        	{
				stWifiMsg.ucSrc = SRC_WIFI;
				stWifiMsg.ucDest = SRC_WIFI;
				stWifiMsg.ucEvent = EVENT_WIFI_ERROR;
			}
			xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);
			boError = false;
        break;

        case 1:
			stWifiMsg.ucSrc = SRC_WIFI;
			stWifiMsg.ucDest = SRC_WIFI;
			stWifiMsg.ucEvent = EVENT_WIFI_SEND_DATA_TIMESTAMP;
			xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);
			ucWifiTry = 0;
            boError = true;
        break;

        default:
        break;
    }
    free(cLocalBuffer);
    return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskWifi_SendDataTimestamp
//
//
//////////////////////////////////////////////
unsigned char TaskWifi_SendDataTimestamp(sMessageType *psMessage)
{
	unsigned char boError = true;

	unsigned int u16PayloadLen;
	unsigned char ucResp = 0;

	char* cLocalBuffer = (char*) malloc(RX_BUF_SIZE+1);
    memset(cLocalBuffer,0,RX_BUF_SIZE+1);

    sprintf(cLocalBuffer,"GET %s %s",cWebPage1,"HTTP/1.1\r\nHost: gpslogger.esy.es\r\nContent-Type: application/x-www-form-urlencoded\r\n\r\n");

	u16PayloadLen = strlen(cLocalBuffer);
	cLocalBuffer[u16PayloadLen]=0x1A;

    /*TaskWifi_SendAtCmd("WF:>DATA\r\n",cLocalBuffer,"Timestamp=","busy");

    ucResp = TaskWifi_WaitResponse(TaskWifi_ParseResp,5,10);
*/

	static tstWifiTableResponse const gasTableWifiResponseSendDataTimestamp[] =
	{
			{	1,		"Timestamp="				},
			{	2,		"busy"						},
			{  255,    "END OF ARRAY"				}
	};

	TaskWifi_SendAtCmd1("WF:>DATA\r\n",cLocalBuffer);

	ucResp = TaskWifi_WaitResponse1(&gasTableWifiResponseSendDataTimestamp[0],TaskWifi_ParseResp1,1,5);

    switch(ucResp)
    {
        case 0:
        case 2:
        	if(++ucWifiTry<=3)
			{
				stWifiMsg.ucSrc = SRC_WIFI;
				stWifiMsg.ucDest = SRC_WIFI;
				stWifiMsg.ucEvent = EVENT_WIFI_SEND_DATA_TIMESTAMP;
			}
			else
			{
				stWifiMsg.ucSrc = SRC_WIFI;
				stWifiMsg.ucDest = SRC_WIFI;
				stWifiMsg.ucEvent = EVENT_WIFI_ERROR;
			}
			xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);
			boError = false;
		break;

        case 1:
        {
			const char *ptr=NULL;

			ptr = strstr((const char*)&cWifiRxBuffer[0],(const char*)"Timestamp=");
			if(ptr != NULL)
			{
				ptr +=strlen((const char*)("Timestamp="));
				ulTimestamp = atol(ptr);
				/*ESP_LOGI(WIFI_TASK_TAG,"Timestamp=%ld\r\n",ulTimestamp);*/

				stWifiMsg.ucSrc = SRC_WIFI;
				stWifiMsg.ucDest = SRC_WIFI;
				stWifiMsg.ucEvent = EVENT_WIFI_SEND_CLOSE;
				xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);
				ucWifiTry = 0;
			}
        }
        break;


        default:
        break;

    }
    free(cLocalBuffer);
    return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskWifi_CloseConnection
//
//
//////////////////////////////////////////////
unsigned char TaskWifi_CloseConnection(sMessageType *psMessage)
{
	unsigned char boError = true;
	unsigned char ucResp = 0;

    /*TaskWifi_SendAtCmd("WF:>CLOSE\r\n","AT+CIPCLOSE\r\n",cModem_OK,cModem_ERROR);

    ucResp = TaskWifi_WaitResponse(TaskWifi_ParseResp,5,10);
*/
	TaskWifi_SendAtCmd1("WF:>CLOSE\r\n","AT+CIPCLOSE\r\n");

	ucResp = TaskWifi_WaitResponse1(&gasTableWifiResponse[0],TaskWifi_ParseResp1,1,5);

    switch(ucResp)
    {
        case 0:
        case 2:
        	if(++ucWifiTry<=3)
			{
				stWifiMsg.ucSrc = SRC_WIFI;
				stWifiMsg.ucDest = SRC_WIFI;
				stWifiMsg.ucEvent = EVENT_WIFI_SEND_CLOSE;
			}
			else
			{
				stWifiMsg.ucSrc = SRC_WIFI;
				stWifiMsg.ucDest = SRC_WIFI;
				stWifiMsg.ucEvent = EVENT_WIFI_ERROR;
			}
			xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);
			boError = false;

		break;

        case 1:
			stWifiMsg.ucSrc = SRC_WIFI;
			stWifiMsg.ucDest = SRC_SD;
			stWifiMsg.ucEvent = EVENT_SD_OPENING;
			xQueueSend( xQueueSd, ( void * )&stWifiMsg, 0);
			ucWifiTry  = 0;
			boError = true;
        break;


        default:
        break;

    }
    return(boError);
}


//////////////////////////////////////////////
//
//
//              TaskWifi_CipStatus
//
//
//////////////////////////////////////////////
unsigned char TaskWifi_CipStatus(sMessageType *psMessage)
{
	unsigned char boError = true;
	unsigned char ucResp = 0;

	TaskWifi_SendAtCmd1("WF:>CIPSTATUS\r\n","AT+CIPSTATUS\r\n");

	static tstWifiTableResponse const gasTableWifiResponseCipStatus[] =
	{
			{	1,		"GOT IP"				},
			{	2,		"Connected"				},
			{	3,		"Disconnected"			},
			{  255,    "END OF ARRAY"			}
	};

	ucResp = TaskWifi_WaitResponse1(&gasTableWifiResponseCipStatus[0],TaskWifi_ParseResp1,1,5);

    switch(ucResp)
    {
        case 0:
        	if(++ucWifiTry<=3)
			{
				stWifiMsg.ucSrc = SRC_WIFI;
				stWifiMsg.ucDest = SRC_WIFI;
				stWifiMsg.ucEvent = EVENT_WIFI_SEND_CIPSTATUS;
			}
			else
			{
				stWifiMsg.ucSrc = SRC_WIFI;
				stWifiMsg.ucDest = SRC_WIFI;
				stWifiMsg.ucEvent = EVENT_WIFI_ERROR;
			}
			xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);
			boError = false;
		break;

        case 3:
			stWifiMsg.ucSrc = SRC_WIFI;
			stWifiMsg.ucDest = SRC_WIFI;
			stWifiMsg.ucEvent = EVENT_WIFI_ERROR;

			xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);
			boError = false;
		break;

        case 1:
        case 2:
			stWifiMsg.ucSrc = SRC_WIFI;
			stWifiMsg.ucDest = SRC_SD;
			stWifiMsg.ucEvent = EVENT_WIFI_SEND_START_PAYLOAD/*EVENT_SD_OPENING*/;
			xQueueSend( xQueueSd, ( void * )&stWifiMsg, 0);
			ucWifiTry  = 0;
			boError = true;
        break;

        default:
        break;

    }
    return(boError);
}
//////////////////////////////////////////////
//
//
//              TaskWifi_SendStartPayload
//
//
//////////////////////////////////////////////
unsigned char TaskWifi_SendStartPayload(sMessageType *psMessage)
{
    unsigned char boError = true;
    unsigned char ucResp;
	char* cLocalBuffer = (char*) malloc(RX_BUF_SIZE+1);

    memset(cLocalBuffer,0,RX_BUF_SIZE+1);
    sprintf(cLocalBuffer,"%s%s%s\r\n",cHttpAtStartCIP,cHttpDns,cHttpAtEndCIP);
/*
    TaskWifi_SendAtCmd("WF:>START\r\n",cLocalBuffer,"OK",cModem_ERROR);
    ucResp = TaskWifi_WaitResponse(TaskWifi_ParseResp,5, 20);
*/

	TaskWifi_SendAtCmd1("WF:>START\r\n",cLocalBuffer);
	ucResp = TaskWifi_WaitResponse1(&gasTableWifiResponse[0],TaskWifi_ParseResp1,1,5);

    switch(ucResp)
    {
        case 0:
        	if(++ucWifiTry<=3)
			{
        		stWifiMsg.ucSrc = SRC_WIFI;
    			stWifiMsg.ucDest = SRC_WIFI;
    			stWifiMsg.ucEvent = EVENT_WIFI_SEND_START_PAYLOAD;
			}
        	else
        	{
    			stWifiMsg.ucSrc = SRC_WIFI;
    			stWifiMsg.ucDest = SRC_WIFI;
    			stWifiMsg.ucEvent = EVENT_WIFI_ERROR;
        	}
			xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);
            boError = false;
        break;

        case 1:
        case 2:
			stWifiMsg.ucSrc = SRC_WIFI;
			stWifiMsg.ucDest = SRC_WIFI;
			stWifiMsg.ucEvent = EVENT_WIFI_SEND_CIPSEND_PAYLOAD;
			xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);
			ucWifiTry = 0;
            boError = true;
        break;

        default:
        break;
    }
    free(cLocalBuffer);
    return(boError);
}

//////////////////////////////////////////////
//
//
//       TaskWifi_SendCipsendPayload
//
//
//////////////////////////////////////////////

unsigned char TaskWifi_SendCipsendPayload(sMessageType *psMessage)
{
	unsigned char boError = true;
	unsigned int u16PayloadLen;
    unsigned char ucResp = 0;

	char* cLocalBuffer = (char*) malloc(RX_BUF_SIZE+256);
    memset(cLocalBuffer,0,RX_BUF_SIZE+256);

    sprintf(cLocalBuffer,"POST %s %s%d%s%s\r\n",cWebPage2,"HTTP/1.1\r\nHost: gpslogger.esy.es\r\nAccept: */*\r\nContent-Length: ",strlen((const char*)cConfigAndData),"\r\nConnection: Keep-Alive\r\nContent-Type: application/x-www-form-urlencoded\r\n\r\n",cConfigAndData);
    u16PayloadLen = strlen(cLocalBuffer);

	memset(cLocalBuffer,0x00,RX_BUF_SIZE+1);
	sprintf(cLocalBuffer,"AT+CIPSEND=%d\r\n",u16PayloadLen);

    /*TaskWifi_SendAtCmd("WF:>CIPSEND\r\n",cLocalBuffer,">","CLOSED");

    ucResp = TaskWifi_WaitResponse(TaskWifi_ParseResp,1, 10);*/

	static tstWifiTableResponse const gasTableWifiResponseSendCipPayload[] =
	{
			{	1,		">"						},
			{	2,		"CLOSED"				},
			{  255,    "END OF ARRAY"			}
	};

	TaskWifi_SendAtCmd1("WF:>CIPSEND\r\n",cLocalBuffer);
	ucResp = TaskWifi_WaitResponse1(&gasTableWifiResponseSendCipPayload[0],TaskWifi_ParseResp1,1,5);

    switch(ucResp)
    {
        case 0:
        case 2:
        	if(++ucWifiTry<=3)
			{
				stWifiMsg.ucSrc = SRC_WIFI;
				stWifiMsg.ucDest = SRC_WIFI;
				stWifiMsg.ucEvent = EVENT_WIFI_SEND_START_PAYLOAD;
			}
			else
			{
				stWifiMsg.ucSrc = SRC_WIFI;
				stWifiMsg.ucDest = SRC_WIFI;
				stWifiMsg.ucEvent = EVENT_WIFI_ERROR;
			}
			xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);
			boError = false;
        break;

        case 1:
			stWifiMsg.ucSrc = SRC_WIFI;
			stWifiMsg.ucDest = SRC_WIFI;
			stWifiMsg.ucEvent = EVENT_WIFI_SEND_DATA_PAYLOAD;
			xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);
			ucWifiTry = 0;
            boError = true;
        break;

        default:
        break;
    }
    free(cLocalBuffer);
    return(boError);
}

//////////////////////////////////////////////
//
//
//        TaskWifi_SendDataPayload
//
//
//////////////////////////////////////////////
unsigned char TaskWifi_SendDataPayload(sMessageType *psMessage)
{
	unsigned char boError = true;

	unsigned int u16PayloadLen;
	unsigned char ucResp = 0;

	char* cLocalBuffer = (char*) malloc(RX_BUF_SIZE+256);
    memset(cLocalBuffer,0,RX_BUF_SIZE+256);

    sprintf(cLocalBuffer,"POST %s %s%d%s%s\r\n",cWebPage2,"HTTP/1.1\r\nHost: gpslogger.esy.es\r\nAccept: */*\r\nContent-Length: ",strlen((const char*)cConfigAndData),"\r\nConnection: Keep-Alive\r\nContent-Type: application/x-www-form-urlencoded\r\n\r\n",cConfigAndData);
	u16PayloadLen = strlen(cLocalBuffer);
	cLocalBuffer[u16PayloadLen]=0x1A;

    /*TaskWifi_SendAtCmd("WF:>DATA\r\n",cLocalBuffer,"ReGcOr","busy");

    ucResp = TaskWifi_WaitResponse(TaskWifi_ParseResp,5,10);
*/

	static tstWifiTableResponse const gasTableWifiResponseSendDataPayload[] =
	{
			{	1,		"ReGcOr"				},
			{	2,		"busy"					},
			{  255,    "END OF ARRAY"			}
	};

	TaskWifi_SendAtCmd1("WF:>DATA\r\n",cLocalBuffer);
	ucResp = TaskWifi_WaitResponse1(&gasTableWifiResponseSendDataPayload[0],TaskWifi_ParseResp1,1,5);

    switch(ucResp)
    {
        case 0:
        case 2:
        	if(++ucWifiTry<=3)
			{
				stWifiMsg.ucSrc = SRC_WIFI;
				stWifiMsg.ucDest = SRC_WIFI;
				stWifiMsg.ucEvent = EVENT_WIFI_SEND_DATA_PAYLOAD;
				xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);
				boError = false;
			}
        	else
        	{
				stWifiMsg.ucSrc = SRC_WIFI;
				stWifiMsg.ucDest = SRC_WIFI;
				stWifiMsg.ucEvent = EVENT_WIFI_ERROR;
				xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);
				boError = false;
        	}
		break;

        case 1:

        	stWifiMsg.ucSrc = SRC_WIFI;
        	stWifiMsg.ucDest = SRC_HTTPCLI;
        	stWifiMsg.ucEvent = EVENT_HTTPCLI_GET_TELEGRAM;
        	stWifiMsg.pcMessageData = "";
			xQueueSend( xQueueHttpCli, ( void * )&stWifiMsg, 0);

        	stWifiMsg.ucSrc = SRC_WIFI;
        	stWifiMsg.ucDest = SRC_SD;
        	stWifiMsg.ucEvent = EVENT_SD_MARKING;
        	xQueueSend( xQueueSd, ( void * )&stWifiMsg, 0);

        	boError = true;
        break;


        default:
        break;

    }
    free(cLocalBuffer);
    return(boError);
}


//////////////////////////////////////////////
//
//
//              TaskWifi_Ending
//
//
//////////////////////////////////////////////
unsigned char TaskWifi_Ending(sMessageType *psMessage)
{
	unsigned char boError = true;

    ESP_LOGI(WIFI_TASK_TAG, "WF:>ENDING\r\n");

	vTaskDelay(2000/portTICK_PERIOD_MS);

	stWifiMsg.ucSrc = SRC_WIFI;
	stWifiMsg.ucDest = SRC_SD;
	stWifiMsg.ucEvent = EVENT_SD_OPENING;
	xQueueSend( xQueueSd, ( void * )&stWifiMsg, 0);

	return(boError);
}


//////////////////////////////////////////////
//
//
//              TaskWifi_Error
//
//
//////////////////////////////////////////////
unsigned char TaskWifi_Error(sMessageType *psMessage)
{
	unsigned char boError = true;

    ESP_LOGI(WIFI_TASK_TAG, "WF:>ERROR\r\n");

	stWifiMsg.ucSrc = SRC_WIFI;
	stWifiMsg.ucDest = SRC_WIFI;
	stWifiMsg.ucEvent = EVENT_WIFI_INIT;
	xQueueSend( xQueueWifi, ( void * )&stWifiMsg, 0);


	return(boError);
}
//////////////////////////////////////////////
//
//
//              TaskWifi_IgnoreEvent
//
//
//////////////////////////////////////////////
unsigned char TaskWifi_IgnoreEvent(sMessageType *psMessage)
{
    unsigned char boError = false;
    return(boError);
}

//////////////////////////////////////////////
//
//
//             Modem State Machine
//
//
//////////////////////////////////////////////
static sStateMachineType const gasTaskWifi_Initializing[] =
{
    /* Event        Action routine      Next state */
    //  State specific transitions
	{EVENT_WIFI_INIT,          TaskWifi_Init,                 	TASKWIFI_INITIALIZING,           		TASKWIFI_INITIALIZING     				},
	{EVENT_WIFI_SETBAUD,       TaskWifi_SetBaudRate,      		TASKWIFI_INITIALIZING,					TASKWIFI_INITIALIZING  					},
    {EVENT_WIFI_SEND_RESET,    TaskWifi_SendReset,           	TASKWIFI_INITIALIZING,   				TASKWIFI_INITIALIZING  					},


    {EVENT_WIFI_SEND_MODE,		TaskWifi_SendMode,           	TASKWIFI_INITIALIZING,   				TASKWIFI_INITIALIZING  					},
    {EVENT_WIFI_SEND_MUX,		TaskWifi_SendMux,           	TASKWIFI_INITIALIZING,   				TASKWIFI_INITIALIZING  					},
    {EVENT_WIFI_SEND_CIP,		TaskWifi_SendCip,           	TASKWIFI_INITIALIZING,   				TASKWIFI_INITIALIZING  					},
    {EVENT_WIFI_SEND_AP,		TaskWifi_SendAp,           		TASKWIFI_CONNECTING,	   				TASKWIFI_INITIALIZING  					},

	{EVENT_WIFI_ERROR,	       TaskWifi_Error,					TASKWIFI_INITIALIZING,  			  	TASKWIFI_INITIALIZING					},
    {EVENT_WIFI_NULL,          TaskWifi_IgnoreEvent,          	TASKWIFI_INITIALIZING,					TASKWIFI_INITIALIZING					}
};
static sStateMachineType const gasTaskWifi_Connecting[] =
{
    /* Event        Action routine      Next state */
    //  State specific transitions
    {EVENT_WIFI_SEND_START,	   				TaskWifi_SendStart,				TASKWIFI_CONNECTING,    	TASKWIFI_CONNECTING 		},
    {EVENT_WIFI_SEND_CIPSEND_TIMESTAMP,		TaskWifi_SendCipsendTimestamp,	TASKWIFI_CONNECTING,    	TASKWIFI_CONNECTING 		},
    {EVENT_WIFI_SEND_DATA_TIMESTAMP,		TaskWifi_SendDataTimestamp,		TASKWIFI_CONNECTING,    	TASKWIFI_CONNECTING 		},

    {EVENT_WIFI_SEND_CLOSE,					TaskWifi_CloseConnection,		TASKWIFI_CONNECTING,    	TASKWIFI_CONNECTING 		},

    {EVENT_WIFI_SEND_CIPSTATUS,	   			TaskWifi_CipStatus,				TASKWIFI_CONNECTING,    	TASKWIFI_CONNECTING 		},

    {EVENT_WIFI_SEND_START_PAYLOAD,	   		TaskWifi_SendStartPayload,		TASKWIFI_CONNECTING,    	TASKWIFI_CONNECTING 		},
    {EVENT_WIFI_SEND_CIPSEND_PAYLOAD,		TaskWifi_SendCipsendPayload,	TASKWIFI_CONNECTING,    	TASKWIFI_CONNECTING 		},
    {EVENT_WIFI_SEND_DATA_PAYLOAD,			TaskWifi_SendDataPayload,		TASKWIFI_CONNECTING,    	TASKWIFI_CONNECTING 		},

	{EVENT_WIFI_ENDING,	       				TaskWifi_Ending,				TASKWIFI_CONNECTING,  		TASKWIFI_CONNECTING			},
	{EVENT_WIFI_ERROR,	       				TaskWifi_Error,					TASKWIFI_INITIALIZING,  	TASKWIFI_CONNECTING			},
    {EVENT_WIFI_NULL,          				TaskWifi_IgnoreEvent,          	TASKWIFI_CONNECTING,		TASKWIFI_CONNECTING			}
};

static sStateMachineType const * const gpasTaskWifi_StateMachine[] =
{
    gasTaskWifi_Initializing,
    gasTaskWifi_Connecting
};

static unsigned char ucCurrentStateWifi = TASKWIFI_INITIALIZING;
void vTaskWifi( void *pvParameters )
{
	TickType_t elapsed_time;

	for( ;; )
	{
		elapsed_time = xTaskGetTickCount();
		if( xQueueReceive( xQueueWifi, &(stWifiMsg ),0 ) )
		{
            (void)eEventHandler ((unsigned char)SRC_WIFI,gpasTaskWifi_StateMachine[ucCurrentStateWifi], &ucCurrentStateWifi, &stWifiMsg);
		}

		vTaskDelayUntil(&elapsed_time, 1000 / portTICK_PERIOD_MS);
	}
}
