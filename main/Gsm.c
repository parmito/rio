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

#include "UartGsm.h"
#include "State.h"
#include "defines.h"
#include "Sd.h"
#include "Debug.h"
#include "Gsm.h"


//////////////////////////////////////////////
//
//
//            FUNCTION PROTOTYPES
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_ParseSmsResp(void);
unsigned char TaskSd_IgnoreEvent(sMessageType *psMessage);
unsigned char TaskGsm_ParseSmsGetCsq(sMessageType *psMessage);
unsigned char TaskGsm_ParseSmsGetCcid(sMessageType *psMessage);
unsigned char TaskGsm_ParseSmsGetGps(sMessageType *psMessage);
unsigned char TaskGsm_DeleteSmsMsg(sMessageType *psMessage);
void vTaskGsm( void *pvParameters );



//////////////////////////////////////////////
//
//
//            	VARIABLES
//
//
//////////////////////////////////////////////
char cGsmRxBuffer[RX_BUF_SIZE+128];
char cGsmSmsBuffer[RX_BUF_SIZE+128];
char *ptrRxGsm;
extern char cConfigAndData[RX_BUF_SIZE];


char cExpectedResp1[RX_BUF_SIZE_REDUCED];
char cExpectedResp2[RX_BUF_SIZE_REDUCED];

unsigned char ucModemAttempt = MODEM_ATTEMPT;

extern char cApn[RX_BUF_SIZE_REDUCED];
extern char cApnLogin[RX_BUF_SIZE_REDUCED];
extern char cApnPassword[RX_BUF_SIZE_REDUCED];
extern char cHttpDns[RX_BUF_SIZE_REDUCED];
extern char cWebPage1[RX_BUF_SIZE_REDUCED];
extern char cWebPage2[RX_BUF_SIZE_REDUCED];

const char cModem_OK[]      = {"OK"};
const char cModem_ERROR[] = {"ERROR"};
const char cModem_ACK[]     = {"ReGcOr"};
const char cCREG1[] ={"+CREG: 0,1"};
const char cCREG2[] ={"+CREG: 0,2"};
const char cCSQ1[]  ={"+CSQ:"};
const char cCSQ2[]  ={"+CSQ: 0,0"};
const char cCPIN1[] ={"+CPIN: READY"};
const char cCIICR1[] ={"+PDP: DEACT"};
const char cCGML[]  ={"+CMGL: "};
const char cHttpAtStartCIP[]    = {"AT+CIPSTART=\"TCP\",\""};
const char cHttpAtEndCIP[]      = {"\",80"};
const char cHttpHeader1[]       = {"POST "};
const char cHttpHeader2[]       = {" HTTP/1.1\r\nHost: "};
const char cHttpHeader3[]       = {":8080\r\nAccept: */*\r\nContent-Length: "};
const char cHttpHeader4[]       = {"Connection: Keep-Alive\r\nContent-Type: application/x-www-form-urlencoded\r\n\r\n"};
const char cHttpContent[]       = {"application/x-www-form-urlencoded"};

const char cTEL1[]  ={"TEL1=\""};
const char cTEL2[]  ={"TEL2=\""};
const char cTEL3[]  ={"TEL3=\""};
const char cRESET[]  ={"RESET"};
const char cALARM[]  ={"ALARM"};

const char cSET[]  ={"SET "};
const char cGET[]  ={"GET "};
const char cGETCSQ[]  ={"GET CSQ"};
const char cESN[]  ={"ESN"};
const char cGETCCID[]  ={"GET CCID"};

const char cSEND[] ={">"};
const char cCIPSEND[]  			= {"AT+CIPSEND="};

static const char *GSM_TASK_TAG = "GSM_TASK";
static SMS_TYPE stSmsRecv;
/*static PHONE_TYPE stPhone[3];*/

sMessageType stGsmMsg;
static unsigned char ucGsmTry = 0;
extern tstIo stIo;
extern unsigned long ulTimestamp;

static sSmsCommandType const gasTaskGsm_SmsCommandTable[] =
{
		{	"GET CSQ",		TaskGsm_ParseSmsGetCsq				},
		{	"GET CCID",		TaskGsm_ParseSmsGetCcid				},
		{	"GET GPS",		TaskGsm_ParseSmsGetGps				},
		/*{	"SET CFG PL",	TaskGsm_ParseSmsSetConfigPeriodLog	},*/
		{	(char*)NULL,	TaskGsm_DeleteSmsMsg				}
};





//////////////////////////////////////////////
//
//
//              Gsm_On
//
//
//////////////////////////////////////////////
void Gsm_On(void)
{
	gpio_set_level(GPIO_OUTPUT_GSM_ENABLE, 1);
}

//////////////////////////////////////////////
//
//
//              Gsm_Off
//
//
//////////////////////////////////////////////
void Gsm_Off(void)
{
	gpio_set_level(GPIO_OUTPUT_GSM_ENABLE, 0);
}

//////////////////////////////////////////////
//
//
//              Gsm_Io_Configuration
//
//
//////////////////////////////////////////////
void Gsm_Io_Configuration(void)
{
	gpio_config_t io_conf;
	//disable interrupt
	io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
	//set as output mode
	io_conf.mode = GPIO_MODE_OUTPUT;
	//bit mask of the pins that you want to set,e.g.GPIO18/19
	io_conf.pin_bit_mask = GPIO_OUTPUT_GSM_ENABLE_PIN_SEL ;
	//disable pull-down mode
	io_conf.pull_down_en = 0;
	//disable pull-up mode
	io_conf.pull_up_en = 0;
	//configure GPIO with the given settings
	gpio_config(&io_conf);

}


//////////////////////////////////////////////
//
//
//              Gsm_Init
//
//
//////////////////////////////////////////////
void GsmInit(void)
{
	/*esp_log_level_set(GSM_TASK_TAG, ESP_LOG_INFO);*/
    ESP_LOGI(GSM_TASK_TAG, "GSM INIT");

	Gsm_Io_Configuration();

	xQueueGsm = xQueueCreate(gsmQUEUE_LENGTH,			/* The number of items the queue can hold. */
								sizeof( sMessageType ) );	/* The size of each item the queue holds. */


    xTaskCreate(vTaskGsm, "vTaskGsm", 1024*4, NULL, configMAX_PRIORITIES-2, NULL);
	/* Create the queue used by the queue send and queue receive tasks.
	http://www.freertos.org/a00116.html */


	stGsmMsg.ucSrc = SRC_GSM;
	stGsmMsg.ucDest = SRC_GSM;
	stGsmMsg.ucEvent = EVENT_GSM_INIT;
	xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
}

//////////////////////////////////////////////
//
//
//              TaskGsm_SendAtCmd
//
//
//////////////////////////////////////////////
void TaskGsm_SendAtCmd(const char *pcDebug,const char *pcAtCmd,\
const char *pcSuccessResp, const char* pcFailedResp)
{

    ESP_LOGI(GSM_TASK_TAG, "\r\n<<<<SEND GSM>>>>\r\n%s\r\n%s\r\n<<<<>>>>\r\n",pcDebug,pcAtCmd);

    memset(&cExpectedResp1,0x00,RX_BUF_SIZE_REDUCED);
    strcpy(&cExpectedResp1[0],pcSuccessResp);

    memset(&cExpectedResp2,0x00,RX_BUF_SIZE_REDUCED);
    strcpy(&cExpectedResp2[0],pcFailedResp);

    memset(&cGsmRxBuffer,0x00,RX_BUF_SIZE);
    ptrRxGsm = &cGsmRxBuffer[0];

    UartGsmSendData(GSM_TASK_TAG,pcAtCmd);
}

//////////////////////////////////////////////
//
//
//              TaskGsm_ParseResp
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_ParseResp(void)
{
    unsigned char ucResp = 0;
    const char *ptr=NULL;


    ptr = strstr((const char*)&cGsmRxBuffer[0],(const char*)&cExpectedResp1[0]);
    if(ptr != NULL)
    {
        ucResp = 1;
        ESP_LOGI(GSM_TASK_TAG, "\r\n<<<<RESP:1 GSM>>>>\r\n%s\r\n<<<<>>>>\r\n",cGsmRxBuffer);
    }
    else
    {
        ptr = strstr((const char*)&cGsmRxBuffer[0],(const char*)&cExpectedResp2[0]);
        if(ptr != NULL)
        {
            ucResp = 2;
            ESP_LOGI(GSM_TASK_TAG, "\r\n<<<<RESP:2 GSM>>>>\r\n%s\r\n<<<<>>>>\r\n",cGsmRxBuffer);
        }
        else
        {
        	if(strlen(cGsmRxBuffer) > 0)
        	{
        		ESP_LOGI(GSM_TASK_TAG, "\r\n<<<<RESP:3 GSM>>>>\r\n%s\r\n<<<<>>>>\r\n",cGsmRxBuffer);
        	}
        }
    }

    return(ucResp);
}

//////////////////////////////////////////////
//
//
//        TaskGsm_WaitResponse
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_WaitResponse(unsigned char (*ucFunc)(void),unsigned char ucPeriod, unsigned char ucTimeout)
{
    unsigned char ucResp = 0;

    while((ucResp == 0) && (ucTimeout > 0))
    {
        ucResp = ucFunc();
        if(ucTimeout > 0 ) ucTimeout--;
        vTaskDelay((ucPeriod*500)/portTICK_PERIOD_MS);
    }

    return(ucResp);
}


//////////////////////////////////////////////
//
//
//              TaskGsm_Init
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_Init(sMessageType *psMessage)
{
    unsigned char boError = true;

    ESP_LOGI(GSM_TASK_TAG, "\r\n GSM INIT\r\n");

	Gsm_Off();
	vTaskDelay(100/portTICK_PERIOD_MS);
	Gsm_On();

	stGsmMsg.ucSrc = SRC_GSM;
	stGsmMsg.ucDest = SRC_GSM;
	stGsmMsg.ucEvent = EVENT_GSM_SETBAUD;
	xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);

    return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskGsm_SetBaudRate
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SetBaudRate(sMessageType *psMessage)
{
	unsigned char boError = true;
	unsigned char ucResp;

/*	stGsmMsg.ucSrc = SRC_GSM;
	stGsmMsg.ucDest = SRC_DEBUG;
	stGsmMsg.ucEvent = EVENT_IO_GSM_CONNECTING;
	xQueueSend( xQueueDebug, ( void * )&stGsmMsg, 0);*/

	TaskGsm_SendAtCmd("MD:>BAUD\r\n","AT\r\n",cModem_OK,cModem_ERROR);

	ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1,3);

	switch(ucResp)
	{
		case 0:
		case 2:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SETBAUD;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			boError = false;
		break;

		case 1:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_AT;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);

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
//              TaskGsm_SendAt
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendAt(sMessageType *psMessage)
{
    unsigned char boError = false;

    TaskGsm_SendAtCmd("MD:>AT\r\n","AT\r\n",cModem_OK,cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 10);

    switch(ucResp)
    {
        case 0:
        case 2:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_AT;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
            boError = false;
        break;

        case 1:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_CSQ;/*EVENT_GSM_SEND_CTZU;*/
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
            boError = true;
        break;

    }
    return(boError);
}


//////////////////////////////////////////////
//
//
//              TaskGsm_SendCpin
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendCpin(sMessageType *psMessage)
{
    unsigned char boError = true;
#if 0
    TaskGsm_SendAtCmd("MD:>CPIN\r\n","AT+CPIN?\r\n",cCPIN1,cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 10);

    switch(ucResp)
    {
        case 0:
        case 2:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_CPIN;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
            boError = false;
        break;

        case 1:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_CSQ;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
            boError = true;
        break;

        default:
        break;
    }
#endif
    return(boError);
}
//////////////////////////////////////////////
//
//
//              TaskGsm_SendCtzu
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendCtzu(sMessageType *psMessage)
{
    unsigned char boError = false;

    TaskGsm_SendAtCmd("MD:>CTZU\r\n","AT+CLTS=1\r\n",cModem_ERROR,cModem_OK);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 3);

    switch(ucResp)
    {
    	case 0:
        case 1:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_CTZU;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
            boError = false;
            break;
        case 2:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = /*EVENT_GSM_SEND_CIPMUX*/EVENT_GSM_SEND_CCLK/*EVENT_GSM_SEND_CGATT*//*EVENT_GSM_WEB_CONNECT*/;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);

			ucGsmTry = 0;
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
//              TaskGsm_SendCclk
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendCclk(sMessageType *psMessage)
{
    unsigned char boError = false;

    TaskGsm_SendAtCmd("MD:>CCLK\r\n","AT+CCLK?\r\n",cModem_ERROR,cModem_OK);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 3);

    switch(ucResp)
    {
    	case 0:
        case 1:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_CCLK;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
            boError = false;
            break;
        case 2:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = /*EVENT_GSM_SEND_CIPMUX*/EVENT_GSM_SEND_CSQ/*EVENT_GSM_SEND_CGATT*//*EVENT_GSM_WEB_CONNECT*/;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);

			ucGsmTry = 0;
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
//              TaskGsm_SendCsq
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendCsq(sMessageType *psMessage)
{
    unsigned char boError = false;

    TaskGsm_SendAtCmd("MD:>CSQ\r\n","AT+CSQ\r\n",cCSQ2,cCSQ1);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 3);

    switch(ucResp)
    {
    	case 0:
        case 1:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_CSQ;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
            boError = false;
            break;
        case 2:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = /*EVENT_GSM_SEND_CIPMUX*/EVENT_GSM_SELECT_SMS_FORMAT/*EVENT_GSM_SEND_CGATT*//*EVENT_GSM_WEB_CONNECT*/;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);

			ucGsmTry = 0;
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
//              TaskGsm_SendCgatt
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendCgatt(sMessageType *psMessage)
{
    unsigned char boError = true;

    TaskGsm_SendAtCmd("MD:>CGATT?\r\n","AT+CGATT?\r\n","+CGATT: 1",cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 10);

    switch(ucResp)
    {
    	case 0:
        case 2:
        	if(++ucGsmTry <=25)
        	{
    			stGsmMsg.ucSrc = SRC_GSM;
    			stGsmMsg.ucDest = SRC_GSM;
    			stGsmMsg.ucEvent = EVENT_GSM_SEND_CGATT;
    			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
        	else
        	{
        		stGsmMsg.ucSrc = SRC_GSM;
        		stGsmMsg.ucDest = SRC_GSM;
        		stGsmMsg.ucEvent = EVENT_GSM_ERROR;
        		xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        		boError = false;
        	}
            boError = false;
            break;

        case 1:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = /*EVENT_GSM_WEB_CONNECT*/EVENT_GSM_SEND_CGDCONT/*EVENT_GSM_SEND_CIPSHUT*//*EVENT_GSM_SEND_BEARERSET1*/;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			ucGsmTry = 0;
            boError = true;
        break;
    }

    return(boError);
}



//////////////////////////////////////////////
//
//
//              TaskGsm_SendCipStatus
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendCipStatus(sMessageType *psMessage)
{
    unsigned char boError = true;

    TaskGsm_SendAtCmd("MD:>CIPSTATUS\r\n","AT+CIPSTATUS\r\n","IP INITIAL",cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 30);

    switch(ucResp)
    {
        case 0:
        case 2:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_CIPSTATUS;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
            boError = false;
        break;

        case 1:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_CIPMUX;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
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
//              TaskGsm_SendCgdcont
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendCgdcont(sMessageType *psMessage)
{
    unsigned char boError = true;
	char* cLocalBuffer = (char*) malloc(RX_BUF_SIZE+1);

    memset(cLocalBuffer,0,RX_BUF_SIZE+1);
    sprintf(cLocalBuffer,"AT+CGDCONT=1,\"IP\",\"%s\"\r\n",cApn);
    TaskGsm_SendAtCmd("MD:>CGDCONT\r\n",cLocalBuffer,cModem_OK,cModem_ERROR);


    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 10);

    switch(ucResp)
    {
    	case 0:
        case 2:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_CGDCONT;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
            boError = false;
        break;

        case 1:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_CIPMUX;/*EVENT_GSM_SEND_CSTT;*/
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			ucGsmTry = 0;
            boError = true;
        break;
    }
    free(cLocalBuffer);
    return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskGsm_SendCipMux
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendCipMux(sMessageType *psMessage)
{
    unsigned char boError = true;

    TaskGsm_SendAtCmd("MD:>CIPMUX\r\n","AT+CIPMUX=0\r\n",cModem_OK,cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 20);

    switch(ucResp)
    {
		case 0:
		case 2:
		if(++ucGsmTry <=3)
		{
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_CIPMUX;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			boError = false;
		}
		else
		{
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_ERROR;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			boError = false;
		}
		break;

        case 1:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_CSTT;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			ucGsmTry = 0;
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
//              TaskGsm_SendCstt
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendCstt(sMessageType *psMessage)
{
    unsigned char boError = true;
	char* cLocalBuffer = (char*) malloc(RX_BUF_SIZE+1);

    memset(cLocalBuffer,0,RX_BUF_SIZE+1);
	sprintf(cLocalBuffer,"AT+CSTT=\"%s\",\"%s\",\"%s\"\r\n",cApn,cApnLogin,cApnPassword);

	TaskGsm_SendAtCmd("MD:>CSTT\r\n",cLocalBuffer,cModem_OK,cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 10);

    switch(ucResp)
    {
        case 0:
        case 2:
        	if(++ucGsmTry <=3)
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_SEND_CSTT;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
				boError = false;
        	}
        	else
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_ERROR;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
				boError = false;
        	}
        break;

        case 1:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_CIICR;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			ucGsmTry = 0;
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
//           TaskGsm_SelectSmsFormat
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SelectSmsFormat(sMessageType *psMessage)
{
    unsigned char boError = true;

    TaskGsm_SendAtCmd("MD:>SMS FORMAT\r\n","AT+CMGF=1\r\n",cModem_OK,cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 10);

    switch(ucResp)
    {
        case 1:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = /*EVENT_GSM_SEND_CGATT*/EVENT_GSM_LIST_SMS_MSG/*EVENT_GSM_PREPARE_SMS_RESP*/;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			boError = true;
        break;

		default:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SELECT_SMS_FORMAT;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			boError = false;
		break;
    }
    return(boError);
}

//////////////////////////////////////////////
//
//
//           TaskGsm_ListSmsMsg
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_ListSmsMsg(sMessageType *psMessage)
{
    unsigned char boError = true;

    TaskGsm_SendAtCmd("MD:>CMGL\r\n","AT+CMGL=\"REC UNREAD\",1\r\n",cCGML,cModem_OK);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseSmsResp,1,20);

    switch(ucResp)
    {
        case 1:
            stGsmMsg.ucSrc = SRC_GSM;
            stGsmMsg.ucDest = SRC_GSM;
            stGsmMsg.ucEvent = /*EVENT_GSM_SEND_CGATT*/EVENT_GSM_DECODE_SMS_MSG;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
            boError = true;
        break;

        default:
   			stGsmMsg.ucEvent = EVENT_GSM_SEND_CGATT;
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			boError = false;

        break;
    }
    return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskGsm_ParseSmsResp
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_ParseSmsResp(void)
{
    unsigned char ucResp = 0;
    const char *ptr=NULL;

    SMS_TYPE *ptrSms = &stSmsRecv;

    memset(&stSmsRecv,0x00,sizeof(SMS_TYPE));

    ptr = strstr((const char*)&cGsmRxBuffer[0],(const char*)&cExpectedResp1[0]);

    if(ptr != NULL)
    {
        ucResp = 1;

        ESP_LOGI(GSM_TASK_TAG, "\r\n<<<<RESP:1 GSM>>>>\r\n%s<<<<>>>>\r\n",cGsmRxBuffer);

        ptr+=(sizeof(cCGML)-1);
        ptr = strtok((char*)ptr,",");
        strcpy(ptrSms->cIndexMsg,ptr);

        ESP_LOGI(GSM_TASK_TAG, "\r\n<<<<INDEX>>>>\r\n%s<<<<>>>>\r\n",ptrSms->cIndexMsg);

        ptr=strtok(NULL,",");
        ptr=strtok(NULL,",");
        strcpy(ptrSms->cPhoneNumber,ptr);
        ESP_LOGI(GSM_TASK_TAG, "\r\n<<<<PHONE>>>>\r\n%s<<<<>>>>\r\n",ptrSms->cPhoneNumber);

        ptr=strtok(NULL,",");
        ptr=strtok(NULL,",");
        ptr=strtok(NULL,"\r\n");
        ptr=strtok(NULL,"\r\n");
        strcpy(ptrSms->cMessageText,ptr);
        ESP_LOGI(GSM_TASK_TAG, "\r\n<<<<TEXT>>>>\r\n%s<<<<>>>>\r\n",ptrSms->cMessageText);
    }
    else
    {
        ptr = strstr((const char*)&cGsmRxBuffer[0],(const char*)&cExpectedResp2[0]);
        if(ptr != NULL)
        {
            ucResp = 2;
        }
    }
    return(ucResp);
}

//////////////////////////////////////////////
//
//
//         TaskGsm_DecodeSmsMsg
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_DecodeSmsMsg(sMessageType *psMessage)
{
    unsigned char boError = true;
    SMS_TYPE *pst = NULL;

    ESP_LOGI(GSM_TASK_TAG, "MD:>DECODE SMS\r\n");

    pst = &stSmsRecv;
	sSmsCommandType const*pstSmsCommand = &gasTaskGsm_SmsCommandTable[0];

	for (;; pstSmsCommand++)
	{
		if((pstSmsCommand->pcCommand != NULL) &&  (strstr((const char*)pst->cMessageText,pstSmsCommand->pcCommand) == NULL))
		{
		   continue;
		}
		return (*pstSmsCommand->FunctionPointer)(psMessage);
	}
	return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskGsm_ParseSmsGetCsq
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_ParseSmsGetCsq(sMessageType *psMessage)
{
	unsigned char boError = true;

	stGsmMsg.ucSrc = SRC_GSM;
	stGsmMsg.ucDest = SRC_GSM;
	stGsmMsg.ucEvent = EVENT_GSM_SEND_SMS_CSQ;
	stGsmMsg.pcMessageData = NULL;
	xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);

	return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskGsm_ParseSmsGetCcid
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_ParseSmsGetCcid(sMessageType *psMessage)
{
	unsigned char boError = true;

	stGsmMsg.ucSrc = SRC_GSM;
	stGsmMsg.ucDest = SRC_GSM;
	stGsmMsg.ucEvent = EVENT_GSM_SEND_SMS_CSQ;
	stGsmMsg.pcMessageData = NULL;
	xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);

	return(boError);
}


//////////////////////////////////////////////
//
//
//              TaskGsm_ParseSmsGetGps
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_ParseSmsGetGps(sMessageType *psMessage)
{
	unsigned char boError = true;

	stGsmMsg.ucSrc = SRC_GSM;
	stGsmMsg.ucDest = SRC_GSM;
	stGsmMsg.ucEvent = EVENT_GSM_SEND_SMS_GPS;
	stGsmMsg.pcMessageData = NULL;
	xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);

	return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskGsm_ParseSmsSetConfigPeriodLog
//
//
//////////////////////////////////////////////
/*unsigned char TaskGsm_ParseSmsSetConfigPeriodLog(sMessageType *psMessage)
{
	unsigned char boError = true;
    SMS_TYPE *pst = NULL;

    Debug("MD:>SET CFG PL\r\n");

    pst = &stSmsRecv;




	memset(&cConfigAndData[0],0,sizeof(cConfigAndData));
	sFLASH_ReadBuffer((uint8_t*)&cConfigAndData[0], CONFIG_FILENAME_ADDR, sizeof(CONFIG_FILENAME_ADDR));




	stGsmMsg.ucSrc = SRC_GSM;
	stGsmMsg.ucDest = SRC_GSM;
	stGsmMsg.ucEvent = EVENT_GSM_SEND_SMS_GPS;
	stGsmMsg.pcMessageData = NULL;
	xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);

	return(boError);
}*/
//////////////////////////////////////////////
//
//
//              TaskGsm_SendSmsCsq
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendSmsCsq(sMessageType *psMessage)
{
    unsigned char boError = true;

    TaskGsm_SendAtCmd("MD:>CSQ\r\n","AT+CSQ\r\n",cCSQ2,cCSQ1);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 3);

    switch(ucResp)
    {
    	case 0:
        case 1:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_CSQ;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
            boError = false;
            break;

        case 2:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_PREPARE_SMS_RESP;

			memset(&cGsmSmsBuffer[0],0,RX_BUF_SIZE+1);
			strcpy(&cGsmSmsBuffer[0],&cGsmRxBuffer[0]);

			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			ucGsmTry = 0;
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
//              TaskGsm_SendSmsCcid
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendSmsCcid(sMessageType *psMessage)
{
    unsigned char boError = true;

    TaskGsm_SendAtCmd("MD:>CCID\r\n","AT+CCID\r\n",cModem_ERROR,cModem_OK);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 10);

    switch(ucResp)
    {
    	case 0:
        case 1:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_CCID;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
            boError = false;
            break;

        case 2:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_PREPARE_SMS_RESP;

			memset(&cGsmSmsBuffer[0],0,RX_BUF_SIZE+1);
			strcpy(&cGsmSmsBuffer[0],&cGsmRxBuffer[0]);

			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			ucGsmTry = 0;
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
//              TaskGsm_SendSmsGps
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendSmsGps(sMessageType *psMessage)
{
    unsigned char boError = true;

    ESP_LOGI(GSM_TASK_TAG, "MD:>GPS SMS\r\n");

	stGsmMsg.ucSrc = SRC_GSM;
	stGsmMsg.ucDest = SRC_GSM;
	stGsmMsg.ucEvent = EVENT_GSM_PREPARE_SMS_RESP;

	memset(&cGsmSmsBuffer[0],0,RX_BUF_SIZE+1);
	sprintf(&cGsmSmsBuffer[0],"https://www.google.com/maps/?q=%s,%s",stIo.cLatitude,stIo.cLongitude);
	xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);


    return(boError);
}


//////////////////////////////////////////////
//
//
//              TaskGsm_Alarming
//
//
//////////////////////////////////////////////
extern tstSensorKeylessCode stTelephone[MAX_KEYLESS_SENSOR];
unsigned char TaskGsm_Alarming(sMessageType *psMessage)
{
    unsigned char boError = false;
	struct tm  ts;
	char cLocalBuffer[80];

    ESP_LOGI(GSM_TASK_TAG, "MD:>ALARMING\r\n");

	for(unsigned int i = 0; i < MAX_KEYLESS_SENSOR ; i++)
	{
		if( strcmp("TELEPHONE",stTelephone[i].cId) == 0)
		{
			boError = true;

			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_SMS_ALARMING;

			strcpy(stSmsRecv.cPhoneNumber,"\"");
			strcat(stSmsRecv.cPhoneNumber,(const char*)stTelephone[i].cCode);
			strcat(stSmsRecv.cPhoneNumber,"\"");

			// Format time, "ddd yyyy-mm-dd hh:mm:ss zzz"
			ts = *localtime((time_t*)&ulTimestamp);
			strftime(cLocalBuffer, sizeof(cLocalBuffer), "%a %Y-%m-%d %H:%M:%S %Z", &ts);


			memset(&cGsmSmsBuffer[0],0,RX_BUF_SIZE+1);
			sprintf(&cGsmSmsBuffer[0],"%s disparou alarme %s",psMessage->pcMessageData,cLocalBuffer);
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);

			break;
		}
	}
    return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskGsm_SendSmsAlarming
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendSmsAlarming(sMessageType *psMessage)
{
    unsigned char boError = true;

    ESP_LOGI(GSM_TASK_TAG, "MD:>ALARMING SMS\r\n");

	stGsmMsg.ucSrc = SRC_GSM;
	stGsmMsg.ucDest = SRC_GSM;
	stGsmMsg.ucEvent = EVENT_GSM_PREPARE_SMS_SEND;

	xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);

    return(boError);
}


//////////////////////////////////////////////
//
//
//           TaskGsm_PrepareSmsResponse
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_PrepareSmsResponse(sMessageType *psMessage)
{
    unsigned char boError = true;
    char ucTemp[64];

    memset(&ucTemp[0],0,sizeof(ucTemp));
    sprintf((char*)&ucTemp[0],"AT+CMGS=%s\r\n",&stSmsRecv.cPhoneNumber[0]);

    TaskGsm_SendAtCmd("MD:>CMGS\r\n",&ucTemp[0],">",cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1,10);

    switch(ucResp)
    {
        case 1:
            stGsmMsg.ucSrc = SRC_GSM;
            stGsmMsg.ucDest = SRC_GSM;
            stGsmMsg.ucEvent = EVENT_GSM_SEND_SMS_RESP;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
            boError = true;
        break;

        default:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_CGATT;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			boError = false;
        break;
    }
    return(boError);
}


//////////////////////////////////////////////
//
//
//           TaskGsm_PrepareSmsSending
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_PrepareSmsSending(sMessageType *psMessage)
{
    unsigned char boError = true;
    char ucTemp[64];

    memset(&ucTemp[0],0,sizeof(ucTemp));
    sprintf((char*)&ucTemp[0],"AT+CMGS=%s\r\n",&stSmsRecv.cPhoneNumber[0]);

    TaskGsm_SendAtCmd("MD:>CMGS\r\n",&ucTemp[0],">",cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1,10);

    switch(ucResp)
    {
        case 1:
            stGsmMsg.ucSrc = SRC_GSM;
            stGsmMsg.ucDest = SRC_GSM;
            stGsmMsg.ucEvent = EVENT_GSM_SEND_SMS;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
            boError = true;
        break;

        default:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_CGATT;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			boError = false;
        break;
    }
    return(boError);
}

//////////////////////////////////////////////
//
//
//           TaskGsm_SendSmsResponse
//
//
//////////////////////////////////////////////
#define CTRL_Z 26
unsigned char TaskGsm_SendSmsResponse(sMessageType *psMessage)
{
    unsigned char boError = true;
    unsigned int u16Len;

    u16Len = strlen(cGsmSmsBuffer);
    cGsmSmsBuffer[u16Len]=CTRL_Z;

    TaskGsm_SendAtCmd("MD:>SEND SMS\r\n",cGsmSmsBuffer,"+CMGS:",cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1,20);

    switch(ucResp)
    {
        case 1:
            stGsmMsg.ucSrc = SRC_GSM;
            stGsmMsg.ucDest = SRC_GSM;
            stGsmMsg.ucEvent = EVENT_GSM_DELETE_SMS_MSG;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
            boError = true;
        break;

        default:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_CGATT;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			boError = false;

        break;
    }
    return(boError);

}


//////////////////////////////////////////////
//
//
//           TaskGsm_SendSms
//
//
//////////////////////////////////////////////
#define CTRL_Z 26
unsigned char TaskGsm_SendSms(sMessageType *psMessage)
{
    unsigned char boError = false;
    unsigned int u16Len;

    u16Len = strlen(cGsmSmsBuffer);
    cGsmSmsBuffer[u16Len]=CTRL_Z;

    TaskGsm_SendAtCmd("MD:>SEND SMS\r\n",cGsmSmsBuffer,"+CMGS:",cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1,20);

    switch(ucResp)
    {
        case 1:
        	stGsmMsg.ucSrc = SRC_GSM;
        	stGsmMsg.ucDest = SRC_GSM;
        	stGsmMsg.ucEvent = EVENT_GSM_INIT;
        	xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
            boError = true;
        break;

        case 2:
        	stGsmMsg.ucSrc = SRC_GSM;
        	stGsmMsg.ucDest = SRC_GSM;
        	stGsmMsg.ucEvent = EVENT_GSM_SEND_SMS_ALARMING;
        	xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
            boError = false;
        break;

        default:
        break;
    }
    return(boError);

}

//////////////////////////////////////////////
//
//
//           TaskGsm_DeleteSmsMsg
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_DeleteSmsMsg(sMessageType *psMessage)
{
    unsigned char boError = true;
    SMS_TYPE *ptrSms = &stSmsRecv;

	char* cLocalBuffer = (char*) malloc(RX_BUF_SIZE+1);

    memset(cLocalBuffer,0,RX_BUF_SIZE+1);
    strcpy(cLocalBuffer,"AT+CMGD=");
    strcat(cLocalBuffer,ptrSms->cIndexMsg);
    strcat(cLocalBuffer,",0\r\n");

    TaskGsm_SendAtCmd("MD:>CMGD\r\n",cLocalBuffer,cModem_OK,cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 20);

    switch(ucResp)
    {
        case 1:
        	stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_CGATT;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
            boError = true;

        break;

        default:
        	stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_DELETE_SMS_MSG;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
            boError = false;
        break;
    }

    free(cLocalBuffer);
    return(boError);
}

#if 0
//////////////////////////////////////////////
//
//
//              TaskModem_SendCsq
//
//
//////////////////////////////////////////////
unsigned char TaskModem_SendCsq(sMessageType *psMessage)
{
    unsigned char boError = true;

    TaskGsm_SendAtCmd("CSQ\r\n","AT+CSQ\r\n",cCSQ2,cCSQ1);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 10);

    memset(&cBufferRxModemTemporario[0],0x00,sizeof(cBufferRxModemTemporario));
    strcpy(&cBufferRxModemTemporario[0],(const char*)&cBufferRxModem[0]);

    switch(ucResp)
    {
        case 2:
            if(strlen(cBufferRxModemTemporario)>0)
            {
                if(psMessage->ucSrc == SRC_GSM)
                {
                    stGsmMsg.ucSrc = SRC_GSM;
                    stGsmMsg.ucDest = SRC_GSM;
                    stGsmMsg.ucEvent = EVENT_GSM_PREPARE_SMS_RESP;
                    stGsmMsg.pcMessageData = &cBufferRxModemTemporario[0];
                    queueModem.put(&stGsmMsg);
                }
                else
                {
                    if(psMessage->ucSrc == SRC_BT)
                    {
                        stGsmMsg.ucSrc = SRC_GSM;
                        stGsmMsg.ucDest = SRC_BT;
                        stGsmMsg.ucEvent = EVENT_BT_SEND_RESP;
                        stGsmMsg.pcMessageData = &cBufferRxModemTemporario[0];
                        queueBt.put(&stGsmMsg);
                    }
                }
            }

            boError = true;
        break;

        default:
        if(strlen(cBufferRxModemTemporario)>0)
        {
            if(psMessage->ucSrc == SRC_GSM)
            {
                stGsmMsg.ucSrc = SRC_GSM;
                stGsmMsg.ucDest = SRC_GSM;
                stGsmMsg.ucEvent = EVENT_GSM_PREPARE_SMS_RESP;
                stGsmMsg.pcMessageData = &cBufferRxModemTemporario[0];
                queueModem.put(&stGsmMsg);
            }
            else
            {
                if(psMessage->ucSrc == SRC_BT)
                {
                    stGsmMsg.ucSrc = SRC_GSM;
                    stGsmMsg.ucDest = SRC_BT;
                    stGsmMsg.ucEvent = EVENT_BT_SEND_RESP;
                    stGsmMsg.pcMessageData = &cBufferRxModemTemporario[0];
                    queueBt.put(&stGsmMsg);
                }
            }
        }
        break;
    }
    return(boError);
}
#endif

#if 0
//////////////////////////////////////////////
//
//
//              TaskModem_SendCcid
//
//
//////////////////////////////////////////////
unsigned char TaskModem_SendCcid(sMessageType *psMessage)
{
    unsigned char boError = true;

    TaskGsm_SendAtCmd("CCID\r\n","AT+CCID\r\n",cModem_OK,cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 10);

    memset(&cBufferRxModemTemporario[0],0x00,sizeof(cBufferRxModemTemporario));
    strcpy(&cBufferRxModemTemporario[0],(const char*)&cBufferRxModem[0]);

    switch(ucResp)
    {
        case 1:
            if(strlen(cBufferRxModemTemporario)>0)
            {
                if(psMessage->ucSrc == SRC_GSM)
                {
                    stGsmMsg.ucSrc = SRC_GSM;
                    stGsmMsg.ucDest = SRC_GSM;
                    stGsmMsg.ucEvent = EVENT_GSM_PREPARE_SMS_RESP;
                    stGsmMsg.pcMessageData = &cBufferRxModemTemporario[0];
                    queueModem.put(&stGsmMsg);
                }
                else
                {
                    if(psMessage->ucSrc == SRC_BT)
                    {
                        stGsmMsg.ucSrc = SRC_GSM;
                        stGsmMsg.ucDest = SRC_BT;
                        stGsmMsg.ucEvent = EVENT_BT_SEND_RESP;
                        stGsmMsg.pcMessageData = &cBufferRxModemTemporario[0];
                        queueBt.put(&stGsmMsg);
                    }
                }
            }

            boError = true;
        break;

        default:
            stGsmMsg.ucSrc = SRC_GSM;
            stGsmMsg.ucDest = SRC_GSM;
            stGsmMsg.ucEvent = EVENT_GSM_SEND_CCID;
            stGsmMsg.pcMessageData = "";
            queueModem.put(&stGsmMsg);
        break;
    }
    return(boError);
}
#endif

#if 0
//////////////////////////////////////////////
//
//
//              TaskModem_ReadCsq
//
//
//////////////////////////////////////////////
unsigned char TaskModem_ReadCsq(sMessageType *psMessage)
{
    unsigned char boError = true;

    TaskGsm_SendAtCmd("MD:>CSQ\r\n","AT+CSQ\r\n",cCSQ2,cCSQ1);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 10);

    switch(ucResp)
    {
        case 2:
            stGsmMsg.ucSrc = SRC_GSM;
            stGsmMsg.ucDest = SRC_GSM;
            stGsmMsg.ucEvent = EVENT_GSM_PREPARE_SMS_RESP;
            stGsmMsg.pcMessageData = (psMessage->pcMessageData);
            queueModem.put(&stGsmMsg);
            boError = true;
        break;

        default:
            stGsmMsg.ucSrc = SRC_GSM;
            stGsmMsg.ucDest = SRC_GSM;
            stGsmMsg.ucEvent = EVENT_GSM_READ_CSQ;
            stGsmMsg.pcMessageData = "";

            queueModem.put(&stGsmMsg);
            boError = false;
        break;
    }
    return(boError);
}
#endif




//////////////////////////////////////////////
//
//
//              TaskGsm_WebConnect
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_WebConnect(sMessageType *psMessage)
{
	unsigned char boError = true;

    ESP_LOGI(GSM_TASK_TAG, "MD:>WEB CONNECTED\r\n");

	stGsmMsg.ucSrc = SRC_GSM;
	stGsmMsg.ucDest = SRC_SD;
	stGsmMsg.ucEvent = EVENT_SD_OPENING;
	xQueueSend( xQueueSd, ( void * )&stGsmMsg, 0);

	return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskGsm_OpeningFileOk
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_OpeningFileOk(sMessageType *psMessage)
{
	unsigned char boError = true;

    ESP_LOGI(GSM_TASK_TAG, "MD:>OPENING FILE OK\r\n");

	/*stGsmMsg.ucSrc = SRC_GSM;
	stGsmMsg.ucDest = SRC_GSM;
	stGsmMsg.ucEvent = EVENT_GSM_SEND_START;
	xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);*/


	stGsmMsg.ucSrc = SRC_GSM;
	stGsmMsg.ucDest = SRC_SD;
	stGsmMsg.ucEvent = EVENT_SD_READING;
	xQueueSend( xQueueSd, ( void * )&stGsmMsg, 0);

    ucGsmTry = 0;

    return(boError);
}


//////////////////////////////////////////////
//
//
//              TaskGsm_SendBearerSet1
//
//
//////////////////////////////////////////////
#if 0
unsigned char TaskGsm_SendBearerSet1(sMessageType *psMessage)
{
    unsigned char boError = true;

    TaskGsm_SendAtCmd("MD:>BEARER SET1\r\n","AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"\r\n",cModem_OK,cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 10);

    switch(ucResp)
    {
        case 0:
        case 2:
        	if(++ucGsmTry<=3)
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_SEND_BEARERSET1;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
        	else
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_ERROR;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
            boError = false;
        break;

        case 1:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_BEARERSET2;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			ucGsmTry = 0;
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
//              TaskGsm_SendBearerSet2
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendBearerSet2(sMessageType *psMessage)
{
    unsigned char boError = true;

    memset(cGsmWorkBuffer,0,sizeof(cGsmWorkBuffer));
    sprintf(cGsmWorkBuffer,"AT+SAPBR=3,1,\"APN\",\"%s\"\r\n",cApn);
    TaskGsm_SendAtCmd("MD:>BEARER SET2\r\n",cGsmWorkBuffer,cModem_OK,cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 10);

    switch(ucResp)
    {
        case 0:
        case 2:
        	if(++ucGsmTry<=3)
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_SEND_BEARERSET2;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
        	else
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_ERROR;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
            boError = false;
        break;

        case 1:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_BEARERSET21;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			ucGsmTry = 0;
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
//              TaskGsm_SendBearerSet21
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendBearerSet21(sMessageType *psMessage)
{
    unsigned char boError = true;

    memset(cGsmWorkBuffer,0,sizeof(cGsmWorkBuffer));
    sprintf(cGsmWorkBuffer,"AT+SAPBR=3,1,\"USER\",\"%s\"\r\n",cApnLogin);
    TaskGsm_SendAtCmd("MD:>BEARER SET21\r\n",cGsmWorkBuffer,cModem_OK,cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 10);

    switch(ucResp)
    {
        case 0:
        case 2:
        	if(++ucGsmTry<=3)
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_SEND_BEARERSET21;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
        	else
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_ERROR;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
            boError = false;
        break;

        case 1:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_BEARERSET22;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			ucGsmTry = 0;
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
//              TaskGsm_SendBearerSet22
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendBearerSet22(sMessageType *psMessage)
{
    unsigned char boError = true;

    memset(cGsmWorkBuffer,0,sizeof(cGsmWorkBuffer));
    sprintf(cGsmWorkBuffer,"AT+SAPBR=3,1,\"PWD\",\"%s\"\r\n",cApnPassword);
    TaskGsm_SendAtCmd("MD:>BEARER SET22\r\n",cGsmWorkBuffer,cModem_OK,cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 10);

    switch(ucResp)
    {
        case 0:
        case 2:
        	if(++ucGsmTry<=3)
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_SEND_BEARERSET22;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
        	else
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_ERROR;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
            boError = false;
        break;

        case 1:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = /*EVENT_GSM_SEND_BEARERSET3*//*EVENT_GSM_SEND_CSTT*/EVENT_GSM_SEND_CIICR;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			ucGsmTry = 0;
            boError = true;
        break;

        default:
        break;
    }
    return(boError);
}

#endif

//////////////////////////////////////////////
//
//
//              TaskGsm_SendCiicr
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendCiicr(sMessageType *psMessage)
{
    unsigned char boError = true;

    TaskGsm_SendAtCmd("MD:>CIICR\r\n","AT+CIICR\r\n",cModem_OK,cCIICR1);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 10);

    switch(ucResp)
    {
        case 0:
        case 2:
        	if(++ucGsmTry<=5)
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_SEND_CIICR;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
        	else
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = /*EVENT_GSM_SEND_CSTT*/EVENT_GSM_ERROR;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
            boError = false;
            break;

        case 1:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_CIFSR;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			ucGsmTry = 0;
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
//              TaskGsm_SendCifsr
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendCifsr(sMessageType *psMessage)
{
    unsigned char boError = true;

    TaskGsm_SendAtCmd("MD:>CIFSR\r\n","AT+CIFSR\r\n",".",cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 10);

    switch(ucResp)
    {
        case 0:
        case 2:
        	if(++ucGsmTry <=3)
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_SEND_CIFSR;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
				boError = false;
        	}
        	else
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_ERROR;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
        break;

        case 1:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = /*EVENT_GSM_WEB_CONNECT*/EVENT_GSM_SEND_BEARERSET3;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			ucGsmTry = 0;
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
//              TaskGsm_SendCipshut
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendCipshut(sMessageType *psMessage)
{
    unsigned char boError = true;

    TaskGsm_SendAtCmd("MD:>CIPSHUT\r\n","AT+CIPSHUT\r\n",cModem_OK,cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 10);

    switch(ucResp)
    {
        case 0:
        case 2:
        	if(++ucGsmTry <=3)
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_SEND_CIPSHUT;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
				boError = false;
        	}
        	else
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_ERROR;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
				boError = false;
        	}

        break;

        case 1:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_CIPSTATUS;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
            boError = true;
            ucGsmTry = 0;
        break;

        default:
        break;
    }

    return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskGsm_GetBearer
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_GetBearer(sMessageType *psMessage)
{
    unsigned char boError = true;
#if 0
    TaskGsm_SendAtCmd("MD:>BEARER GET\r\n","AT+SAPBR=2,1\r\n","1,3,\"0.0.0.0\"",cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 10);

    switch(ucResp)
    {
        case 1:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_BEARERSET3;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			boError = false;
        break;

        case 0:
        case 2:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_ERROR;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			ucGsmTry = 0;
            boError = true;
        break;

        default:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_HTTPINIT;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			ucGsmTry = 0;
            boError = true;
        break;

    }
#endif
    return(boError);
}


//////////////////////////////////////////////
//
//
//              TaskGsm_SendBearerSet3
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendBearerSet3(sMessageType *psMessage)
{
    unsigned char boError = true;

    TaskGsm_SendAtCmd("MD:>BEARER SET3\r\n","AT+SAPBR=1,1\r\n",cModem_OK,cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 10);

    switch(ucResp)
    {
        case 0:
        case 2:
        	if(++ucGsmTry <=5)
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_SEND_BEARERSET3;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
        	else
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_ERROR;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
			boError = false;
        break;

        case 1:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_HTTPINIT;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			ucGsmTry = 0;
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
//              TaskGsm_SendBearerSet33
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendBearerSet33(sMessageType *psMessage)
{
    unsigned char boError = true;
#if 0
    TaskGsm_SendAtCmd("MD:>BEARER SET33\r\n","AT+SAPBR=0,1\r\n",cModem_OK,cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 10);

    switch(ucResp)
    {
        case 0:
        case 2:
        	if(++ucGsmTry<=3)
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_SEND_BEARERSET33;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
        	else
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_SEND_BEARERSET1;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
            boError = false;
        break;

        case 1:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_HTTPINIT;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			ucGsmTry = 0;
            boError = true;
        break;

        default:
        break;
    }

#endif
    return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskGsm_SendHttpInit
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendHttpInit(sMessageType *psMessage)
{
    unsigned char boError = true;

    TaskGsm_SendAtCmd("MD:>HTTP INIT\r\n","AT+HTTPINIT\r\n",cModem_OK,cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 10);

    switch(ucResp)
    {
        case 0:
        case 2:
        	if(++ucGsmTry <= 3)
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_SEND_HTTPINIT;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
        	else
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_ERROR;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
            boError = false;
        break;

        case 1:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_HTTPCID;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	ucGsmTry = 0;
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
//              TaskGsm_SendHttpCid
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendHttpCid(sMessageType *psMessage)
{
    unsigned char boError = true;

    TaskGsm_SendAtCmd("MD:>HTTP CID\r\n","AT+HTTPPARA=\"CID\",1\r\n",cModem_OK,cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 10);

    switch(ucResp)
    {
        case 0:
        case 2:
        	if(++ucGsmTry <= 3)
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_SEND_HTTPCID;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
        	else
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_ERROR;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
            boError = false;
        break;

        case 1:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_HTTPURL;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			ucGsmTry  = 0;
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
//              TaskGsm_SendHttpUrl
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendHttpUrl(sMessageType *psMessage)
{
    unsigned char boError = true;
	char* cLocalBuffer = (char*) malloc(RX_BUF_SIZE+1);

    memset(cLocalBuffer,0,RX_BUF_SIZE+1);
    sprintf(cLocalBuffer,"AT+HTTPPARA=\"URL\",\"%s\"\r\n",cWebPage2);
    TaskGsm_SendAtCmd("MD:>HTTP URL\r\n",cLocalBuffer,cModem_OK,cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 10);

    switch(ucResp)
    {
        case 0:
        case 2:
        	if(++ucGsmTry <= 3)
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_SEND_HTTPURL;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
        	else
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_ERROR;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
            boError = false;
        break;

        case 1:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_HTTPCONTENT;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			ucGsmTry = 0;
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
//              TaskGsm_SendHttpContent
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendHttpContent(sMessageType *psMessage)
{
    unsigned char boError = true;
    char* cLocalBuffer = (char*) malloc(RX_BUF_SIZE+1);

    memset(cLocalBuffer,0,RX_BUF_SIZE+1);
    sprintf(cLocalBuffer,"AT+HTTPPARA=\"CONTENT\",\"%s\"\r\n",cHttpContent);
    TaskGsm_SendAtCmd("MD:>HTTP URL\r\n",cLocalBuffer,cModem_OK,cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 10);

    switch(ucResp)
    {
        case 0:
        case 2:
        	if(++ucGsmTry <= 3)
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_SEND_HTTPCONTENT;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
			else
			{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_ERROR;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			}
            boError = false;
        break;

        case 1:
			/*stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_HTTPPREPAREDATA;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
            boError = true;*/

			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_WEB_CONNECT;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			ucGsmTry = 0;
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
//              TaskGsm_SendHttpPrepareData
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendHttpPrepareData(sMessageType *psMessage)
{
    unsigned char boError = true;
	unsigned int u16PayloadLen;
    char* cLocalBuffer = (char*) malloc(RX_BUF_SIZE+1);

    memset(cLocalBuffer,0,RX_BUF_SIZE+1);
	u16PayloadLen = strlen(cConfigAndData);

    sprintf(cLocalBuffer,"AT+HTTPDATA=%d,5000\r\n",u16PayloadLen);
    TaskGsm_SendAtCmd("MD:>HTTP URL\r\n",(const char*)&cLocalBuffer[0],"DOWNLOAD",cModem_ERROR);

    unsigned char ucResp = 0;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 20);

    switch(ucResp)
    {
        case 0:
        case 2:
        {
        	if(++ucGsmTry <=3)
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_SEND_HTTPPREPAREDATA;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
        	else
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = /*EVENT_GSM_SEND_HTTPTERM*/EVENT_GSM_ERROR;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
            boError = false;
        }
        break;

        case 1:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_HTTPDATA;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			ucGsmTry = 0;
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
//              TaskGsm_SendHttpData
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendHttpData(sMessageType *psMessage)
{
    unsigned char boError = true;
    char* cLocalBuffer = (char*) malloc(RX_BUF_SIZE+1);


/*	stGsmMsg.ucSrc = SRC_GSM;
	stGsmMsg.ucDest = SRC_DEBUG;
	stGsmMsg.ucEvent = EVENT_IO_GSM_COMMUNICATING;
	xQueueSend( xQueueDebug, ( void * )&stGsmMsg, 0);*/

    memset(cLocalBuffer,0,RX_BUF_SIZE+1);
    strcpy(cLocalBuffer,cConfigAndData);
    TaskGsm_SendAtCmd("MD:>DATA\r\n",cLocalBuffer,cModem_OK,cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 20);

    switch(ucResp)
    {
        case 0:
        case 2:
        	if(++ucGsmTry <= 3)
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_SEND_HTTPPREPAREDATA;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
        	else
        	{
    			stGsmMsg.ucSrc = SRC_GSM;
    			stGsmMsg.ucDest = SRC_GSM;
    			stGsmMsg.ucEvent = EVENT_GSM_ERROR;
    			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
            boError = false;
        break;

        case 1:
        	stGsmMsg.ucSrc = SRC_GSM;
        	stGsmMsg.ucDest = SRC_GSM;
        	stGsmMsg.ucEvent = EVENT_GSM_SEND_HTTPACTION;
        	xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);

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
//              TaskGsm_SendHttpSsl
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendHttpSsl(sMessageType *psMessage)
{
    unsigned char boError = true;
#if 0
    TaskGsm_SendAtCmd("MD:>HTTP SSL\r\n","AT+HTTPSSL=0\r\n",cModem_OK,cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 10);

    switch(ucResp)
    {
        case 0:
        case 2:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_HTTPSSL;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
            boError = false;
        break;

        case 1:
			stGsmMsg.ucSrc = SRC_GSM;
			stGsmMsg.ucDest = SRC_GSM;
			stGsmMsg.ucEvent = EVENT_GSM_SEND_HTTPACTION;
			xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
			ucGsmTry = 0;
            boError = true;
        break;

        default:
        break;
    }
#endif
    return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskGsm_SendHttpAction
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendHttpAction(sMessageType *psMessage)
{
    unsigned char boError = true;
    static unsigned char ucGsmTryAction = 0;

    /*gucQtyItemsTx = 6*gucQtyItemsTx;
    memset(cGsmWorkBuffer,0,sizeof(cGsmWorkBuffer));
    sprintf(cGsmWorkBuffer,"1,200,%d",gucQtyItemsTx);*/

    TaskGsm_SendAtCmd("MD:>HTTP ACTION\r\n","AT+HTTPACTION=1\r\n","1,200,6"/*(const char*)&cGsmWorkBuffer[0]*/,cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 10);


    switch(ucResp)
    {
        case 0:
        case 2:
        	if(++ucGsmTryAction<=5)
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_SEND_HTTPPREPAREDATA;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
        	else
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_ERROR;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
				ucGsmTryAction = 0;
        	}
			boError = false;
        break;

        case 1:
        	stGsmMsg.ucSrc = SRC_GSM;
        	stGsmMsg.ucDest = SRC_SD;
        	stGsmMsg.ucEvent = EVENT_SD_MARKING;
        	xQueueSend( xQueueSd, ( void * )&stGsmMsg, 0);

        /*	stGsmMsg.ucSrc = SRC_GSM;
        	stGsmMsg.ucDest = SRC_DEBUG;
        	stGsmMsg.ucEvent = EVENT_IO_GSM_UPLOAD_DONE;
        	xQueueSend( xQueueDebug, ( void * )&stGsmMsg, 0);*/

        	ucGsmTryAction = 0;
        	boError = true;
        break;

        default:
			ucGsmTryAction = 0;
        break;
    }

    return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskGsm_SendHttpRead
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendHttpRead(sMessageType *psMessage)
{
    unsigned char boError = true;
#if 0
    TaskGsm_SendAtCmd("MD:>HTTP READ\r\n","AT+HTTPREAD\r\n",cModem_ACK,cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 10);

    switch(ucResp)
    {
        case 0:
        case 2:
        	if(++ucGsmTry <=3)
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_SEND_HTTPREAD;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
        	else
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_ERROR;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
        	boError = false;
        break;

        case 1:
        	stGsmMsg.ucSrc = SRC_GSM;
        	stGsmMsg.ucDest = SRC_SD;
        	stGsmMsg.ucEvent = EVENT_SD_MARKING;
        	xQueueSend( xQueueSd, ( void * )&stGsmMsg, 0);

        	stGsmMsg.ucSrc = SRC_GSM;
        	stGsmMsg.ucDest = SRC_DEBUG;
        	stGsmMsg.ucEvent = EVENT_IO_GSM_UPLOAD_DONE;
        	xQueueSend( xQueueDebug, ( void * )&stGsmMsg, 0);
        	boError = true;
        break;

        default:
        break;
    }
#endif
    return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskGsm_SendHttpTerm
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendHttpTerm(sMessageType *psMessage)
{
    unsigned char boError = true;
#if 0
    TaskGsm_SendAtCmd("MD:>HTTP TERM\r\n","AT+HTTPTERM\r\n",cModem_OK,cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 10);

    switch(ucResp)
    {
        case 0:
        case 2:
        	if(++ucGsmTry <=2)
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_SEND_HTTPTERM;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
				boError = false;
        	}
        	else
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_ERROR;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
        break;

        case 1:
        	stGsmMsg.ucSrc = SRC_GSM;
        	stGsmMsg.ucDest = SRC_GSM;
        	stGsmMsg.ucEvent = EVENT_GSM_SEND_BEARERSET4;
        	xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        break;

        default:
        break;
    }
#endif
    return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskGsm_SendBearerSet4
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_SendBearerSet4(sMessageType *psMessage)
{
    unsigned char boError = true;
#if 0
    TaskGsm_SendAtCmd("MD:>BEARER SET4\r\n","AT+SAPBR=0,1\r\n",cModem_OK,cModem_ERROR);

    unsigned char ucResp;
    ucResp = TaskGsm_WaitResponse(TaskGsm_ParseResp,1, 10);

    switch(ucResp)
    {
        case 0:
        case 2:
        	if(++ucGsmTry <=2)
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_SEND_BEARERSET4;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
				boError = false;
        	}
        	else
        	{
				stGsmMsg.ucSrc = SRC_GSM;
				stGsmMsg.ucDest = SRC_GSM;
				stGsmMsg.ucEvent = EVENT_GSM_ERROR;
				xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        	}
        break;

        case 1:
        	stGsmMsg.ucSrc = SRC_GSM;
        	stGsmMsg.ucDest = SRC_GSM;
        	stGsmMsg.ucEvent = EVENT_GSM_SEND_BEARERSET1;
        	xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);
        break;

        default:
        break;
    }
#endif
    return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskGsm_DataOk
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_DataOk(sMessageType *psMessage)
{
	unsigned char boError = true;

    ESP_LOGI(GSM_TASK_TAG, "MD:>DATA OK\r\n");

	return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskGsm_Ending
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_Ending(sMessageType *psMessage)
{
	unsigned char boError = true;

    ESP_LOGI(GSM_TASK_TAG, "MD:>ENDING\r\n");

	vTaskDelay(2000/portTICK_PERIOD_MS);

	stGsmMsg.ucSrc = SRC_GSM;
	stGsmMsg.ucDest = SRC_SD;
	stGsmMsg.ucEvent = EVENT_SD_OPENING;
	xQueueSend( xQueueSd, ( void * )&stGsmMsg, 0);

	return(boError);
}


//////////////////////////////////////////////
//
//
//              TaskGsm_Error
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_Error(sMessageType *psMessage)
{
	unsigned char boError = true;

    ESP_LOGI(GSM_TASK_TAG, "MD:>ERROR\r\n");

	stGsmMsg.ucSrc = SRC_GSM;
	stGsmMsg.ucDest = SRC_GSM;
	stGsmMsg.ucEvent = EVENT_GSM_INIT;
	xQueueSend( xQueueGsm, ( void * )&stGsmMsg, 0);


	return(boError);
}
//////////////////////////////////////////////
//
//
//              TaskGsm_IgnoreEvent
//
//
//////////////////////////////////////////////
unsigned char TaskGsm_IgnoreEvent(sMessageType *psMessage)
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
static sStateMachineType const gasTaskGsm_Idling[] =
{
    /* Event        Action routine      Next state */
    //  State specific transitions
	{EVENT_GSM_INIT,          TaskGsm_Init,                 TASKGSM_IDLING,           		TASKGSM_IDLING        				},
	{EVENT_GSM_SETBAUD,       TaskGsm_SetBaudRate,      	TASKGSM_IDLING,					TASKGSM_IDLING  					},
    {EVENT_GSM_AT,            TaskGsm_SendAt,           	TASKGSM_CHECKINGNETWORK,   		TASKGSM_IDLING  					},
	{EVENT_GSM_ERROR,	      TaskGsm_Error,				TASKGSM_IDLING,  			  	TASKGSM_IDLING						},
    {EVENT_GSM_NULL,          TaskGsm_IgnoreEvent,          TASKGSM_IDLING,					TASKGSM_IDLING						}
};

static sStateMachineType const gasTaskGsm_CheckingNetwork[] =
{
    /* Event        Action routine      Next state */
    //  State specific transitions

    /*{EVENT_GSM_SEND_CPIN,     TaskGsm_SendCpin,         TASKGSM_INITIALIZING,         TASKGSM_INITIALIZING  },*/
    /*{EVENT_GSM_SEND_CSQ,      TaskGsm_SendCsq,          TASKGSM_INITIALIZING,         TASKGSM_INITIALIZING  },*/
	{EVENT_GSM_SEND_CTZU,      	TaskGsm_SendCtzu,    	  TASKGSM_CHECKINGNETWORK,     	TASKGSM_CHECKINGNETWORK	},
	{EVENT_GSM_SEND_CCLK,      	TaskGsm_SendCclk,         TASKGSM_CHECKINGNETWORK,     	TASKGSM_CHECKINGNETWORK	},
    {EVENT_GSM_SEND_CSQ,      	TaskGsm_SendCsq,          TASKGSM_INITIALIZING,       	TASKGSM_CHECKINGNETWORK	},
	{EVENT_GSM_ERROR,	      	TaskGsm_Error,			  TASKGSM_CHECKINGNETWORK,  	TASKGSM_CHECKINGNETWORK	},
	{EVENT_GSM_NULL,          	TaskGsm_IgnoreEvent,      TASKGSM_CHECKINGNETWORK,		TASKGSM_CHECKINGNETWORK	}
};

static sStateMachineType const gasTaskGsm_Initializing[] =
{
    /* Event        Action routine      Next state */
    //  State specific transitions

    {EVENT_GSM_SELECT_SMS_FORMAT,   	TaskGsm_SelectSmsFormat,			TASKGSM_INITIALIZING,       TASKGSM_INITIALIZING  	},
    {EVENT_GSM_LIST_SMS_MSG,      		TaskGsm_ListSmsMsg,					TASKGSM_SMSREADING,       	TASKGSM_INITIALIZING  	},

    {EVENT_GSM_ALARMING,  				TaskGsm_Alarming,					TASKGSM_SMSSENDING,       	TASKGSM_INITIALIZING  	},

  /*{EVENT_GSM_SEND_CGATT,    			TaskGsm_SendCgatt,      			TASKGSM_INITIALIZING,       TASKGSM_INITIALIZING  	},
    {EVENT_GSM_SEND_CGDCONT,  			TaskGsm_SendCgdcont,      			TASKGSM_INITIALIZING,       TASKGSM_INITIALIZING  	},
    {EVENT_GSM_SEND_CIPSHUT,  			TaskGsm_SendCipshut,     			TASKGSM_INITIALIZING,       TASKGSM_INITIALIZING  	},

    {EVENT_GSM_SEND_CIPSTATUS,			TaskGsm_SendCipStatus,      		TASKGSM_INITIALIZING,       TASKGSM_INITIALIZING 	},
    {EVENT_GSM_SEND_CIPMUX,   			TaskGsm_SendCipMux,       			TASKGSM_INITIALIZING,       TASKGSM_INITIALIZING  	},
    {EVENT_GSM_SEND_CSTT,     			TaskGsm_SendCstt,					TASKGSM_COMMUNICATING,    	TASKGSM_INITIALIZING  	},

    {EVENT_GSM_SEND_CSTT,     			TaskGsm_SendCstt,         			TASKGSM_INITIALIZING,         TASKGSM_INITIALIZING  },
    {EVENT_GSM_SEND_CIICR,    			TaskGsm_SendCiicr,        			TASKGSM_INITIALIZING,         TASKGSM_INITIALIZING  },
	{EVENT_GSM_SEND_CIFSR,    			TaskGsm_SendCifsr,        			TASKGSM_INITIALIZING,         TASKGSM_INITIALIZING  },
	{EVENT_GSM_WEB_CONNECT,   			TaskGsm_WebConnect,       			TASKGSM_COMMUNICATING,        TASKGSM_INITIALIZING  },*/

	{EVENT_GSM_ERROR,	      			TaskGsm_Error,						TASKGSM_IDLING,  			  TASKGSM_IDLING		},
	{EVENT_GSM_NULL,          			TaskGsm_IgnoreEvent,      			TASKGSM_INITIALIZING,		  TASKGSM_INITIALIZING	}
};

static sStateMachineType const gasTaskGsm_Communicating[] =
{

    /* Event        Action routine      Next state */
    //  State specific transitions
	{EVENT_GSM_SEND_CIICR,    		TaskGsm_SendCiicr,        	TASKGSM_COMMUNICATING,    	TASKGSM_COMMUNICATING },
	{EVENT_GSM_SEND_CIFSR,    		TaskGsm_SendCifsr,        	TASKGSM_COMMUNICATING,    	TASKGSM_COMMUNICATING },

	{EVENT_GSM_WEB_CONNECT,   		TaskGsm_WebConnect,       	TASKGSM_COMMUNICATING,      TASKGSM_COMMUNICATING },
    {EVENT_GSM_OPENING_FILE_OK,   	TaskGsm_OpeningFileOk,    	TASKGSM_COMMUNICATING,    	TASKGSM_COMMUNICATING },

    {EVENT_GSM_ALARMING,  			TaskGsm_Alarming,			TASKGSM_SMSSENDING,       	TASKGSM_COMMUNICATING },
/*	{EVENT_GSM_SEND_BEARERSET1,	  	TaskGsm_SendBearerSet1,		TASKGSM_COMMUNICATING,    	TASKGSM_COMMUNICATING },
	{EVENT_GSM_SEND_BEARERSET2,	  	TaskGsm_SendBearerSet2,		TASKGSM_COMMUNICATING,    	TASKGSM_COMMUNICATING },
	{EVENT_GSM_SEND_BEARERSET21,  	TaskGsm_SendBearerSet21,	TASKGSM_COMMUNICATING,    	TASKGSM_COMMUNICATING },
	{EVENT_GSM_SEND_BEARERSET22,  	TaskGsm_SendBearerSet22,	TASKGSM_COMMUNICATING,    	TASKGSM_COMMUNICATING },
*/

	{EVENT_GSM_SEND_BEARERSET3,	  	TaskGsm_SendBearerSet3,		TASKGSM_COMMUNICATING,    	TASKGSM_COMMUNICATING },
	/*{EVENT_GSM_GET_BEARER,	  	  TaskGsm_GetBearer,			TASKGSM_COMMUNICATING,    	TASKGSM_COMMUNICATING },*/
	/*{EVENT_GSM_SEND_BEARERSET33,  TaskGsm_SendBearerSet33,		TASKGSM_COMMUNICATING,    	TASKGSM_COMMUNICATING },*/

	{EVENT_GSM_SEND_HTTPINIT,	  	TaskGsm_SendHttpInit,		TASKGSM_COMMUNICATING,    	TASKGSM_COMMUNICATING },
	{EVENT_GSM_SEND_HTTPCID,	  	TaskGsm_SendHttpCid,		TASKGSM_COMMUNICATING, 		TASKGSM_COMMUNICATING },
	{EVENT_GSM_SEND_HTTPURL,	  	TaskGsm_SendHttpUrl,		TASKGSM_COMMUNICATING, 		TASKGSM_COMMUNICATING},

	{EVENT_GSM_SEND_HTTPCONTENT,  	TaskGsm_SendHttpContent,		TASKGSM_COMMUNICATING,    	TASKGSM_COMMUNICATING },
	{EVENT_GSM_SEND_HTTPPREPAREDATA,TaskGsm_SendHttpPrepareData,	TASKGSM_COMMUNICATING, 		TASKGSM_COMMUNICATING },
	{EVENT_GSM_SEND_HTTPDATA,		TaskGsm_SendHttpData,			TASKGSM_COMMUNICATING, 		TASKGSM_COMMUNICATING},
	{EVENT_GSM_SEND_HTTPSSL,		TaskGsm_SendHttpSsl,			TASKGSM_COMMUNICATING, 		TASKGSM_COMMUNICATING},
	{EVENT_GSM_SEND_HTTPACTION,		  TaskGsm_SendHttpAction,		TASKGSM_COMMUNICATING, 		TASKGSM_COMMUNICATING},
	/*{EVENT_GSM_SEND_HTTPREAD,		  TaskGsm_SendHttpRead,			TASKGSM_COMMUNICATING, 		TASKGSM_COMMUNICATING},
	{EVENT_GSM_SEND_HTTPTERM,		  TaskGsm_SendHttpTerm,			TASKGSM_COMMUNICATING, 		TASKGSM_COMMUNICATING},
	{EVENT_GSM_SEND_BEARERSET4,		  TaskGsm_SendBearerSet4,			TASKGSM_COMMUNICATING, 		TASKGSM_COMMUNICATING},*/

    /*{EVENT_GSM_LIST_SMS_MSG,        TaskGsm_ListSmsMsg,			TASKGSM_SMSREADING,       			TASKGSM_COMMUNICATING  	},*/
	{EVENT_GSM_ENDING,			  	TaskGsm_Ending,					TASKGSM_COMMUNICATING,  			TASKGSM_IDLING			},
	{EVENT_GSM_ERROR,			  	TaskGsm_Error,					TASKGSM_IDLING,  					TASKGSM_IDLING			},
    {EVENT_GSM_NULL,              	TaskGsm_IgnoreEvent,      		TASKGSM_COMMUNICATING,	  			TASKGSM_COMMUNICATING	}

};

static sStateMachineType const gasTaskGsm_SmsReading[] =
{

    /* Event        Action routine      Next state */
    //  State specific transitions
	{EVENT_GSM_ALARMING,  				TaskGsm_Alarming,					TASKGSM_SMSSENDING,       TASKGSM_COMMUNICATING     },
	{EVENT_GSM_DECODE_SMS_MSG,      	TaskGsm_DecodeSmsMsg,				TASKGSM_SMSREADING,       TASKGSM_INITIALIZING  	},
	{EVENT_GSM_SEND_SMS_CSQ,      		TaskGsm_SendSmsCsq,					TASKGSM_SMSREADING,       TASKGSM_INITIALIZING  	},
    {EVENT_GSM_SEND_CSQ,      			TaskGsm_SendCsq,          			TASKGSM_SMSREADING,       TASKGSM_INITIALIZING  	},

	{EVENT_GSM_SEND_SMS_CCID,      		TaskGsm_SendSmsCcid,				TASKGSM_SMSREADING,       TASKGSM_INITIALIZING  	},
	{EVENT_GSM_SEND_SMS_GPS,      		TaskGsm_SendSmsGps,					TASKGSM_SMSREADING,       TASKGSM_INITIALIZING  	},

	{EVENT_GSM_PREPARE_SMS_RESP,      	TaskGsm_PrepareSmsResponse,			TASKGSM_SMSREADING,       TASKGSM_INITIALIZING  	},
	{EVENT_GSM_SEND_SMS_RESP,      		TaskGsm_SendSmsResponse,			TASKGSM_SMSREADING,       TASKGSM_INITIALIZING  	},
	{EVENT_GSM_DELETE_SMS_MSG,      	TaskGsm_DeleteSmsMsg,				TASKGSM_INITIALIZING,     TASKGSM_INITIALIZING  	},

	{EVENT_GSM_ERROR,			  		TaskGsm_Error,						TASKGSM_IDLING,  			TASKGSM_IDLING			},
    {EVENT_GSM_NULL,              		TaskGsm_IgnoreEvent,      			TASKGSM_SMSREADING,	  		TASKGSM_SMSREADING		}
};

static sStateMachineType const gasTaskGsm_SmsSending[] =
{

    /* Event        Action routine      Next state */
    //  State specific transitions
    {EVENT_GSM_SEND_SMS_ALARMING,  		TaskGsm_SendSmsAlarming,			TASKGSM_SMSSENDING,       TASKGSM_INITIALIZING  	},
	{EVENT_GSM_PREPARE_SMS_SEND,      	TaskGsm_PrepareSmsSending,			TASKGSM_SMSSENDING,       TASKGSM_INITIALIZING  	},
	{EVENT_GSM_SEND_SMS,      			TaskGsm_SendSms,					TASKGSM_IDLING,     	  TASKGSM_SMSSENDING	  	},

	{EVENT_GSM_ERROR,			  		TaskGsm_Error,						TASKGSM_SMSSENDING,  	  TASKGSM_SMSSENDING		},
    {EVENT_GSM_NULL,              		TaskGsm_IgnoreEvent,      			TASKGSM_SMSSENDING,	  	  TASKGSM_SMSSENDING		}
};


static sStateMachineType const * const gpasTaskGsm_StateMachine[] =
{
    gasTaskGsm_Idling,
    gasTaskGsm_CheckingNetwork,
	gasTaskGsm_Initializing,
	gasTaskGsm_Communicating,
	gasTaskGsm_SmsReading,
	gasTaskGsm_SmsSending
};

static unsigned char ucCurrentStateGsm = TASKGSM_IDLING;
void vTaskGsm( void *pvParameters )
{
	TickType_t elapsed_time;


	for( ;; )
	{
		elapsed_time = xTaskGetTickCount();
		if( xQueueReceive( xQueueGsm, &(stGsmMsg ),0 ) )
		{
            (void)eEventHandler ((unsigned char)SRC_GSM,gpasTaskGsm_StateMachine[ucCurrentStateGsm], &ucCurrentStateGsm, &stGsmMsg);
		}

		vTaskDelayUntil(&elapsed_time, 500 / portTICK_PERIOD_MS);
	}
}
