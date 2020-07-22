/*
 * Io.c
 *
 *  Created on: 02/10/2018
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
#include "esp_sleep.h"

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
#include "Gsm.h"

#define DEFAULT_VREF    1100        //Use adc2_vref_to_gpio() to obtain a better estimate
#define NO_OF_SAMPLES   64          //Multisampling

//////////////////////////////////////////////
//
//
//            FUNCTION PROTOTYPES
//
//
//////////////////////////////////////////////
void vTaskIo( void *pvParameters );
void Io_Sleeping(void);

//////////////////////////////////////////////
//
//
//            VARIABLES
//
//
//////////////////////////////////////////////
tstIo stIo;
sMessageType stDebugMsg;
static const char *IO_TASK_TAG = "IO_TASK";

/*static esp_adc_cal_characteristics_t *adc_chars;*/
static const adc_channel_t channel = ADC_CHANNEL_3;     /*GPIO39 if ADC1*/
static const adc_atten_t atten = ADC_ATTEN_DB_11;
/*static const adc_unit_t unit = ADC_UNIT_1;*/


//////////////////////////////////////////////
//
//
//              Io_Configuration
//
//
//////////////////////////////////////////////
void Io_Configuration(void)
{
	/////////////////
	// IGNITION PIN
	/////////////////
	gpio_config_t io_conf;
	//disable interrupt
	io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
	//set as output mode
	io_conf.mode = GPIO_MODE_INPUT;
	//bit mask of the pins that you want to set,e.g.GPIO18/19
	io_conf.pin_bit_mask = GPIO_INPUT_IGNITION_PIN_SEL;
	//disable pull-down mode
	io_conf.pull_down_en = 0;
	//disable pull-up mode
	io_conf.pull_up_en = 0;
	//configure GPIO with the given settings
	gpio_config(&io_conf);

	//////////////////////////
	// ADC MAIN BATTERY
	/////////////////////////
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(channel, atten);
}
//////////////////////////////////////////////
//
//
//              Io_Sleeping
//
//
//////////////////////////////////////////////
void Io_Sleeping(void)
{
	#if DEBUG_IO
	ESP_LOGI(IO_TASK_TAG,"SLEEPING\r\n");
	#endif

    const int ext_wakeup_pin_1 = 36;
    const uint64_t ext_wakeup_pin_1_mask = 1ULL << ext_wakeup_pin_1;

    printf("Enabling EXT1 wakeup on pins GPIO%d\r\n", ext_wakeup_pin_1);
    esp_sleep_enable_ext1_wakeup(ext_wakeup_pin_1_mask, ESP_EXT1_WAKEUP_ANY_HIGH);

    esp_deep_sleep_start();
}


//////////////////////////////////////////////
//
//
//              Io_Init
//
//
//////////////////////////////////////////////
void Io_Init(void)
{

    xTaskCreate(vTaskIo, "vTaskIo", 1024*2, NULL, configMAX_PRIORITIES, NULL);
	/* Create the queue used by the queue send and queue receive tasks.
	http://www.freertos.org/a00116.html */

    Io_Configuration();
}



//////////////////////////////////////////////
//
//
//              TaskIo_ReadIo
//
//
//////////////////////////////////////////////
extern unsigned long u32TimeToSleep;

unsigned char TaskIo_ReadIo(void)
{
	unsigned char boError = true;
	static unsigned char ucCurrIgnition = 0xFF;
	static unsigned long u32EnterSleepMode = 300;
    uint32_t adc_reading = 0;

    /* Multisampling */
    for (int i = 0; i < NO_OF_SAMPLES; i++)
    {
    	adc_reading += adc1_get_raw((adc1_channel_t)channel);
    }
    adc_reading /= NO_OF_SAMPLES;

/*
    External Voltage	Internal Voltage	AD
    7,5	1,08	536
    8,9	1,25	695
    14,4	1,8	1314
    19,2	2,2	1865
    21,9	2,47	2171



    	Regressão

    	Modelo de regressão	Linear
    	R²	0,999987576204966
    	Erro padrão	0,025510615711619

    	Inclinação	0,008806058296029	144,278459122135 (Multipliquei por 16384)
    	Interceptação	2,78946607076697	45702,6121034461 (Multipliquei por 16384)

    	536	7,50951331743838
    	695	8,90967658650695
    	1314	14,3606266717487
    	1865	19,2127647928606
    	2171	21,9074186314454
*/

    stIo.flAdMainBatteryVoltage = (adc_reading*144.27);
    stIo.flAdMainBatteryVoltage += 45702;
    stIo.flAdMainBatteryVoltage /= (((1ULL<<14)));

	#if DEBUG_IO
	/*ESP_LOGI(DEBUG_TASK_TAG,"AD Voltage=%d\r\n",adc_reading);*/
	ESP_LOGI(IO_TASK_TAG,"MainBattery Voltage=%f\r\n",stIo.flAdMainBatteryVoltage);
	#endif

#if 0
	if(gpio_get_level(GPIO_INPUT_IGNITION) == 1)
	{
		stIo.ucIgnition = 1;
		u32EnterSleepMode = (u32TimeToSleep);

		if(ucCurrIgnition != stIo.ucIgnition)
		{
			#if DEBUG_IO
			ESP_LOGI(IO_TASK_TAG,"Ignition ON\r\n");
			#endif
			ucCurrIgnition = stIo.ucIgnition;
		}
	}
	else
	{
		stIo.ucIgnition = 0;
		if(ucCurrIgnition != stIo.ucIgnition)
		{
			#if DEBUG_IO
			ESP_LOGI(IO_TASK_TAG,"Ignition OFF\r\n");
			#endif
			ucCurrIgnition = stIo.ucIgnition;
		}

		if(u32EnterSleepMode > 0)
		{
			#if DEBUG_IO
			ESP_LOGI(IO_TASK_TAG,"Sleep=%ld\r\n",u32EnterSleepMode);
			#endif

			u32EnterSleepMode--;
		}
		else
		{
			Io_Sleeping();
		}
	}
#endif
	return(boError);
}


void vTaskIo( void *pvParameters )
{
	TickType_t elapsed_time;
	for( ;; )
	{
		elapsed_time = xTaskGetTickCount();
		TaskIo_ReadIo();

		/*vTaskDelay(1000-(after_time)/portTICK_PERIOD_MS);*/

		vTaskDelayUntil(&elapsed_time, 1000 / portTICK_PERIOD_MS);
	}
}

