/*
 * Sd.c
 *
 *  Created on: 20/09/2018
 *      Author: danilo
 */
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <sys/param.h>
#include <dirent.h>

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

#include "defines.h"
#include "State.h"
#include "Gsm.h"
#include "Wifi.h"
#include "App.h"
#include "Ble.h"
#include "Sd.h"
#include "http_client.h"

void vTaskSd( void *pvParameters );
unsigned char TaskSd_Writing(sMessageType *psMessage);
unsigned char TaskSd_IgnoreEvent(sMessageType *psMessage);

unsigned long u32TimeToSleep;
unsigned long u32TimeToWakeup;
unsigned long u32PeriodLog = 10;
unsigned long u32PeriodTx;
static unsigned char boBuzzerStatus;

char cWifiApName[RX_BUF_SIZE_REDUCED];
char cWifiApPassword[RX_BUF_SIZE_REDUCED];
char cApn[RX_BUF_SIZE_REDUCED];
char cApnLogin[RX_BUF_SIZE_REDUCED];
char cApnPassword[RX_BUF_SIZE_REDUCED];
char cHttpDns[RX_BUF_SIZE_REDUCED];
char cWebPage1[RX_BUF_SIZE_REDUCED];
char cWebPage2[RX_BUF_SIZE_REDUCED];
char cStateAlarm[RX_BUF_SIZE_REDUCED];


const char cConfigText[]= {"CONFIG/CONFIG.txt"};
const char cTimeToSleepToken[] = {"TS="};
const char cPeriodLogToken[]  = {"PL="};
const char cBuzzerStatusToken[]  = {"BZ="};
const char cWifiSettingsToken[]  = {"WIFI="};
const char cHttpSettingsToken[]  = {"HTTP="};
const char cPage1UrlToken[]  = {"PAGE1="};
const char cPage2UrlToken[]  = {"PAGE2="};
const char cModemApnToken[]  = {"APN="};
const char cModemPeriodTxToken[] = {"PTX="};
const char cStateAlarmToken[] = {"STATE="};


const char cConfigData[]=
{
		"\
		TS=120\r\n\
		PL=30\r\n\
		BZ=OFF\r\n\
		WIFI=Iphone4,poliana90\r\n\
		HTTP=gpslogger.esy.es\r\n\
		PAGE1=http://gpslogger.esy.es/pages/upload/timestamp.php\r\n\
		PAGE2=http://gpslogger.esy.es/pages/upload/rio.php\r\n\
		APN=vodafone.br, , \r\n\
		PTX=120\r\n\
		STATE=UNARMED\r\n"/*UNARMED,ARMED,ALARMED*/
};

/*const char cConfigTimeToSleep[] = {"TS=120\r\n"};
const char cConfigPeriodLog[]  = {"PL=30\r\n"};
const char cConfigBuzzerStatus[]  = {"BZ=OFF\r\n"};
const char cConfigWifiSettings[]  = {"WIFI=Iphone4_EXT,poliana90\r\n"};
const char cConfigHttpSettings[]  = {"HTTP=gpslogger.esy.es\r\n"};
const char cConfigPageUrl[]  = {"PAGE=http://gpslogger.esy.es/pages/upload/timestamp.php\r\n"};
const char cConfigModemApn[]  = {"APN=zap.vivo.com.br, , \r\n"};
const char cConfigModemPeriodTx[] = {"PTX=120\r\n"};*/

char cConfigHttpRxBuffer[RX_BUF_SIZE];
char  cConfigAndData[RX_BUF_SIZE+1];

static long int liFilePointerPositionBeforeReading = 0;
static long int liFilePointerPositionAfterReading = 0;

//////////////////////////////////////////////
//
//
//              VARIABLES
//
//
//////////////////////////////////////////////
/*extern tstIo stIo;*/
sMessageType stSdMsg;
static char cDataBuffer[32];
extern tstSensorKeylessCode stSensor[MAX_KEYLESS_SENSOR];
extern tstSensorKeylessCode stKeyless[MAX_KEYLESS_SENSOR];
extern tstSensorKeylessCode stTelephone[MAX_KEYLESS_SENSOR];
/*extern tstSensorKeylessCode stSensorKeylessCode[MAX_KEYLESS_SENSOR];*/

static char szFilenameToBeRead[64];
static char szFilenameToBeWritten[64];

static const char *SD_TASK_TAG = "SD_TASK";
extern unsigned long ulTimestamp;
//////////////////////////////////////////////
//
//
//              Sd_Init
//
//
//////////////////////////////////////////////
void SdInit(void)
{
	/*esp_log_level_set(SD_TASK_TAG, ESP_LOG_INFO);*/

	xQueueSd = xQueueCreate(sdQUEUE_LENGTH,			/* The number of items the queue can hold. */
							sizeof( sMessageType ) );	/* The size of each item the queue holds. */


    xTaskCreate(vTaskSd, "vTaskSd", 1024*4, NULL, configMAX_PRIORITIES-1, NULL);
	/* Create the queue used by the queue send and queue receive tasks.
	http://www.freertos.org/a00116.html */


	stSdMsg.ucSrc = SRC_SD;
	stSdMsg.ucDest = SRC_SD;
	stSdMsg.ucEvent = EVENT_SD_INIT;
	xQueueSend( xQueueSd, ( void * )&stSdMsg, 0);
}

//////////////////////////////////////////////
//
//
//              TaskSd_Init
//
//
//////////////////////////////////////////////
unsigned char TaskSd_Init(sMessageType *psMessage)
{
	unsigned char boError = true;

	char* cLocalBuffer = (char*) malloc(RX_BUF_SIZE+1);

    ESP_LOGI(SD_TASK_TAG, "INIT\r\n");
    ESP_LOGI(SD_TASK_TAG, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = NULL,
      .max_files = 5,
      .format_if_mount_failed = true
    };

    // Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(SD_TASK_TAG, "Failed to mount or format filesystem");
        }
        else if (ret == ESP_ERR_NOT_FOUND)
        {
            ESP_LOGE(SD_TASK_TAG, "Failed to find SPIFFS partition");
        }
        else
        {
            ESP_LOGE(SD_TASK_TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        boError = false;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK)
    {
        ESP_LOGE(SD_TASK_TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(SD_TASK_TAG, "Partition size: total: %d, used: %d", total, used);
    }

    /*********************************************
     *				DELETE FILE
     *********************************************/
#if 0
    if (remove("/spiffs/CONFIG.TXT.TXT") == 0)
    	ESP_LOGI(SD_TASK_TAG,"Deleted successfully");
     else
    	 ESP_LOGI(SD_TASK_TAG,"Unable to delete the file");
#endif


#if 0
    if (remove("/spiffs/KEYLESS.TXT") == 0)
    	ESP_LOGI(SD_TASK_TAG,"Deleted successfully");
     else
    	 ESP_LOGI(SD_TASK_TAG,"Unable to delete the file");
#endif

#if 0
    if (remove("/spiffs/SENSOR.TXT") == 0)
    	ESP_LOGI(SD_TASK_TAG,"Deleted successfully");
     else
    	 ESP_LOGI(SD_TASK_TAG,"Unable to delete the file");
#endif

#if 0
    if (remove("/spiffs/TELEPHONE.TXT") == 0)
    	ESP_LOGI(SD_TASK_TAG,"Deleted successfully");
     else
    	 ESP_LOGI(SD_TASK_TAG,"Unable to delete the file");
#endif

#if 1
    /*********************************************
     *		READ FILES INSIDE FOLDER
     *********************************************/
    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir ("/spiffs/")) != NULL)
    {
		/* print all the files and directories within directory */
		while ((ent = readdir (dir)) != NULL)
		{
		  ESP_LOGI(SD_TASK_TAG,"%s\n", ent->d_name);
		}
		closedir (dir);
    }
    else
    {
      /* could not open directory */
    	ESP_LOGE(SD_TASK_TAG, "Error opening directory");
        boError = false;
    }
#endif

    FILE *f;
#if 0
    /*********************************************
     *			WRITING KEYLESS FILE
     *********************************************/

    // Use POSIX and C standard library functions to work with files.
    // First create a file.
    ESP_LOGI(SD_TASK_TAG, "Opening file");
    f = fopen("/spiffs/KEYLESS.TXT", "w");
    if (f == NULL) {
        ESP_LOGE(SD_TASK_TAG, "Failed to open file for writing");
    }
    fclose(f);

    /*********************************************
     *			WRITING SENSOR FILE
     *********************************************/

    // Use POSIX and C standard library functions to work with files.
    // First create a file.
    ESP_LOGI(SD_TASK_TAG, "Opening file");
    f = fopen("/spiffs/SENSOR.TXT", "w");
    if (f == NULL) {
        ESP_LOGE(SD_TASK_TAG, "Failed to open file for writing");
    }
    fclose(f);

    /*********************************************
     *			WRITING TELEPHONE FILE
     *********************************************/

    // Use POSIX and C standard library functions to work with files.
    // First create a file.
    ESP_LOGI(SD_TASK_TAG, "Opening file");
    f = fopen("/spiffs/TELEPHONE.TXT", "w");
    if (f == NULL) {
        ESP_LOGE(SD_TASK_TAG, "Failed to open file for writing");
    }
    fclose(f);

    /*********************************************
     *			WRITING CONFIG FILE
     *********************************************/

    // Use POSIX and C standard library functions to work with files.
    // First create a file.
    ESP_LOGI(SD_TASK_TAG, "Opening file");
    f = fopen("/spiffs/CONFIG.TXT", "w");
    if (f == NULL) {
        ESP_LOGE(SD_TASK_TAG, "Failed to open file for writing");
    }

    ESP_LOGI(SD_TASK_TAG, "\r\n%s\r\n",cConfigData);
    fprintf(f, "%s",cConfigData);
    fclose(f);

#endif

    /*********************************************
     *			READING KEYLESS FILE
     *********************************************/
    // Open renamed file for reading
    ESP_LOGI(SD_TASK_TAG, "Reading Keyless file");
    f = fopen("/spiffs/KEYLESS.TXT", "r");
    if (f == NULL)
    {
		ESP_LOGE(SD_TASK_TAG, "Failed to open file for reading");
		boError = false;
		free(cLocalBuffer);
    }

    memset(cLocalBuffer,0,RX_BUF_SIZE+1);
	fread (cLocalBuffer,1,RX_BUF_SIZE,f);
	fclose(f);
	ESP_LOGI(SD_TASK_TAG, "Read from file:%s", cLocalBuffer);

	char *pch;
	int i = 0;

	pch = strtok(cLocalBuffer,",\r\n");
	if(pch != NULL)
	{
		strcpy(stKeyless[i].cId,pch);
		ESP_LOGI(SD_TASK_TAG, "stKeyless[%d].cId:%s",i,stKeyless[i].cId);
	}
	while ((pch != NULL) && (i < MAX_KEYLESS_SENSOR))
	{
		/*ESP_LOGI(SD_TASK_TAG, "p:%s",pch);*/

		pch = strtok(NULL,",\r\n");
		if(pch == NULL) break;
		strcpy(stKeyless[i].cCode,pch);
		ESP_LOGI(SD_TASK_TAG, "stKeyless[%d].cCode:%s",i,stKeyless[i].cCode);

		i++;

		pch = strtok(NULL,",\r\n");
		if(pch == NULL) break;
		strcpy(stKeyless[i].cId,pch);
		ESP_LOGI(SD_TASK_TAG, "stKeyless[%d].cId:%s",i,stKeyless[i].cId);
	}

    /*********************************************
     *			READING SENSOR FILE
     *********************************************/
    // Open renamed file for reading
    ESP_LOGI(SD_TASK_TAG, "Reading Sensor file");
    f = fopen("/spiffs/SENSOR.TXT", "r");
    if (f == NULL)
    {
		ESP_LOGE(SD_TASK_TAG, "Failed to open file for reading");
		boError = false;
		free(cLocalBuffer);
    }

    memset(cLocalBuffer,0,RX_BUF_SIZE+1);
	fread (cLocalBuffer,1,RX_BUF_SIZE,f);
	fclose(f);
	ESP_LOGI(SD_TASK_TAG, "Read from file: %s", cLocalBuffer);


	i = 0;
	pch = strtok(cLocalBuffer,",\r\n");
	if(pch != NULL)
	{
		strcpy(stSensor[i].cId,pch);
		ESP_LOGI(SD_TASK_TAG, "stSensor[%d].cId:%s",i,stSensor[i].cId);
	}
	while ((pch != NULL) && (i < MAX_KEYLESS_SENSOR))
	{
		/*ESP_LOGI(SD_TASK_TAG, "p:%s",pch);*/

		pch = strtok(NULL,",\r\n");
		if(pch == NULL) break;
		strcpy(stSensor[i].cCode,pch);
		ESP_LOGI(SD_TASK_TAG, "stSensor[%d].cCode:%s",i,stSensor[i].cCode);

		i++;

		pch = strtok(NULL,",\r\n");
		if(pch == NULL) break;
		strcpy(stSensor[i].cId,pch);
		ESP_LOGI(SD_TASK_TAG, "stSensor[%d].cId:%s",i,stSensor[i].cId);
	}

    /*********************************************
     *			READING TELEPHONE FILE
     *********************************************/
    // Open renamed file for reading
    ESP_LOGI(SD_TASK_TAG, "Reading Telephone file");
    f = fopen("/spiffs/TELEPHONE.TXT", "r");
    if (f == NULL)
    {
		ESP_LOGE(SD_TASK_TAG, "Failed to open file for reading");
		boError = false;
		free(cLocalBuffer);
    }

    memset(cLocalBuffer,0,RX_BUF_SIZE+1);
	fread (cLocalBuffer,1,RX_BUF_SIZE,f);
	fclose(f);
	ESP_LOGI(SD_TASK_TAG, "Read from file: %s", cLocalBuffer);


	i = 0;
	pch = strtok(cLocalBuffer,",\r\n");
	if(pch != NULL)
	{
		strcpy(stTelephone[i].cId,pch);
		ESP_LOGI(SD_TASK_TAG, "stTelephone[%d].cId:%s",i,stTelephone[i].cId);
	}
	while ((pch != NULL) && (i < MAX_KEYLESS_SENSOR))
	{
		pch = strtok(NULL,",\r\n");
		if(pch == NULL) break;
		strcpy(stTelephone[i].cCode,pch);
		ESP_LOGI(SD_TASK_TAG, "stTelephone[%d].cCode:%s",i,stTelephone[i].cCode);

		i++;

		pch = strtok(NULL,",\r\n");
		if(pch == NULL) break;
		strcpy(stTelephone[i].cId,pch);
		ESP_LOGI(SD_TASK_TAG, "stTelephone[%d].cId:%s",i,stTelephone[i].cId);
	}

#if 0
    /*********************************************
     *			UNMOUNT SPIFFS
     *********************************************/
    // All done, unmount partition and disable SPIFFS
    esp_vfs_spiffs_unregister(NULL);
    ESP_LOGI(SD_TASK_TAG, "SPIFFS unmounted");
#endif



    /*********************************************
     *			READING CONFIG FILE
     *********************************************/
    // Open renamed file for reading
    ESP_LOGI(SD_TASK_TAG, "Reading Config file");
    f = fopen("/spiffs/CONFIG.TXT", "r");
    if (f == NULL)
    {
		ESP_LOGE(SD_TASK_TAG, "Failed to open file for reading");
		boError = false;
		free(cLocalBuffer);
    }

    memset(cLocalBuffer,0,RX_BUF_SIZE+1);
	fread (cLocalBuffer,1,RX_BUF_SIZE,f);
	fclose(f);
	ESP_LOGI(SD_TASK_TAG, "Read from file: '%s'", cLocalBuffer);

	char *pcChar;
    /* TimeToSleep*/
	pcChar = strstr (cLocalBuffer,cTimeToSleepToken);
	if(pcChar != NULL)
	{
		pcChar = pcChar + 3;
		if(pcChar != NULL)
		{
			u32TimeToSleep = atoi(pcChar);
		}
	}


	/* Period Log*/
	pcChar = strstr (cLocalBuffer,cPeriodLogToken);
	if(pcChar != NULL)
	{
		pcChar = pcChar + 3;
		if(pcChar != NULL)
		{
			u32PeriodLog = atoi(pcChar);
		}
	}


    /* Buzzer Status*/
	pcChar = strstr (cLocalBuffer,cBuzzerStatusToken);
	if(pcChar != NULL)
	{
		pcChar = pcChar + 3;
		if(pcChar != NULL)
		{
			if( (strcmp("ON\r\n",pcChar)) == 0)
			{
				boBuzzerStatus = true;
			}
			else
			{
				boBuzzerStatus = false;
			}
		}
	}

	/*Wifi Settings*/
	pcChar = strstr (cLocalBuffer,cWifiSettingsToken);
	if(pcChar != NULL)
	{
		pcChar = pcChar + 5;
		pcChar = strtok((char*)pcChar,",");
		if(pcChar != NULL)
		{
			strcpy(cWifiApName,pcChar);
		}

		pcChar = strtok(NULL,"\r");
		if(pcChar != NULL)
		{
			strcpy(cWifiApPassword,pcChar);
		}
	}

	/*Http Settings*/
	pcChar = strtok (NULL,"=");
	pcChar = strtok (NULL,"\r");
	if(pcChar != NULL)
	{
		strcpy(cHttpDns,pcChar);

	}

	/* Page Url*/
	pcChar = strtok (NULL,"=");
	pcChar = strtok (NULL,"\r");
	if(pcChar != NULL)
	{
		strcpy(cWebPage1,pcChar);
	}

	/* Page Url*/
	pcChar = strtok (NULL,"=");
	pcChar = strtok (NULL,"\r");
	if(pcChar != NULL)
	{
		strcpy(cWebPage2,pcChar);
	}


	/* Modem Settings*/
	pcChar = strtok (NULL,"=");
	pcChar = strtok (NULL,",");
	if(pcChar != NULL)
	{
		strcpy(cApn,pcChar);
	}
	pcChar = strtok(NULL,",");
	if(pcChar != NULL)
	{
		strcpy(cApnLogin,pcChar);
	}

	pcChar = strtok(NULL,"\r");
	if(pcChar != NULL)
	{
		strcpy(cApnPassword,pcChar);
	}

	pcChar = strtok(NULL,"=");
	pcChar = strtok(NULL,"\r");
	if(pcChar != NULL)
	{
		u32PeriodTx = atoi(pcChar);
	}

	pcChar = strtok(NULL,"=");
	pcChar = strtok(NULL,"\r");
	if(pcChar != NULL)
	{
		strcpy(cStateAlarm,pcChar);
	}


    ESP_LOGI(SD_TASK_TAG, "\r\nu32TimeToSleep=%ld\r\n",u32TimeToSleep);
    ESP_LOGI(SD_TASK_TAG, "u32PeriodLog=%ld\r\n",u32PeriodLog);
    ESP_LOGI(SD_TASK_TAG, "WifiAPName=%s\r\nWifiPwd=%s\r\n",cWifiApName,cWifiApPassword);
    ESP_LOGI(SD_TASK_TAG, "cHttpDns=%s\r\n",cHttpDns);
    ESP_LOGI(SD_TASK_TAG, "cWebPage1=%s\r\n",cWebPage1);
    ESP_LOGI(SD_TASK_TAG, "cWebPage2=%s\r\n",cWebPage2);
    ESP_LOGI(SD_TASK_TAG, "cApn=%s\r\ncApnLogin=%s\r\ncApnPassword=%s\r\n",cApn,cApnLogin,cApnPassword);
    ESP_LOGI(SD_TASK_TAG, "cStateAlarm=%s\r\n",cStateAlarm);

	stSdMsg.ucSrc = SRC_SD;
	stSdMsg.ucDest = SRC_SD;
	stSdMsg.ucEvent = EVENT_SD_OPENING;
	xQueueSend( xQueueSd, ( void * )&stSdMsg, 0);

    free(cLocalBuffer);

    return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskSd_Opening
//
//
//////////////////////////////////////////////
unsigned char TaskSd_Opening(sMessageType *psMessage)
{
	unsigned char boError = true;

	FILE *f;
	char* cLocalBuffer = (char*) malloc(RX_BUF_SIZE+1);
	#if DEBUG_SDCARD
    ESP_LOGI(SD_TASK_TAG, "<<<<OPENING>>>>\r\n\r\n<<<<>>>>\r\n");
	#endif

    /*********************************************
     *		READ FILES INSIDE FOLDER
     *********************************************/
    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir ("/spiffs/")) != NULL)
    {
		/* print all the files and directories within directory */
		for(;;)
		{
			if((ent = readdir (dir)) != NULL)
			{
				ESP_LOGI(SD_TASK_TAG,"%s,FileNumber=%d\n", ent->d_name,ent->d_ino);

				if(strstr(ent->d_name,"DATA_") != NULL)
				{
					memset(cLocalBuffer,0,RX_BUF_SIZE+1);

					strcpy(cLocalBuffer,"/spiffs/");
					strcat(cLocalBuffer,ent->d_name);

					f = fopen((const char*)cLocalBuffer, "r");
				    if(f == NULL )
				    {
						ESP_LOGE(SD_TASK_TAG, "Failed to open file for reading");
						boError = false;
				    }
				    else
				    {
					    fclose(f);
						strcpy(szFilenameToBeRead,cLocalBuffer);

						liFilePointerPositionBeforeReading = 0;
						liFilePointerPositionAfterReading = 0;

						stSdMsg.ucSrc = SRC_SD;
						stSdMsg.ucDest = SRC_SD;
						stSdMsg.ucEvent = EVENT_SD_READING;
						xQueueSend( xQueueSd, ( void * )&stSdMsg, 0);
				    }
					break;
				}
			}
			else
			{
				ESP_LOGE(SD_TASK_TAG,"No more DATA files\n");
		        boError = false;
#if 0
				stSdMsg.ucSrc = SRC_SD;
				stSdMsg.ucDest = SRC_GSM;
				stSdMsg.ucEvent = EVENT_GSM_ENDING;
				xQueueSend( xQueueGsm, ( void * )&stSdMsg, 0);

				stSdMsg.ucSrc = SRC_SD;
				stSdMsg.ucDest = SRC_WIFI;
				stSdMsg.ucEvent = EVENT_WIFI_ENDING;
				xQueueSend( xQueueWifi, ( void * )&stSdMsg, 0);
#endif

				stSdMsg.ucSrc = SRC_SD;
				stSdMsg.ucDest = SRC_HTTPCLI;
				stSdMsg.ucEvent = EVENT_HTTPCLI_ENDING;
				xQueueSend( xQueueHttpCli, ( void * )&stSdMsg, 0);

				break;
			}
		}
		closedir (dir);
    }
    else
    {
      /* could not open directory */
    	ESP_LOGE(SD_TASK_TAG, "Error opening directory");
        boError = false;
    }
    free(cLocalBuffer);

	return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskSd_Writing
//
//
//////////////////////////////////////////////
unsigned char TaskSd_Writing(sMessageType *psMessage)
{
	unsigned char boError = true;
	FILE *f;
	struct tm  ts;
	char cLocalBuffer[32];


#if DEBUG_SDCARD
    ESP_LOGI(SD_TASK_TAG, "<<<<WRITING>>>>\r\n");
#endif

	// Format time, "ddd yyyy-mm-dd hh:mm:ss zzz"
	ts = *localtime((time_t*)&ulTimestamp);
	strftime(cLocalBuffer, sizeof(cLocalBuffer), "%Y%m%d%H", &ts);

	memset(szFilenameToBeWritten,0x00,sizeof(szFilenameToBeWritten));
	strcpy(szFilenameToBeWritten,"/spiffs/DATA_");
	strcat(szFilenameToBeWritten,cLocalBuffer);
	strcat(szFilenameToBeWritten,".TXT");


#if 0
    f = fopen((const char*)szFilenameToBeWritten, "w+" );
#if DEBUG_SDCARD
    ESP_LOGI(SD_TASK_TAG, "DATA GPS:%s",cGpsDataCopiedToSd);
#endif
	fprintf(f,"%s",(const char*)"DANILO");
	fclose(f);

    f = fopen((const char*)szFilenameToBeWritten, "r" );
	char* cLocalBuffer = (char*) malloc(RX_BUF_SIZE+1);
	memset(cLocalBuffer,0,RX_BUF_SIZE+1);
	fread (cLocalBuffer,1,RX_BUF_SIZE,f);
	/*fgets (cLocalBuffer,RX_BUF_SIZE,f);*/

#if DEBUG_SDCARD
    ESP_LOGI(SD_TASK_TAG, "DATA:%s",cLocalBuffer);
#endif
	free(cLocalBuffer);
	(void)fclose(f);
#endif

	unsigned char mac[6];
	(void)esp_efuse_mac_get_default(&mac[0]);
	ESP_LOGI(SD_TASK_TAG, "MAC=%02x:%02x:%02x:%02x:%02x:%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);


	f = fopen(szFilenameToBeWritten, "a" );
	if(f != NULL)
	{
#if DEBUG_SDCARD
    ESP_LOGI(SD_TASK_TAG, "Reading for update...");
#endif

		if(strlen(psMessage->pcMessageData)>0)
		{
		    /* Format File: R=SENSORX,timestamp\r\n*/
			int i = fprintf(f,"R=%02X%02X%02X%02X%02X%02X,%s,%ld\r\n",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],psMessage->pcMessageData,ulTimestamp);
			if(i > 0)
			{
				#if DEBUG_SDCARD
				ESP_LOGI(SD_TASK_TAG, "Writing OK!");
				#endif
			}
			else
			{
				#if DEBUG_SDCARD
				ESP_LOGI(SD_TASK_TAG, "Writing NOK!");
				#endif
			}
		}
		fclose(f);
	}
	else
	{
		f = fopen(szFilenameToBeWritten, "w");
		if(f != NULL)
		{
			if(strlen(psMessage->pcMessageData)>0)
			{
				int i = fprintf(f,"R=%02X%02X%02X%02X%02X%02X,%s,%ld\r\n",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],psMessage->pcMessageData,ulTimestamp);
				if(i > 0)
				{
					#if DEBUG_SDCARD
					ESP_LOGI(SD_TASK_TAG, "Writing new file");
					#endif
				}
				else
				{
					#if DEBUG_SDCARD
					ESP_LOGE(SD_TASK_TAG, "Error writing new file");
					#endif
				}
			}
			fclose(f);
		}
	}
	return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskSd_Programming
//
//
//////////////////////////////////////////////
unsigned char TaskSd_Programming(sMessageType *psMessage)
{
	unsigned char boError = true;
	FILE *f;

#if DEBUG_SDCARD
    ESP_LOGI(SD_TASK_TAG, "<<<<PROGRAMMING>>>>");
#endif


#if 0
    f = fopen((const char*)szFilenameToBeWritten, "w+" );
#if DEBUG_SDCARD
    ESP_LOGI(SD_TASK_TAG, "DATA GPS:%s",cGpsDataCopiedToSd);
#endif
	fprintf(f,"%s",(const char*)"DANILO");
	fclose(f);

    f = fopen((const char*)szFilenameToBeWritten, "r" );
	char* cLocalBuffer = (char*) malloc(RX_BUF_SIZE+1);
	memset(cLocalBuffer,0,RX_BUF_SIZE+1);
	fread (cLocalBuffer,1,RX_BUF_SIZE,f);
	/*fgets (cLocalBuffer,RX_BUF_SIZE,f);*/

#if DEBUG_SDCARD
    ESP_LOGI(SD_TASK_TAG, "DATA:%s",cLocalBuffer);
#endif
	free(cLocalBuffer);
	(void)fclose(f);
#endif

	char *pch;
	char cDataId[16],cDataCode[16],cFilename[48];
	tstSensorKeylessCode *pst = NULL;

	pch = strtok(psMessage->pcMessageData,",");
	strcpy(cFilename,pch);

	pch = strtok(NULL,",");
	strcpy(cDataId,pch);

	pch = strtok(NULL,"\r\n");
	strcpy(cDataCode,pch);
#if DEBUG_SDCARD
		ESP_LOGI(SD_TASK_TAG, "Filename:%s",cFilename);
		ESP_LOGI(SD_TASK_TAG, "Id:%s",cDataId);
		ESP_LOGI(SD_TASK_TAG, "Code:%s",cDataCode);
#endif


	for(int i = 0; i < MAX_KEYLESS_SENSOR; i++)
	{
		if( strstr(cFilename,"KEYLESS.TXT") != NULL)
		{
			pst = &stKeyless[i];
		}
		else
		{
			if( strstr(cFilename,"SENSOR.TXT") != NULL)
			{
				pst = &stSensor[i];
			}
			else
			{
				if( strstr(cFilename,"TELEPHONE.TXT") != NULL)
				{
					pst = &stTelephone[i];
				}
			}
		}

		#if DEBUG_SDCARD
			ESP_LOGI(SD_TASK_TAG, "Code[%d]:%s",i,pst->cCode);
		#endif

		if(strncmp(pst->cCode,cDataCode,7) == 0)
		{
			#if DEBUG_SDCARD
				ESP_LOGI(SD_TASK_TAG, "Code already recorded\r\n");
			#endif
				memset(cDataBuffer,0,sizeof(cDataBuffer));
				strcpy(cDataBuffer,"CODE ALREADY EXISTS");

				stSdMsg.ucSrc = SRC_SD;
				stSdMsg.ucDest = SRC_BLE;
				stSdMsg.ucEvent = (int)NULL;
				stSdMsg.pcMessageData = &cDataBuffer[0];
				xQueueSend(xQueueBle,( void * )&stSdMsg,NULL);

				stSdMsg.ucSrc = SRC_SD;
				stSdMsg.ucDest = SRC_APP;
				stSdMsg.ucEvent = EVENT_APP_REMOTE_RECORDED;
				stSdMsg.u32MessageData = 0;/* Not Recorded*/
				xQueueSend(xQueueApp,( void * )&stSdMsg,NULL);

				return(boError);
		}
	}

	f = fopen(cFilename, "a" );
	if(f != NULL)
	{
#if DEBUG_SDCARD
    ESP_LOGI(SD_TASK_TAG, "Reading for update...");
#endif

		if(strlen(cDataCode)>0)
		{
			int i = fprintf(f,"%s,%s\r\n",cDataId,cDataCode);
			if(i > 0)
			{
				#if DEBUG_SDCARD
				ESP_LOGI(SD_TASK_TAG, "Programming OK!");
				#endif

				memset(cDataBuffer,0,sizeof(cDataBuffer));
				strcpy(cDataBuffer,"RECORDED SUCCESFULLY!");

				stSdMsg.ucSrc = SRC_SD;
				stSdMsg.ucDest = SRC_BLE;
				stSdMsg.ucEvent = (int)NULL;
				stSdMsg.pcMessageData = &cDataBuffer[0];
				xQueueSend(xQueueBle,( void * )&stSdMsg,NULL);

				stSdMsg.ucSrc = SRC_SD;
				stSdMsg.ucDest = SRC_APP;
				stSdMsg.ucEvent = EVENT_APP_REMOTE_RECORDED;
				stSdMsg.u32MessageData = 1;/* Recorded Successfully*/
				xQueueSend(xQueueApp,( void * )&stSdMsg,NULL);
			}
			else
			{
				#if DEBUG_SDCARD
				ESP_LOGI(SD_TASK_TAG, "Programming NOK!");
				#endif
			}
		}
		fclose(f);
	}
	else
	{
		f = fopen(cFilename, "w");
		if(f != NULL)
		{
			if(strlen(cDataCode)>0)
			{
				int i = fprintf(f,"%s,%s\r\n",cDataId,cDataCode);
				if(i > 0)
				{
					#if DEBUG_SDCARD
					ESP_LOGI(SD_TASK_TAG, "Programming new file OK");
					#endif

					memset(cDataBuffer,0,sizeof(cDataBuffer));
					strcpy(cDataBuffer,"RECORDED SUCCESFULLY!");

					stSdMsg.ucSrc = SRC_SD;
					stSdMsg.ucDest = SRC_BLE;
					stSdMsg.ucEvent = (int)NULL;
					stSdMsg.pcMessageData = &cDataBuffer[0];
					xQueueSend(xQueueBle,( void * )&stSdMsg,NULL);

					stSdMsg.ucSrc = SRC_SD;
					stSdMsg.ucDest = SRC_APP;
					stSdMsg.ucEvent = EVENT_APP_REMOTE_RECORDED;
					stSdMsg.u32MessageData = 1;/* Recorded Successfully*/
					xQueueSend(xQueueApp,( void * )&stSdMsg,NULL);

				}
				else
				{
					#if DEBUG_SDCARD
					ESP_LOGE(SD_TASK_TAG, "Error programming new file");
					#endif
				}
			}
            fflush(f);    // flushing or repositioning required
			fclose(f);
		}
		else
		{
			#if DEBUG_SDCARD
			ESP_LOGI(SD_TASK_TAG, "Strange Error, pls investigate");
			#endif
		}
	}


    /*********************************************
     *			READING  FILE
     *********************************************/
    // Open renamed file for reading
	char* cLocalBuffer = (char*) malloc(RX_BUF_SIZE+1);
    f = fopen(cFilename, "r");
    if (f == NULL)
    {
		ESP_LOGE(SD_TASK_TAG, "Failed to open file for reading");
		boError = false;
		free(cLocalBuffer);
		return(boError);
    }

    memset(cLocalBuffer,0,RX_BUF_SIZE+1);
	fread (cLocalBuffer,1,RX_BUF_SIZE,f);
	fclose(f);
	ESP_LOGI(SD_TASK_TAG, "Read from file:%s", cLocalBuffer);

	int i = 0;
	pch = strtok(cLocalBuffer,",\r\n");
	if(pch != NULL)
	{
		if( strstr(cFilename,"KEYLESS.TXT") != NULL)
		{
			strcpy(stKeyless[i].cId,pch);
			ESP_LOGI(SD_TASK_TAG, "stKeyless[%d].cId:%s",i,stKeyless[i].cId);
		}
		else
		{
			if( strstr(cFilename,"SENSOR.TXT") != NULL)
			{
				strcpy(stSensor[i].cId,pch);
				ESP_LOGI(SD_TASK_TAG, "stSensor[%d].cId:%s",i,stSensor[i].cId);
			}
			else
			{
				if( strstr(cFilename,"TELEPHONE.TXT") != NULL)
				{
					strcpy(stTelephone[i].cId,pch);
					ESP_LOGI(SD_TASK_TAG, "stTelephone[%d].cId:%s",i,stTelephone[i].cId);
				}
			}
		}
	}
	while ((pch != NULL) && (i < MAX_KEYLESS_SENSOR))
	{
		/*ESP_LOGI(SD_TASK_TAG, "p:%s",pch);*/

		pch = strtok(NULL,",\r\n");
		if(pch == NULL) break;

		if( strstr(cFilename,"KEYLESS.TXT") != NULL)
		{
			strcpy(stKeyless[i].cCode,pch);
			ESP_LOGI(SD_TASK_TAG, "stKeyless[%d].cCode:%s",i,stKeyless[i].cCode);

			i++;

			pch = strtok(NULL,",\r\n");
			if(pch == NULL) break;
			strcpy(stKeyless[i].cId,pch);
			ESP_LOGI(SD_TASK_TAG, "stKeyless[%d].cId:%s",i,stKeyless[i].cId);
		}
		else
		{
			if( strstr(cFilename,"SENSOR.TXT") != NULL)
			{
				strcpy(stSensor[i].cCode,pch);
				ESP_LOGI(SD_TASK_TAG, "stSensor[%d].cCode:%s",i,stSensor[i].cCode);

				i++;

				pch = strtok(NULL,",\r\n");
				if(pch == NULL) break;
				strcpy(stSensor[i].cId,pch);
				ESP_LOGI(SD_TASK_TAG, "stSensor[%d].cId:%s",i,stSensor[i].cId);
			}
			else
			{
				if( strstr(cFilename,"TELEPHONE.TXT") != NULL)
				{
					strcpy(stTelephone[i].cCode,pch);
					ESP_LOGI(SD_TASK_TAG, "stTelephone[%d].cCode:%s",i,stTelephone[i].cCode);

					i++;

					pch = strtok(NULL,",\r\n");
					if(pch == NULL) break;
					strcpy(stTelephone[i].cId,pch);
					ESP_LOGI(SD_TASK_TAG, "stTelephone[%d].cId:%s",i,stTelephone[i].cId);
				}
			}
		}
	}

	free(cLocalBuffer);
	return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskSd_Reading
//
//
//////////////////////////////////////////////
unsigned char TaskSd_Reading(sMessageType *psMessage)
{
	unsigned char boError = true;

	char* cLocalBuffer = (char*) malloc(RX_BUF_SIZE+1);
	FILE *f;

#if DEBUG_SDCARD
	ESP_LOGI(SD_TASK_TAG, "<<<<READING>>>>\r\n%s\r\n<<<<>>>>\r\n",szFilenameToBeRead);
#endif


	f = fopen(szFilenameToBeRead, "r");
	if(f != NULL)
	{
		#if DEBUG_SDCARD
			ESP_LOGI(SD_TASK_TAG, "Opening for Reading OK!");
		#endif

		for(;;)
		{
			fseek(f,liFilePointerPositionBeforeReading,SEEK_SET);

			#if DEBUG_SDCARD
			ESP_LOGI(SD_TASK_TAG, "PositionBeforeReading=%ld\r\n",liFilePointerPositionBeforeReading);
			#endif

			/*Reading file until finds \n or RX_BUF_SIZE, whichever happens first*/
			memset(cLocalBuffer,0,RX_BUF_SIZE+1);
			int i;
			if((fgets ( cLocalBuffer, RX_BUF_SIZE, f )) != NULL )
			{

				liFilePointerPositionAfterReading =  ftell (f);

				#if DEBUG_SDCARD
				ESP_LOGI(SD_TASK_TAG, "PositionAfterReading=%ld\r\n",liFilePointerPositionAfterReading);
				#endif

				i = strlen(cLocalBuffer);

				ESP_LOGI(SD_TASK_TAG, "Content of file with %d Bytes: %s",i, cLocalBuffer);
				/* Verify if file is OK*/
				if((cLocalBuffer[0] == 'R') && (cLocalBuffer[1] == '=') && (cLocalBuffer[i-1] == '\n') && (cLocalBuffer[i-2] == '\r'))
				{
					memset(cConfigAndData,0,RX_BUF_SIZE+1);
					strcpy(cConfigAndData,cLocalBuffer);
#if 0
					stSdMsg.ucSrc = SRC_SD;
					stSdMsg.ucDest = SRC_GSM;
					stSdMsg.ucEvent = EVENT_GSM_SEND_HTTPPREPAREDATA;
					stSdMsg.pcMessageData = "";
					xQueueSend( xQueueGsm, ( void * )&stSdMsg, 0);

					stSdMsg.ucSrc = SRC_SD;
					stSdMsg.ucDest = SRC_WIFI;
					stSdMsg.ucEvent = EVENT_WIFI_SEND_START_PAYLOAD/*EVENT_WIFI_SEND_CIPSTATUS*/;
					stSdMsg.pcMessageData = "";
					xQueueSend( xQueueWifi, ( void * )&stSdMsg, 0);
#endif

					stSdMsg.ucSrc = SRC_SD;
					stSdMsg.ucDest = SRC_HTTPCLI;
					stSdMsg.ucEvent = EVENT_HTTPCLI_POST;
					stSdMsg.pcMessageData = "";
					xQueueSend( xQueueHttpCli, ( void * )&stSdMsg, 0);


					fclose(f);
					break;
				}
				else
				{
					liFilePointerPositionBeforeReading = liFilePointerPositionAfterReading;
				}
			}
			else
			{
				#if DEBUG_SDCARD
					ESP_LOGE(SD_TASK_TAG, "No possible to retrieve data");
				#endif


				if(feof(f))
				{
					ESP_LOGI(SD_TASK_TAG, "End of File");


					int ret = remove(szFilenameToBeRead);

					if(ret == 0)
					{
						#if DEBUG_SDCARD
						ESP_LOGE(SD_TASK_TAG, "File deleted successfully");
						#endif
					 }
					else
					{
						#if DEBUG_SDCARD
						ESP_LOGE(SD_TASK_TAG, "Error: unable to delete the file");
						#endif
					}
#if 0
					stSdMsg.ucSrc = SRC_SD;
					stSdMsg.ucDest = SRC_GSM;
					stSdMsg.ucEvent = EVENT_GSM_ENDING/*EVENT_GSM_LIST_SMS_MSG*/;
					xQueueSend( xQueueGsm, ( void * )&stSdMsg, 0);


					stSdMsg.ucSrc = SRC_SD;
					stSdMsg.ucDest = SRC_WIFI;
					stSdMsg.ucEvent = EVENT_WIFI_ENDING/*EVENT_GSM_LIST_SMS_MSG*/;
					xQueueSend( xQueueWifi, ( void * )&stSdMsg, 0);
#endif
					stSdMsg.ucSrc = SRC_SD;
					stSdMsg.ucDest = SRC_HTTPCLI;
					stSdMsg.ucEvent = EVENT_HTTPCLI_ENDING;
					xQueueSend( xQueueHttpCli, ( void * )&stSdMsg, 0);

				}



				fclose(f);
				break;
			}
		}
	}
	else
	{
		#if DEBUG_SDCARD
			ESP_LOGE(SD_TASK_TAG, "No more files...");
		#endif

		stSdMsg.ucSrc = SRC_SD;
		stSdMsg.ucDest = SRC_GSM;
		stSdMsg.ucEvent = EVENT_GSM_ENDING/*EVENT_GSM_LIST_SMS_MSG*/;
		xQueueSend( xQueueGsm, ( void * )&stSdMsg, 0);

		stSdMsg.ucSrc = SRC_SD;
		stSdMsg.ucDest = SRC_WIFI;
		stSdMsg.ucEvent = EVENT_WIFI_ENDING;
		xQueueSend( xQueueWifi, ( void * )&stSdMsg, 0);


		stSdMsg.ucSrc = SRC_SD;
		stSdMsg.ucDest = SRC_SD;
		stSdMsg.ucEvent = EVENT_SD_OPENING;
		xQueueSend( xQueueSd, ( void * )&stSdMsg, 0);



	}
	free(cLocalBuffer);

	return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskSd_Marking
//
//
//////////////////////////////////////////////
unsigned char TaskSd_Marking(sMessageType *psMessage)
{
	unsigned char boError = true;
	FILE *f;

#if DEBUG_SDCARD
	ESP_LOGI(SD_TASK_TAG, "<<<<MARKING>>>>\r\n");
#endif

	if((f = fopen(szFilenameToBeRead, "r+")) != NULL)
	{
		fseek(f,liFilePointerPositionBeforeReading,SEEK_SET);
		fputs("*",f);
		liFilePointerPositionBeforeReading = liFilePointerPositionAfterReading;
		fclose(f);

	}

	stSdMsg.ucSrc = SRC_SD;
	stSdMsg.ucDest = SRC_SD;
	stSdMsg.ucEvent = EVENT_SD_READING;
	xQueueSend( xQueueSd, ( void * )&stSdMsg, 0);

	return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskSd_Erasing
//
//
//////////////////////////////////////////////
unsigned char TaskSd_Erasing(sMessageType *psMessage)
{
	unsigned char boError = true;
	FILE *f;

#if DEBUG_SDCARD
    ESP_LOGI(SD_TASK_TAG, "<<<<ERASING>>>>");
#endif

	char *pch;
	char cDataId[16],cDataCode[16],cFilename[48];
	tstSensorKeylessCode *pst = NULL;

	pch = strtok(psMessage->pcMessageData,",");
	strcpy(cFilename,pch);

	pch = strtok(NULL,",");
	strcpy(cDataId,pch);

	pch = strtok(NULL,"\r\n");
	strcpy(cDataCode,pch);
#if DEBUG_SDCARD
		ESP_LOGI(SD_TASK_TAG, "Filename:%s",cFilename);
		ESP_LOGI(SD_TASK_TAG, "Id:%s",cDataId);
		ESP_LOGI(SD_TASK_TAG, "Code:%s",cDataCode);
#endif

	f = fopen(cFilename, "w" );
	fseek ( f , 0 , SEEK_SET );
	if(f != NULL)
	{
		ESP_LOGI(SD_TASK_TAG, "Arquivo aberto\r\n");

		for(int i = 0; i < MAX_KEYLESS_SENSOR; i++)
		{
			if( strstr(cFilename,"KEYLESS.TXT") != NULL)
			{
				pst = &stKeyless[i];
			}
			else
			{
				if( strstr(cFilename,"SENSOR.TXT") != NULL)
				{
					pst = &stSensor[i];
				}
				else
				{
					if( strstr(cFilename,"TELEPHONE.TXT") != NULL)
					{
						pst = &stTelephone[i];
					}
				}
			}

			#if DEBUG_SDCARD
				ESP_LOGI(SD_TASK_TAG, "Code[%d]:%s",i,pst->cCode);
			#endif

			if(strncmp(pst->cCode,cDataCode,13) == 0)
			{
#if 0
				memset(cDataBuffer,0,sizeof(cDataBuffer));
				strcpy(cDataBuffer,"CODE EXISTS AND WILL BE ERASED");

				stSdMsg.ucSrc = SRC_SD;
				stSdMsg.ucDest = SRC_BLE;
				stSdMsg.ucEvent = (int)NULL;
				stSdMsg.pcMessageData = &cDataBuffer[0];
				xQueueSend(xQueueBle,( void * )&stSdMsg,NULL);


				stSdMsg.ucSrc = SRC_SD;
				stSdMsg.ucDest = SRC_APP;
				stSdMsg.ucEvent = EVENT_APP_REMOTE_RECORDED;
				stSdMsg.u32MessageData = 0;/* Not Recorded*/
				xQueueSend(xQueueApp,( void * )&stSdMsg,NULL);
#endif

				#if DEBUG_SDCARD
				ESP_LOGI(SD_TASK_TAG, "Code Found!");
				#endif

				memset(cDataBuffer,0,sizeof(cDataBuffer));
				strcpy(cDataBuffer,"CODE EXISTS AND WILL BE ERASED");

				stSdMsg.ucSrc = SRC_SD;
				stSdMsg.ucDest = SRC_BLE;
				stSdMsg.ucEvent = (int)NULL;
				stSdMsg.pcMessageData = &cDataBuffer[0];
				xQueueSend(xQueueBle,( void * )&stSdMsg,NULL);

				strcpy(pst->cId,"TEL ERASED");
				strcpy(pst->cCode,"5511000000000");
				int i = fprintf(f,"%s,%s\r\n",pst->cId,pst->cCode);
				if(i > 0)
				{
					#if DEBUG_SDCARD
					ESP_LOGI(SD_TASK_TAG, "Erasing Undefined code!");
					#endif
				}
			}
			else
			{
				if(strlen(pst->cCode) > 0)
				{
					int i = fprintf(f,"%s,%s\r\n",pst->cId,pst->cCode);
					if(i > 0)
					{
						#if DEBUG_SDCARD
						ESP_LOGI(SD_TASK_TAG, "Erasing OK!");
						#endif
					}
				}
			}
		}
		fclose(f);
	}


    /*********************************************
     *			READING FILE
     *********************************************/
    // Open renamed file for reading
	char* cLocalBuffer = (char*) malloc(RX_BUF_SIZE+1);
    f = fopen(cFilename, "r");
    if (f == NULL)
    {
		ESP_LOGE(SD_TASK_TAG, "Failed to open file for reading");
		boError = false;
		free(cLocalBuffer);
		return(boError);
    }

    memset(cLocalBuffer,0,RX_BUF_SIZE+1);
	fread (cLocalBuffer,1,RX_BUF_SIZE,f);
	fclose(f);


	int i = 0;
	pch = strtok(cLocalBuffer,",");
	pch = strtok(NULL,"\r\n");
	while ((pch != NULL) && (i < MAX_KEYLESS_SENSOR))
	{
		if( strstr(cFilename,"KEYLESS.TXT") != NULL)
		{
			pst = &stKeyless[i];
		}
		else
		{
			if( strstr(cFilename,"SENSOR.TXT") != NULL)
			{
				pst = &stSensor[i];
			}
			else
			{
				if( strstr(cFilename,"TELEPHONE.TXT") != NULL)
				{
					pst = &stTelephone[i];
				}

			}
		}
		strcpy(pst->cCode,pch);
		ESP_LOGI(SD_TASK_TAG, "Reading Code[%d]:%s",i, pst->cCode);
		pch = strtok(cLocalBuffer,"\r\n");
		pch = strtok(NULL,",");
		i++;
	}

	free(cLocalBuffer);
	return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskSd_WritingConfig
//
//
//////////////////////////////////////////////
unsigned char TaskSd_WritingConfig(sMessageType *psMessage)
{
	unsigned char boError = true;
	FILE *f;
	char* cLocalBuffer = (char*) malloc(RX_BUF_SIZE+1);

#if DEBUG_SDCARD
    ESP_LOGI(SD_TASK_TAG, "<<<<TaskSd_WritingConfig>>>>\r\n");
#endif

	char *pch;
	char cToken[16],cData[64],cFilename[48];

	pch = strtok(psMessage->pcMessageData,",");
	strcpy(cFilename,pch);

	pch = strtok(NULL,"=");
	strcpy(cToken,pch);


	pch = strtok(NULL,"\r\n");
	strcpy(cData,pch);
#if DEBUG_SDCARD
		ESP_LOGI(SD_TASK_TAG, "Filename:%s",cFilename);
		ESP_LOGI(SD_TASK_TAG, "Token:%s",cToken);
		ESP_LOGI(SD_TASK_TAG, "Data:%s",cData);
#endif

	f = fopen(cFilename, "r" );
	/*fseek ( f , 0 , SEEK_SET );*/
	if(f != NULL)
	{
	    memset(cLocalBuffer,0,RX_BUF_SIZE+1);
		fread (cLocalBuffer,1,RX_BUF_SIZE,f);

		ESP_LOGI(SD_TASK_TAG, "ConfigFile:%s",cLocalBuffer);

		pch=strstr(cLocalBuffer,(const char*)cToken);
		if(pch != NULL)
		{
			pch +=strlen(cToken);
			pch ++;
			pch =strtok(pch,"\r");/* Token*/
			strcpy(pch,cData);
		}
		fclose(f);
	}

	f = fopen(cFilename, "w" );
	fseek ( f , 0 , SEEK_SET );
	if(f != NULL)
	{
		fprintf(f,"%s",cLocalBuffer);
	}
	fclose(f);

	fflush(f);

    /*********************************************
     *			READING FILE
     *********************************************/
    // Open renamed file for reading
    f = fopen(cFilename, "r");
    if (f == NULL)
    {
		ESP_LOGE(SD_TASK_TAG, "Failed to open file for reading");
		boError = false;
		free(cLocalBuffer);
		return(boError);
    }

    memset(cLocalBuffer,0,RX_BUF_SIZE+1);
	fread (cLocalBuffer,1,RX_BUF_SIZE,f);

	ESP_LOGI(SD_TASK_TAG, "%s",cLocalBuffer);
	fclose(f);

	free(cLocalBuffer);
	return(boError);
}

//////////////////////////////////////////////
//
//
//              TaskSd_IgnoreEvent
//
//
//////////////////////////////////////////////
unsigned char TaskSd_IgnoreEvent(sMessageType *psMessage)
{
	unsigned char boError = false;
	return(boError);
}

static sStateMachineType const gasTaskSd_Initializing[] =
{
        /* Event                            Action routine          Success state               Failure state*/
        //  State specific transitions
        {EVENT_SD_INIT,                     TaskSd_Init,            	TASKSD_INITIALIZING,        TASKSD_INITIALIZING },
		{EVENT_SD_OPENING, 					TaskSd_Opening,			 	TASKSD_INITIALIZING,		TASKSD_INITIALIZING	},
		{EVENT_SD_WRITING, 					TaskSd_Writing,	    	 	TASKSD_INITIALIZING,		TASKSD_INITIALIZING	},
		{EVENT_SD_PROGRAMMING, 				TaskSd_Programming,    	 	TASKSD_INITIALIZING,		TASKSD_INITIALIZING	},
		{EVENT_SD_READING, 					TaskSd_Reading,				TASKSD_INITIALIZING,		TASKSD_INITIALIZING	},
	   	{EVENT_SD_MARKING, 					TaskSd_Marking,				TASKSD_INITIALIZING,		TASKSD_INITIALIZING },
	   	{EVENT_SD_ERASING, 					TaskSd_Erasing,				TASKSD_INITIALIZING,		TASKSD_INITIALIZING },

	   	{EVENT_SD_WRITING_CONFIG,			TaskSd_WritingConfig,		TASKSD_INITIALIZING,		TASKSD_INITIALIZING },

	    {EVENT_SD_NULL,                     TaskSd_IgnoreEvent,			TASKSD_INITIALIZING,		TASKSD_INITIALIZING }
};

static sStateMachineType const * const gpasTaskSd_StateMachine[] =
{
    gasTaskSd_Initializing
};

static unsigned char ucCurrentStateSd = TASKSD_INITIALIZING;

void vTaskSd( void *pvParameters )
{

	for( ;; )
	{
	    /*ESP_LOGI(SD_TASK_TAG, "Running...");*/

		if( xQueueReceive( xQueueSd, &( stSdMsg ),0 ) )
		{
            (void)eEventHandler ((unsigned char)SRC_SD,gpasTaskSd_StateMachine[ucCurrentStateSd], &ucCurrentStateSd, &stSdMsg);
		}

		vTaskDelay(500/portTICK_PERIOD_MS);
	}




}
