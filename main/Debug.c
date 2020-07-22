/*
 * Debug.c
 *
 *  Created on: 24/09/2018
 *      Author: danilo
 */

/* Kernel includes. */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"

#include <esp_system.h>
#include <esp_spi_flash.h>
#include <rom/spi_flash.h>
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"

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
void vTaskDebug( void *pvParameters );

//////////////////////////////////////////////
//
//
//            VARIABLES
//
//
//////////////////////////////////////////////
static uint32_t ulCountPulseGsm = 0;
static uint32_t ulCountPeriodGsm = 0;

static uint32_t ulQtyPulseGsm = 0;
static uint32_t ulPeriodGsm = 0;

static uint32_t ulCountPulseOut1 = 0;
static uint32_t ulCountPeriodOut1 = 0;

static uint32_t ulQtyPulseOut1 = 0;
static uint32_t ulPeriodOut1 = 0;

sMessageType stDebugMsg;
static const char *DEBUG_TASK_TAG = "DEBUG_TASK";

//////////////////////////////////////////////
//
//
//              Debug_Io_Configuration
//
//
//////////////////////////////////////////////
void Debug_Io_Configuration(void)
{
	/////////////////
	// GSM DIAG PIN
	/////////////////
	gpio_config_t io_conf;
	//disable interrupt
	io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
	//set as output mode
	io_conf.mode = GPIO_MODE_OUTPUT;
	//bit mask of the pins that you want to set,e.g.GPIO18/19
	io_conf.pin_bit_mask = GPIO_OUTPUT_GSM_DIAG_PIN_SEL | GPIO_OUTPUT_GSM_DIAG_EXT_PIN_SEL;
	//disable pull-down mode
	io_conf.pull_down_en = 0;
	//disable pull-up mode
	io_conf.pull_up_en = 0;
	//configure GPIO with the given settings
	gpio_config(&io_conf);

	/////////////////
	//  	OUT1
	/////////////////

	//disable interrupt
	io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
	//set as output mode
	io_conf.mode = GPIO_MODE_OUTPUT;
	//bit mask of the pins that you want to set,e.g.GPIO18/19
	io_conf.pin_bit_mask = GPIO_OUTPUT_OUT1_PIN_SEL;
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
//              Debug_Init
//
//
//////////////////////////////////////////////
void DebugInit(void)
{
	/*esp_log_level_set(DEBUG_TASK_TAG, ESP_LOG_INFO);*/

    xQueueDebug = xQueueCreate(sdQUEUE_LENGTH,			/* The number of items the queue can hold. */
							sizeof( sMessageType ) );	/* The size of each item the queue holds. */


    xTaskCreate(vTaskDebug, "vTaskDebug", 1024*2, NULL, configMAX_PRIORITIES, NULL);
	/* Create the queue used by the queue send and queue receive tasks.
	http://www.freertos.org/a00116.html */


    stDebugMsg.ucSrc = SRC_DEBUG;
    stDebugMsg.ucDest = SRC_DEBUG;
    stDebugMsg.ucEvent = EVENT_IO_GSM_INIT;
	xQueueSend( xQueueDebug, ( void * )&stDebugMsg, 0);


	Debug_Io_Configuration();
}


//////////////////////////////////////////////
//
//
//              TaskDebug_Out1_Init
//
//
//////////////////////////////////////////////
unsigned char TaskDebug_Out1_Init(sMessageType *psMessage)
{
    unsigned char boError = true;

	gpio_set_level(GPIO_OUTPUT_OUT1, 0);
	gpio_set_level(GPIO_OUTPUT_GSM_DIAG, 0);
	gpio_set_level(GPIO_OUTPUT_GSM_DIAG_EXT, 0);


    return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskDebug_Out1_Arming
//
//
//////////////////////////////////////////////
unsigned char TaskDebug_Out1_Arming(sMessageType *psMessage)
{
    unsigned char boError = true;

    ESP_LOGI(DEBUG_TASK_TAG, "ARMING\r\n");

	gpio_set_level(GPIO_OUTPUT_OUT1, 0);

    ulCountPulseOut1 = 0;
    ulCountPeriodOut1 = 0;

	ulQtyPulseOut1 = 1;
	ulPeriodOut1 = 1;


	gpio_set_level(GPIO_OUTPUT_GSM_DIAG, 0);
	gpio_set_level(GPIO_OUTPUT_GSM_DIAG_EXT, 0);

    ulCountPulseGsm = 0;
    ulCountPeriodGsm = 0;

	ulQtyPulseGsm = 1;
	ulPeriodGsm = 1;


    return(boError);
}

//////////////////////////////////////////////
//
//
//         TaskDebug_Out1_Disarming
//
//
//////////////////////////////////////////////
unsigned char TaskDebug_Out1_Disarming(sMessageType *psMessage)
{
    unsigned char boError = true;

    ESP_LOGI(DEBUG_TASK_TAG, "DISARMING\r\n");

	gpio_set_level(GPIO_OUTPUT_OUT1, 0);

    ulCountPulseOut1 = 0;
    ulCountPeriodOut1 = 0;

	ulQtyPulseOut1 = 2;
	ulPeriodOut1 = 4;

	gpio_set_level(GPIO_OUTPUT_GSM_DIAG, 0);
	gpio_set_level(GPIO_OUTPUT_GSM_DIAG_EXT, 0);

    ulCountPulseGsm = 0;
    ulCountPeriodGsm = 0;

	ulQtyPulseGsm = 2;
	ulPeriodGsm = 4;


    return(boError);
}


//////////////////////////////////////////////
//
//
//         TaskDebug_Out1_Alarming
//
//
//////////////////////////////////////////////
unsigned char TaskDebug_Out1_Alarming(sMessageType *psMessage)
{
    unsigned char boError = true;

    ESP_LOGI(DEBUG_TASK_TAG, "ALARMING\r\n");

	gpio_set_level(GPIO_OUTPUT_OUT1, 0);

    ulCountPulseOut1 = 0;
    ulCountPeriodOut1 = 0;

	ulQtyPulseOut1 = /*0xFFFFFFFF;*/0;
	ulPeriodOut1 = /*0xFFFFFFFF;*/0;


	gpio_set_level(GPIO_OUTPUT_GSM_DIAG, 0);
	gpio_set_level(GPIO_OUTPUT_GSM_DIAG_EXT, 0);

    ulCountPulseGsm = 0;
    ulCountPeriodGsm = 0;

	ulQtyPulseGsm = 0xFFFFFFFF;
	ulPeriodGsm = 0xFFFFFFFF;


    return(boError);
}


//////////////////////////////////////////////
//
//
//          	vHandleOut1
//
//
//////////////////////////////////////////////
void vHandleOut1 (void)
 {
	static unsigned char ucOnState = true;

    if(ulCountPeriodOut1 < ulPeriodOut1)
    {
    	if(ulCountPulseOut1 < ulQtyPulseOut1)
    	{
    		if(ucOnState != false)
    		{
				gpio_set_level(GPIO_OUTPUT_OUT1, 1);

				ulCountPulseOut1++;
				ucOnState = false;
    		}
    		else
    		{
    			gpio_set_level(GPIO_OUTPUT_OUT1, 0);

				ucOnState = true;
    		}
    	}
    	else
    	{
			gpio_set_level(GPIO_OUTPUT_OUT1, 0);

			ucOnState = true;
    	}
    	ulCountPeriodOut1++;
    }
    else
    {
    	ulQtyPulseOut1 = 0;
    	ulPeriodOut1 = 0;

        ulCountPulseOut1 = 0;
        ulCountPeriodOut1 = 0;

		gpio_set_level(GPIO_OUTPUT_OUT1, 0);

		ucOnState = true;
    }
 }


//////////////////////////////////////////////
//
//
//              TaskDebug_Gsm_Init
//
//
//////////////////////////////////////////////
unsigned char TaskDebug_Gsm_Init(sMessageType *psMessage)
{
    unsigned char boError = true;

	gpio_set_level(GPIO_OUTPUT_GSM_DIAG, 0);
	gpio_set_level(GPIO_OUTPUT_GSM_DIAG_EXT, 0);


    return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskDebug_Gsm_Connecting
//
//
//////////////////////////////////////////////
unsigned char TaskDebug_Gsm_Connecting(sMessageType *psMessage)
{
    unsigned char boError = true;

	gpio_set_level(GPIO_OUTPUT_GSM_DIAG, 0);
	gpio_set_level(GPIO_OUTPUT_GSM_DIAG_EXT, 0);

    ulCountPulseGsm = 0;
    ulCountPeriodGsm = 0;

	ulQtyPulseGsm = 1;		/* 2 pulses - 2 ON + 2 OFF (200ms + 200ms)	*/
	ulPeriodGsm = 11; 		/* 12 * 100ms = 1,2s		*/

    return(boError);
}
//////////////////////////////////////////////
//
//
//              TaskDebug_Gsm_Communicating
//
//
//////////////////////////////////////////////
unsigned char TaskDebug_Gsm_Communicating(sMessageType *psMessage)
{
    unsigned char boError = true;

	gpio_set_level(GPIO_OUTPUT_GSM_DIAG, 0);
	gpio_set_level(GPIO_OUTPUT_GSM_DIAG_EXT, 0);

    ulCountPulseGsm = 0;
    ulCountPeriodGsm = 0;

	ulQtyPulseGsm = 2;		/* 5 pulses - 5 ON + 5 OFF (500ms + 500ms)	*/
	ulPeriodGsm = 10; 		/* 30 * 100ms = 3s		*/

    return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskDebug_Gsm_UploadDone
//
//
//////////////////////////////////////////////
unsigned char TaskDebug_Gsm_UploadDone(sMessageType *psMessage)
{
    unsigned char boError = true;

	gpio_set_level(GPIO_OUTPUT_GSM_DIAG, 0);
	gpio_set_level(GPIO_OUTPUT_GSM_DIAG_EXT, 0);

    ulCountPulseGsm = 0;
    ulCountPeriodGsm = 0;

	ulQtyPulseGsm = 3;		/* 1 pulses - 10 ON + 0 OFF (1000ms)	*/
	ulPeriodGsm = 10; 		/* 20 * 100ms = 2s		*/

    return(boError);
}
//////////////////////////////////////////////
//
//
//              vTimerCallbackSleep
//
//
//////////////////////////////////////////////
#if 0
void vTimerCallbackSleep( xTimerHandle xTimer )
 {
    if(ulCountPeriodSleep <= u32TimeToSleep)
    {
    	ulCountPeriodSleep++;
    }
    else
    {
    	stDebugMsg.ucSrc = SRC_DEBUG;
    	stDebugMsg.ucDest = SRC_DEBUG;
    	stDebugMsg.ucEvent = EVENT_IO_SLEEPING;
		xQueueSend( xQueueDebug, ( void * )&stDebugMsg, 0);
    }
 }
#endif
//////////////////////////////////////////////
//
//
//          	vHandleGsmDiag
//
//
//////////////////////////////////////////////
void vHandleGsmDiag (void)
 {
	static unsigned char ucOnState = true;

    if(ulCountPeriodGsm < ulPeriodGsm)
    {
    	if(ulCountPulseGsm < ulQtyPulseGsm)
    	{
    		if(ucOnState != false)
    		{
				gpio_set_level(GPIO_OUTPUT_GSM_DIAG, 1);
				gpio_set_level(GPIO_OUTPUT_GSM_DIAG_EXT, 1);
				ulCountPulseGsm++;
				ucOnState = false;
    		}
    		else
    		{
    			gpio_set_level(GPIO_OUTPUT_GSM_DIAG, 0);
				gpio_set_level(GPIO_OUTPUT_GSM_DIAG_EXT, 0);
				ucOnState = true;
    		}
    	}
    	else
    	{
			gpio_set_level(GPIO_OUTPUT_GSM_DIAG, 0);
			gpio_set_level(GPIO_OUTPUT_GSM_DIAG_EXT, 0);
			ucOnState = true;
    	}
    	ulCountPeriodGsm++;
    }
    else
    {
    	ulCountPeriodGsm = 0;
    	ulCountPulseGsm = 0;

    	ulQtyPulseGsm = 0;		/* 1 pulses - 10 ON + 0 OFF (1000ms)	*/
    	ulPeriodGsm = 0; 		/* 20 * 100ms = 2s		*/

		gpio_set_level(GPIO_OUTPUT_GSM_DIAG, 0);
		gpio_set_level(GPIO_OUTPUT_GSM_DIAG_EXT, 0);
		ucOnState = true;

    }
 }
//////////////////////////////////////////////
//
//
//              TaskGsm_IgnoreEvent
//
//
//////////////////////////////////////////////
unsigned char TaskDebug_Gsm_IgnoreEvent(sMessageType *psMessage)
{
    unsigned char boError = false;
    return(boError);
}
//////////////////////////////////////////////
//
//
//             Io Gsm State Machine
//
//
//////////////////////////////////////////////

static sStateMachineType const gasTaskDebug_Gsm_Initializing[] =
{
	/* Event		Action routine		Next state */
	//	State specific transitions

	{EVENT_IO_GSM_INIT,	   			TaskDebug_Gsm_Init,				TASKIO_GSM_INITIALIZING, 		TASKIO_GSM_INITIALIZING 	},
	{EVENT_IO_GSM_CONNECTING,		TaskDebug_Gsm_Connecting,		TASKIO_GSM_INITIALIZING,		TASKIO_GSM_INITIALIZING 	},
	{EVENT_IO_GSM_COMMUNICATING,	TaskDebug_Gsm_Communicating,	TASKIO_GSM_INITIALIZING,		TASKIO_GSM_INITIALIZING 	},
	/*{EVENT_IO_GSM_ERROR,			TaskDebug_Gsm_Error,			TASKIO_GSM_INITIALIZING,		TASKIO_GSM_INITIALIZING 	},*/
	{EVENT_IO_GSM_UPLOAD_DONE,		TaskDebug_Gsm_UploadDone,		TASKIO_GSM_INITIALIZING,		TASKIO_GSM_INITIALIZING 	},
	/*{EVENT_IO_GSM_OFF,				TaskDebug_Gsm_Off,				TASKIO_GSM_INITIALIZING,	TASKIO_GSM_INITIALIZING 	},*/

	/*{EVENT_IO_SLEEPING,				TaskDebug_Sleeping,				TASKIO_GSM_INITIALIZING,		TASKIO_GSM_INITIALIZING 	},*/

	{EVENT_IO_OUT1_ARMING,			TaskDebug_Out1_Arming,			TASKIO_GSM_INITIALIZING,		TASKIO_GSM_INITIALIZING 	},
	{EVENT_IO_OUT1_DISARMING,		TaskDebug_Out1_Disarming,		TASKIO_GSM_INITIALIZING,		TASKIO_GSM_INITIALIZING 	},
	{EVENT_IO_OUT1_ALARMING,		TaskDebug_Out1_Alarming,		TASKIO_GSM_INITIALIZING,		TASKIO_GSM_INITIALIZING 	},
	{EVENT_IO_GSM_NULL,    			TaskDebug_Gsm_IgnoreEvent,		TASKIO_GSM_INITIALIZING,		TASKIO_GSM_INITIALIZING		}
};



static sStateMachineType const *const gpasTaskDebug_Sd_StateMachine[] =
{
	gasTaskDebug_Gsm_Initializing
};


static unsigned char ucCurrentStateIoSd = TASKIO_GSM_INITIALIZING;


void vTaskDebug( void *pvParameters )
{
	TickType_t elapsed_time;

    ESP_LOGI(DEBUG_TASK_TAG, __DATE__);
    ESP_LOGI(DEBUG_TASK_TAG," ");
    ESP_LOGI(DEBUG_TASK_TAG,__TIME__);
    ESP_LOGI(DEBUG_TASK_TAG,"\r\n");

	for( ;; )
	{
		elapsed_time = xTaskGetTickCount();
		if( xQueueReceive( xQueueDebug, &( stDebugMsg ),0 ) )
		{
			(void)eEventHandler ((unsigned char)SRC_DEBUG,gpasTaskDebug_Sd_StateMachine[ucCurrentStateIoSd], &ucCurrentStateIoSd, &stDebugMsg);
		}

		vHandleGsmDiag();
		vHandleOut1();
		vTaskDelayUntil(&elapsed_time, 100 / portTICK_PERIOD_MS);
	}
}


