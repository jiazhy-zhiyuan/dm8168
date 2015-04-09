/*******************************************************************************
 *                                                                             *
 * Copyright (c) 2009 Texas Instruments Incorporated - http://www.ti.com/      *
 *                        ALL RIGHTS RESERVED                                  *
 *                                                                             *
 ******************************************************************************/
#include <audio.h>
#include <audio_priv.h>
#include <mcfw/interfaces/link_api/avsync.h>

/*add errno 0313*/
#include <errno.h>

/*add audio-control params 0416*/
#include <demos/link_api_demos/common/chains.h>
/****zys***/
#include <ti_audio.h>
#include "g726.h"
#include "g729a.h"

#define         MAX_STR_SIZE        256

static Int32    RecordAudio(Uint8 *buffer, Int32 *numSamples);
static Int32    InitAudioCaptureDevice (Int32 channels, Uint32 sample_rate, Int32 driver_buf_size);
static Int32    deInitAudioCaptureDevice(Void);
static Void     deletePreviousRecording (Int8 chNum);
static Void     Audio_recordMain (Void * arg);

static Int32    recordAudioFlag = 0, recordChNum = 0;
static Int32    audioSamplesRecorded = 0;
static Int32    zero_samples_count = 0;
static Uint8    audioRecordBuf[AUDIO_SAMPLES_TO_READ_PER_CHANNEL
                                    * AUDIO_MAX_CHANNELS * AUDIO_SAMPLE_LEN * AUDIO_PLAYBACK_SAMPLE_MULTIPLIER];

 /**< Audio sampling rate in Hz, Valid values: 8000, 16000 
  */
  UInt32 samplingHz=AUDIO_SAMPLE_RATE_DEFAULT;

  /**< Audio volume, Valid values: 0..8. Refer to TVP5158 datasheet for details
   */
  UInt32 audioVolume=AUDIO_VOLUME_DEFAULT;


static TaskCtx  captureThrHandle;
static snd_pcm_t *capture_handle = NULL;

static Int8     audioPhyToDataIndexMap[AUDIO_MAX_CHANNELS];
Int8            audioPath[MAX_STR_SIZE];
static AudioStats captureStats;
#ifdef AUDIO_G711CODEC_ENABLE
Uint8 audioG711codec = TRUE;
#endif
/****zys***/
static Int32 AUDIOAACAENC = TRUE;

static Void    *encHandle = NULL;
#define MAX_INPUT_BUFFER        (4*1024)
#define MAX_OUTPUT_BUFFER       (4*1024)
FILE        *fp_AAC = NULL;
FILE        *fp_test = NULL;


static short seg_end[8] = {0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF, 0x3FFF, 0x7FFF};

#define	SEG_SHIFT	(4)		/* Left shift for segment number. */

#define	QUANT_MASK	(0xf)		/* Quantization field mask. */

/* G711 ulaw to alaw edit by xyx_xte_0419 */

unsigned char _u2a[128] = {			
	1,	1,	2,	2,	3,	3,	4,	4,
	5,	5,	6,	6,	7,	7,	8,	8,
	9,	10,	11,	12,	13,	14,	15,	16,
	17,	18,	19,	20,	21,	22,	23,	24,
	25,	27,	29,	31,	33,	34,	35,	36,
	37,	38,	39,	40,	41,	42,	43,	44,
	46,	48,	49,	50,	51,	52,	53,	54,
	55,	56,	57,	58,	59,	60,	61,	62,
	64,	65,	66,	67,	68,	69,	70,	71,
	72,	73,	74,	75,	76,	77,	78,	79,
	81,	82,	83,	84,	85,	86,	87,	88,
	89,	90,	91,	92,	93,	94,	95,	96,
	97,	98,	99,	100,	101,	102,	103,	104,
	105,	106,	107,	108,	109,	110,	111,	112,
	113,	114,	115,	116,	117,	118,	119,	120,
	121,	122,	123,	124,	125,	126,	127,	128};
/***zys-G726******/

Uint8 ulaw2alaw(Uint8	uval)
{
	//printf("*****u2a call*****\n");
	uval &= 0xff;
	return ((uval & 0x80) ? (0xD5 ^ (_u2a[0xFF ^ uval] - 1)) :
	    (0x55 ^ (_u2a[0x7F ^ uval] - 1)));
}

Int32 Audio_captureIsStart()
{
	return recordAudioFlag;
}

Int32 Audio_setStoragePath (Int8 *path)
{
    Int8    i;
    Int8    dirName[256];
    Uint8   len;
    Int8    *ptr;

    printf ("Store path set to %s\n", path);
    /* This path has \n also */
    ptr = strstr(path, "\n");
    if (!ptr)
    ptr = strstr(path, "\r");

    if (ptr)
    {
        Int32 to_copy;

        to_copy = ptr - path;
//      printf ("Removing carriage return, bytes to copy %d\n", to_copy);
        strncpy(audioPath, path, to_copy);
        audioPath[to_copy] = 0;
    }
    else
    {
        strcpy(audioPath, path);
    }

    printf ("Trying to set storage path to %s\n", audioPath);

    if(-1 == access(audioPath, 0))
    {
        remove(audioPath);
        if( 0 != mkdir(audioPath, 0755))
        {
            printf ("\nAudio storage dir \"%s\" - Invalid Entry or File Permission Issue - audio recording not possible.....\n", audioPath);
            return  AUDIO_STATUS_EFAIL;
        }
    }

    for (i=0; i<AUDIO_MAX_CHANNELS; i++)
    {
        strcpy(dirName, audioPath);
        len = strlen(dirName);
        sprintf (&dirName[len], "/%02d", i + 1);
        if(-1 == access((char *) dirName, 0))
        {
            if( 0 != mkdir((char *) dirName, 0755))
            {
                printf ("\nAudio storage dir \"%s\" , subdir \"%s\"  Invalid Entry or File Permission Issue - audio recording not possible.....\n",
                            audioPath, dirName);
                return  AUDIO_STATUS_EFAIL;
            }
        }
    }
    return AUDIO_STATUS_OK;
}

Int32 Audio_captureCreate (Void)
{
    Int32  status = AUDIO_STATUS_OK;
#ifdef  USE_DEFAULT_STORAGE_PATH
    Int8    i;
    Int8 dirName[256];
    Uint8   len;
#endif

    /* TVP5158 CAPTURED AUDIO DATA MAPPING*/
    /* Audio data captured from the TVP5158 are interleaved*/
    /* TVP5158 Daughter card has following configuration for Audio Input (Hardware pins
    --------------------------------------------------------------------------------------
    | AIN15 | AIN13 | AIN11 | AIN9  | AIN7 | AIN5 | AIN3 | AIN1 |
    --------------------------------------------------------------------------------------
    | AIN16 | AIN14 | AIN12 | AIN10 | AIN8 | AIN6 | AIN4 | AIN2 |
    --------------------------------------------------------------------------------------
    */

#if (AUDIO_MAX_CHANNELS==4)
    /*
    Channel Mapping for 4-channels audio capture
    <-----------------64bits----------------->
    <-16bits->
    --------------------------------------------
    | S16-0  | S16-1  | S16-2  | S16-3 |
    --------------------------------------------
    | AIN 3 | AIN 0 | AIN 2 | AIN 1 |
    --------------------------------------------
    */
    audioPhyToDataIndexMap[0]   = 1;
    audioPhyToDataIndexMap[1]   = 3;
    audioPhyToDataIndexMap[2]   = 2;
    audioPhyToDataIndexMap[3]   = 0;

#else

    /*
    Channel Mapping for 16-channels audio capture
    <---------------------------------------------------------------------------------256bits------------------------------------------------------------------------------------>
    <-16bits->
    -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    | S16-0  | S16-1  | S16-2  | S16-3 | S16-4  | S16-5  | S16-6  | S16-7 | S16-8  | S16-9  | S16-10 | S16-11 | S16-12 | S16-13 | S16-14  | S16-15 |
    -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    AIN 16  | AIN 1 | AIN 3 | AIN 5 | AIN 7 | AIN 9 | AIN 11 | AIN 13 | AIN 15 | AIN 2 | AIN 4 | AIN 6 | AIN 8 | AIN 10 | AIN 12 | AIN 14 |
    -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    */

    audioPhyToDataIndexMap[0]   = 1;
    audioPhyToDataIndexMap[1]   = 9;
    audioPhyToDataIndexMap[2]   = 2;
    audioPhyToDataIndexMap[3]   = 10;

    audioPhyToDataIndexMap[4]   = 3;
    audioPhyToDataIndexMap[5]   = 11;
    audioPhyToDataIndexMap[6]   = 4;
    audioPhyToDataIndexMap[7]   = 12;

    audioPhyToDataIndexMap[8]   = 5;
    audioPhyToDataIndexMap[9]   = 13;
    audioPhyToDataIndexMap[10]  = 6;
    audioPhyToDataIndexMap[11]  = 14;

    audioPhyToDataIndexMap[12]  = 7;
    audioPhyToDataIndexMap[13]  = 15;
    audioPhyToDataIndexMap[14]  = 8;
    audioPhyToDataIndexMap[15]  = 0;
#endif
    strcpy(audioPath, "");


#ifdef  USE_DEFAULT_STORAGE_PATH

    strcpy(audioPath, "/home/audio");
	
    if(-1 == access(audioPath, 0))
    {
        remove(audioPath);
        if( 0 != mkdir(audioPath, 0755))
        {
            printf ("\nAUDIO  [%d] >>  Audio storage dir %s not created - audio recording not possible.....\n", __LINE__, audioPath);
            return AUDIO_STATUS_EFAIL;
        }
    }

    for (i=0; i<AUDIO_MAX_CHANNELS; i++)
    {
        strcpy(dirName, audioPath);
        len = strlen(dirName);
        sprintf (&dirName[len], "/%02d", i + 1);
        if(-1 == access((char *) dirName, 0))
        {
            if( 0 != mkdir((char *) dirName, 0755))
            {
                printf ("\nAUDIO  [%d] >>  Audio storage dir %s not created - audio recording not possible.....\n", __LINE__, dirName);
                return  AUDIO_STATUS_EFAIL;
            }
        }
    }
#endif


	/*add info xyx_0226*/
	//printf("#################audio_thr call");
   status = Audio_thrCreate(
                &captureThrHandle,
                (ThrEntryFunc) Audio_recordMain,
                AUDIO_CAPTURE_TSK_PRI,
                AUDIO_CAPTURE_TSK_STACK_SIZE
                );
   printf ("\n Audio capture task created");
   UTILS_assert(  status==AUDIO_STATUS_OK);
   return   status;
}

Int32 Audio_captureStart (Int8      chNum)
{
    deletePreviousRecording (chNum);

    /*edit by xyx 0416*/	
    InitAudioCaptureDevice (AUDIO_MAX_CHANNELS, gChains_ctrl.channelConf[0].audioSample_rate, AUDIO_BUFFER_SIZE);
    //InitAudioCaptureDevice (AUDIO_MAX_CHANNELS, samplingHz, AUDIO_BUFFER_SIZE);
    sleep(1);
    recordChNum = chNum;
    recordAudioFlag = 1;

/*******zys******************
    AENC_CREATE_PARAMS_S  aencParams;

    aencParams.bitRate = 24000;
    aencParams.encoderType =AUDIO_CODEC_TYPE_AAC_LC;
    aencParams.numberOfChannels = 1;
    aencParams.sampleRate = 16000;*/
    
    /*aencParams.bitRate =  gChains_ctrl.channelConf[0].audioBitRate;
    aencParams.encoderType =AUDIO_CODEC_TYPE_AAC_LC;
    aencParams.numberOfChannels = 1;
    aencParams.sampleRate = gChains_ctrl.channelConf[0].audioSample_rate;
    encHandle = Aenc_create(&aencParams);
    if (encHandle)
    {
        printf ("AENC Create done...........\n");
    }
    else
    {
	 printf("AENC Create fail...........\n");
    }*/

    /*xte_xyx_0409 set some param here we use default value*/
    //Audio_playSetSamplingFreq(AUDIO_SAMPLE_RATE_DEFAULT,AUDIO_VOLUME_DEFAULT);

    /*xte_xyx_0409 create audio cap thread*/
    Audio_captureCreate();
	
    return 0;
}


Int32 Audio_captureStop (Void)
{
    recordAudioFlag = 0;
    sleep(1);
    deInitAudioCaptureDevice();
    return 0;
}

Int32 Audio_captureDelete (Void)
{
    printf ("\n Deleting Audio capture task");
    Audio_thrDelete(&captureThrHandle);
    printf (" Audio capture task deleted\n");
    return 0;
}


Int32 Audio_capturePrintStats (Void)
{
    printf ("\n============================\n");
    if (recordAudioFlag == 1)
        printf ("Capture ACTIVE,  ");
    else
        printf ("Capture NOT ACTIVE,  ");
    printf ("Channel %d, ErrCnt [%d], lastErr [%d, %s]\n", recordChNum, captureStats.errorCnt, captureStats.lastError, snd_strerror(captureStats.lastError));
    return 0;
}


Int32 Audio_pramsPrint()
{
  printf ("\n===================================================\n");
  printf ("\nSampling Frequency : %d, Audio Volume Level : %d  \n",samplingHz,audioVolume);
  printf ("\n===================================================\n");
  return 0;
}


Void deletePreviousRecording (Int8 chNum)
{
    Int8 idx;
    Int8 fname[256];
    Uint8 len;

    if (chNum<AUDIO_MAX_CHANNELS)
    {
        //Temp - Delete already existing files
        for (idx=0; idx<AUDIO_MAX_FILES; idx++)
        {
            strcpy(fname, audioPath);
            len = strlen(fname);
            sprintf (&fname[len], "/%02d/%s%02d.pcm", chNum + 1, AUDIO_RECORD_FILE, idx + 1);
            printf ("Removing %s \n", fname);
            remove(fname);
        }
	 //remove ("/audio/01/test.wav");
    }
}

static UInt32 Audio_getFrontendDelay(Int32 curFrameLen)
{
    snd_pcm_sframes_t delay;
    UInt32 audioFEDelay = 0;

    delay = snd_pcm_avail_update(capture_handle);
    if (delay >= 0)
    {
        delay += curFrameLen;
    }
    else
    {
        delay = curFrameLen;
    }
    audioFEDelay = (delay * 1000) /samplingHz;
    OSA_printf("AUDIO:Calculated Audio FE delay:%d\n",
               audioFEDelay);

    return audioFEDelay;
}

/******zys********/
Int32 AUDIO_AACencode(Uint8 *dst, Uint8 *src, Int32 *bufsize)
{
    UInt8                 *inBuf = NULL;
    UInt8                 *outBuf = NULL;
    AENC_PROCESS_PARAMS_S encPrm;
    Int32                 rdIdx, to_read, readBytes;
    Int32                 frameCnt = 0, totalBytes = 0;
    Int32                 inBytes, inBufSize, outBufSize;

    AENC_CREATE_PARAMS_S  aencParams;
	while(AUDIOAACAENC == TRUE)
	{
		aencParams.bitRate =  gChains_ctrl.channelConf[0].audioBitRate;
    		aencParams.encoderType =AUDIO_CODEC_TYPE_AAC_LC;
    		aencParams.numberOfChannels = 1;
    		aencParams.sampleRate = gChains_ctrl.channelConf[0].audioSample_rate;
    		encHandle = Aenc_create(&aencParams);
    		if (encHandle)
    		{
        		printf ("AENC Create done...........\n");
    		}
    		else
    		{
			 printf("AENC Create fail...........\n");
    		}
			break;
	}

	AUDIOAACAENC = FALSE;

    aencParams.encoderType =AUDIO_CODEC_TYPE_AAC_LC;
    if (aencParams.encoderType == AUDIO_CODEC_TYPE_AAC_LC)
    {
        inBufSize = AUDIO_SAMPLES_TO_READ_PER_CHANNEL * AUDIO_SAMPLE_LEN * AUDIO_PLAYBACK_SAMPLE_MULTIPLIER;
        if (inBufSize < aencParams.minInBufSize)
            inBufSize = aencParams.minInBufSize;

        outBufSize = MAX_OUTPUT_BUFFER;
        if (outBufSize < aencParams.minOutBufSize)
            outBufSize = aencParams.minOutBufSize;
    }
    else
    {
        inBufSize = MAX_INPUT_BUFFER;
        outBufSize = MAX_OUTPUT_BUFFER;
    }

    inBytes = inBufSize;

    inBuf = Audio_allocateSharedRegionBuf(inBytes);
    outBuf = Audio_allocateSharedRegionBuf(outBufSize); 

    if (!inBuf || !outBuf)
    {
        printf ("Memory Error....\n");
    }
 
    if (inBuf && outBuf)
    {
        //printf ("\n\n=============== Starting Encode ===================\n");
        rdIdx = 0;
        to_read = inBytes;
        encPrm.outBuf.dataBuf = outBuf;
	 memcpy(inBuf + rdIdx,src,inBytes);
        encPrm.inBuf.dataBufSize = inBytes;
        encPrm.inBuf.dataBuf = inBuf;
        encPrm.outBuf.dataBufSize = outBufSize;
        Aenc_process(encHandle, &encPrm);
       if (encPrm.outBuf.dataBufSize <= 0)
           {
        	printf ("ENC: Encoder didnt generate bytes <remaining - %d>... exiting....\n", readBytes);
              printf ("=============== Encode completed, bytes generated <%d> ================\n",
                       totalBytes);
           }
	if (1)
          {
                    //fwrite(encPrm.outBuf.dataBuf, 1, encPrm.outBuf.dataBufSize, fp_aac);
		 memcpy(dst,encPrm.outBuf.dataBuf,encPrm.outBuf.dataBufSize);
		 dst += encPrm.outBuf.dataBufSize;
          }
         frameCnt++;
         totalBytes += encPrm.outBuf.dataBufSize;
	*bufsize = totalBytes;
  	}
	else
	{
    		printf ("=============== Encode completed, bytes generated <%d> ================\n",totalBytes);
	}
    //App_freeBuf(inBuf, inBufSize, isSharedRegion);
    Audio_freeSharedRegionBuf(outBuf, outBufSize);
    //Aenc_delete(encHandle);
	return dst;
}


Void  Audio_recordMain (Void * arg)
{
    FILE        *fp = NULL;
    //FILE        *fp_test = NULL;
	
    Int8        fname[256];
    Int32       len, idx = 0, fileLen = 0, ii, stored;
    AudioBkt    bkt;
    Uint8       buf[AUDIO_SAMPLES_TO_READ_PER_CHANNEL * AUDIO_SAMPLE_LEN * AUDIO_PLAYBACK_SAMPLE_MULTIPLIER], *tmp;
    Uint8       buf_g711_encode[AUDIO_SAMPLES_TO_READ_PER_CHANNEL * AUDIO_SAMPLE_LEN * AUDIO_PLAYBACK_SAMPLE_MULTIPLIER]; /* Added Support for G711 Codec */
	/* Added Support for G711 alaw Codec edit by xyx*/
    static Int32       i_aenc;
    Uint8       buf_g711a_encode[AUDIO_SAMPLES_TO_READ_PER_CHANNEL * AUDIO_SAMPLE_LEN * AUDIO_PLAYBACK_SAMPLE_MULTIPLIER]; 
    Int32       audioRecordLen = 0;
    TaskCtx     *ctx = arg;
    Uint8       dirlen;
    Int32       err;

/****zys*****/
    Uint8       AACBuf[AUDIO_SAMPLES_TO_READ_PER_CHANNEL * AUDIO_SAMPLE_LEN * AUDIO_PLAYBACK_SAMPLE_MULTIPLIER];
    Int32       AACBufsize;
    Int32       G726_buf;
    Uint8       buf_g726_encode[AUDIO_SAMPLES_TO_READ_PER_CHANNEL * AUDIO_SAMPLE_LEN * AUDIO_PLAYBACK_SAMPLE_MULTIPLIER];
    Uint8       buf_g729_recordbuf[1280];
    Uint8       buf_g729_encode[10];
    Uint8       buf_g729[160];
    g726_state_t *g_state726_16 = NULL; //for g726_16
    g726_state_t *g_state726_24 = NULL; //for g726_24 
    g726_state_t *g_state726_32 = NULL; //for g726_32
    g726_state_t *g_state726_40 = NULL; //for g726_40

    
    if (gChains_ctrl.channelConf[0].audioCodeType == VSYS_AUD_CODEC_G726)
    	{
    
   		 switch(gChains_ctrl.channelConf[0].audioBitRate)
    		{
    		case 16000:
			{
    				g_state726_16 = (g726_state_t *)malloc(sizeof(g726_state_t));
    				g_state726_16 = g726_init(g_state726_16, 8000*2);
			}
			break;
		case 24000:
			{
    				g_state726_24 = (g726_state_t *)malloc(sizeof(g726_state_t));
    				g_state726_24 = g726_init(g_state726_24, 8000*3);
			}
			break;
		case 32000:
			{
    				g_state726_32 = (g726_state_t *)malloc(sizeof(g726_state_t));
    				g_state726_32 = g726_init(g_state726_32, 8000*4);
			}
			break;
		case 40000:
			{
    				g_state726_40 = (g726_state_t *)malloc(sizeof(g726_state_t));
    				g_state726_40 = g726_init(g_state726_40, 8000*5);
			}
			break;
		default:
			{
				g_state726_16 = (g726_state_t *)malloc(sizeof(g726_state_t));
    				g_state726_16 = g726_init(g_state726_16, 8000*2);
			}

		}
    
	}
	
    /*xte_xyx_0313 audiopath must be valid,or file creation will be failed*/
    strcpy(audioPath, ".");//  /mnt

    idx = 0;
    strcpy(bkt.id, "AUD_");

    while (ctx->exitFlag == 0)
    {
        if (recordAudioFlag)
        {
            if (fp == NULL)
            {
            	/*check the path*/
            	printf("the file path is: %s\n", audioPath);
                strcpy(fname, audioPath);
                dirlen = strlen(fname);
                sprintf (&fname[dirlen], "/%02d/%s%02d.pcm", recordChNum + 1, AUDIO_RECORD_FILE, idx + 1);
	
                idx ++;
                fp = fopen(fname, "wb");
		  fp_AAC = fopen("./01/aac.pcm","wb");
		  fp_test = fopen("./01/record.pcm","wb");
		  
                /*xte_xyx_0409 to check the file valid or not*/
		  printf("the error info: %s\n", strerror(errno));
		  //if ((fp == NULL) ||(fp_test == NULL))
		  if (fp == NULL)
			{
				printf("file is empty\n");
				exit(0);
			}
		  else
			{
				printf("file is valid\n");
			}
                printf ("Opened %s for recording..\n", fname);
                fileLen = 0;
                if (idx >= AUDIO_MAX_FILES)
                {
                    idx = 0;
                }
            }

	     if (gChains_ctrl.channelConf[0].audioCodeType == VSYS_AUD_CODEC_G729)
	     {
		   va_g729a_init_encoder();
		   tmp = buf_g729_recordbuf;
            	   ii = 0;
            	   audioRecordLen = 0;
            	   while (ii < 5 && recordAudioFlag)
            	   {
                	len = AUDIO_SAMPLES_TO_READ_PER_CHANNEL;
                	//len = AUDIO_SAMPLE_LEN;

                	/*xte_xyx_0409 do the audio record*/
                	err = RecordAudio(tmp, &len);
                	if (len == 0 || err < 0)
                	{
                    		if (strcmp(snd_strerror(captureStats.lastError),"Success"))
                    		{
                    			printf (" AUDIO >>  CAPTURE ERROR %s, capture wont continue...\n", snd_strerror(captureStats.lastError));
                   			recordAudioFlag = 0;
                    			deInitAudioCaptureDevice();
                    			len = 0;
                    			if (fp)
                       			 fclose(fp);
                    			fp = NULL;
                    			idx = 0;
                    		}
                	}
                	if (len > 0)
                	{
                   	 	audioRecordLen += len;
                    		tmp += len * AUDIO_SAMPLE_LEN * AUDIO_MAX_CHANNELS;
                	}
                	else
                	{
                    		audioRecordLen = 0;
                    		break;
                	}
                	ii++;
            	   }

			tmp = buf_g729_recordbuf;
		//	printf("write buf to fp_test,thie is g729\n");
			fwrite(buf_g729_recordbuf,audioRecordLen*AUDIO_SAMPLE_LEN,1,fp_test);
	     }
	     else
	     {
			tmp = audioRecordBuf;
            		ii = 0;
            		audioRecordLen = 0;
            		while (ii < AUDIO_PLAYBACK_SAMPLE_MULTIPLIER && recordAudioFlag)
            		{
                		len = AUDIO_SAMPLES_TO_READ_PER_CHANNEL;
                		//len = AUDIO_SAMPLE_LEN;

                		/*xte_xyx_0409 do the audio record*/
                		err = RecordAudio(tmp, &len);
                		if (len == 0 || err < 0)
                		{
                    			if (strcmp(snd_strerror(captureStats.lastError),"Success"))
                    			{
                    				printf (" AUDIO >>  CAPTURE ERROR %s, capture wont continue...\n", snd_strerror(captureStats.lastError));
                    				recordAudioFlag = 0;
                    				deInitAudioCaptureDevice();
                    				len = 0;
                    				if (fp)
                        				fclose(fp);
                    				fp = NULL;
                    				idx = 0;
                    			}
                	 	}
                		if (len > 0)
                		{
                  		  	audioRecordLen += len;
                    			tmp += len * AUDIO_SAMPLE_LEN * AUDIO_MAX_CHANNELS;
                		}
                		else
                		{
                    			audioRecordLen = 0;
                    			break;
                		}
                		ii++;
           	 	}

			tmp = audioRecordBuf;
		//	printf("write buf to fp_test\n");
			fwrite(audioRecordBuf,audioRecordLen*AUDIO_SAMPLE_LEN,1,fp_test);
			memcpy(buf,audioRecordBuf,AUDIO_SAMPLES_TO_READ_PER_CHANNEL * AUDIO_SAMPLE_LEN * AUDIO_PLAYBACK_SAMPLE_MULTIPLIER);
	     }


            if (audioRecordLen > 0 && fp)
            {
                /*UInt64 curTime = Avsync_getWallTime() - Audio_getFrontendDelay(audioRecordLen);

                if (fileLen > AUDIO_RECORD_FILE_MAX_SIZE)
                {
                    bkt.numChannels = 1;
		      bkt.samplingRate = gChains_ctrl.channelConf[0].audioSample_rate;
                    bkt.data_size = 0;
                    bkt.timestamp = curTime;  // timestamp from avsync wall time clock.
                    bkt.bkt_status = BKT_END;
#ifndef  AUDIO_STORE_HEADER
                    fwrite(&bkt, sizeof(AudioBkt), 1, fp);
#endif
                    fclose(fp);
                    strcpy(fname, audioPath);
                    dirlen = strlen(fname);
                    sprintf (&fname[dirlen], "/%02d/%s%02d.pcm", recordChNum + 1, AUDIO_RECORD_FILE, idx + 1);
                    printf ("Opened %s for recording..\n", fname);
                    idx ++;
                    fp = fopen(fname, "wb");
                    fileLen = 0;
                    if (idx >= AUDIO_MAX_FILES)
                    {
                        idx = 0;
                    }
                }

                bkt.numChannels = 1;
		  bkt.samplingRate = gChains_ctrl.channelConf[0].audioSample_rate;
                bkt.timestamp = curTime;
               // printf (" AUDIO >>  CAPTURE : rec TS = %lld \n", bkt.timestamp);
                bkt.data_size = audioRecordLen * AUDIO_SAMPLE_LEN;
		  if (gChains_ctrl.channelConf[0].audioCodeType == VSYS_AUD_CODEC_G711)
		  {
                        bkt.data_size = audioRecordLen * AUDIO_SAMPLE_LEN/2;
		  }
                bkt.bkt_status = BKT_VALID;


#ifndef  AUDIO_STORE_HEADER
                fwrite(&bkt, sizeof(AudioBkt), 1, fp);
#endif*/
                /* Data is for recordChNum, store only 1 channel data now */
  /*              stored = 0;
                tmp = audioRecordBuf;
#if 0
                tmp += (recordChNum * AUDIO_SAMPLE_LEN);
#else
                tmp += (audioPhyToDataIndexMap[recordChNum] * AUDIO_SAMPLE_LEN);
#endif

                while (stored < (audioRecordLen * AUDIO_SAMPLE_LEN))
                {
                    memcpy(buf + stored, tmp, AUDIO_CHANNEL_SAMPLE_INTERLEAVE_LEN * AUDIO_SAMPLE_LEN);
                    stored += (AUDIO_CHANNEL_SAMPLE_INTERLEAVE_LEN * AUDIO_SAMPLE_LEN);
                    tmp += (AUDIO_CHANNEL_SAMPLE_INTERLEAVE_LEN * AUDIO_SAMPLE_LEN * AUDIO_MAX_CHANNELS);
                }*/
//#ifdef AUDIO_G711CODEC_ENABLE
                //if (audioG711codec==TRUE) /* Added Support for G711 Codec */
		  if (gChains_ctrl.channelConf[0].audioCodeType == VSYS_AUD_CODEC_G711)
                {
			/*if (fp_test)
			    fwrite(buf, audioRecordLen * AUDIO_SAMPLE_LEN, 1, fp_test);*/	    
			if (gChains_ctrl.channelConf[0].G711uLaw)
			{
				/*xte_xyx_0409 do G711 encode*/
                    		AUDIO_audioEncode(1,(short *)buf_g711_encode,(short *)buf,(Int32)audioRecordLen * AUDIO_SAMPLE_LEN);
				if (fp)
                        		fwrite(buf_g711_encode, audioRecordLen * AUDIO_SAMPLE_LEN/2, 1, fp);
                    		fileLen += audioRecordLen * AUDIO_SAMPLE_LEN/2;
                    		audioSamplesRecorded += audioRecordLen;
				memcpy(audiobag.buf_encode,buf_g711_encode, audioRecordLen * AUDIO_SAMPLE_LEN/2);
				audiobag.data_size=audioRecordLen * AUDIO_SAMPLE_LEN/2;
				audiobag.samplingRate=bkt.samplingRate;
				audiobag.timestamp=bkt.timestamp;
				Audioflag=Audioflag+1;
			}
			else//G711aLaw
			{
				AUDIO_audioEncode(1,(short *)buf_g711_encode,(short *)buf,(Int32)audioRecordLen * AUDIO_SAMPLE_LEN);
				for (i_aenc = 0; i_aenc < AUDIO_SAMPLES_TO_READ_PER_CHANNEL * AUDIO_SAMPLE_LEN * AUDIO_PLAYBACK_SAMPLE_MULTIPLIER; i_aenc++)
				{
					buf_g711a_encode[i_aenc] = ulaw2alaw(buf_g711_encode[i_aenc]);
				}
				/*xte_Sue_0423 write to audiobag struct*/
                           if(Audioflag==0)
                           {
				memcpy(audiobag.buf_encode,buf_g711a_encode, audioRecordLen * AUDIO_SAMPLE_LEN/2);
				audiobag.data_size=audioRecordLen * AUDIO_SAMPLE_LEN/2;
				audiobag.samplingRate=bkt.samplingRate;
				audiobag.timestamp=bkt.timestamp;
				Audioflag=Audioflag+1;
				//printf("Audioflag=%d\n",Audioflag);
                           }
            	             /********************************/
				/*xte_xyx_0409 write the encoded data to file*/
                    		if (fp)
                        		fwrite(buf_g711a_encode, audioRecordLen * AUDIO_SAMPLE_LEN/2, 1, fp);
                    		fileLen += audioRecordLen * AUDIO_SAMPLE_LEN/2;
                    		audioSamplesRecorded += audioRecordLen;
				
			}
                }
		  /*******zys-G726**********/
		  else if (gChains_ctrl.channelConf[0].audioCodeType == VSYS_AUD_CODEC_G726)
		  {
		  		if(g_state726_16)
		  		{
					G726_buf=g726_encode(g_state726_16, buf_g726_encode, (short *)buf, audioRecordLen);
					fwrite(buf_g726_encode,G726_buf,1,fp);
					fileLen += audioRecordLen * AUDIO_SAMPLE_LEN;
                    			audioSamplesRecorded += audioRecordLen;
				}
		  		if(g_state726_24)
		  		{
					G726_buf=g726_encode(g_state726_24, buf_g726_encode, (short *)buf, audioRecordLen);
					fwrite(buf_g726_encode,G726_buf,1,fp);
					fileLen += audioRecordLen * AUDIO_SAMPLE_LEN;
                    			audioSamplesRecorded += audioRecordLen;
		  		}
				if(g_state726_32)
		  		{
					G726_buf=g726_encode(g_state726_32, buf_g726_encode, (short *)buf, audioRecordLen);
					fwrite(buf_g726_encode,G726_buf,1,fp);
					fileLen += audioRecordLen * AUDIO_SAMPLE_LEN;
                    			audioSamplesRecorded += audioRecordLen;
		  		}
				if(g_state726_40)
		  		{
					G726_buf=g726_encode(g_state726_40, buf_g726_encode, (short *)buf, audioRecordLen);
					fwrite(buf_g726_encode,G726_buf,1,fp);
					fileLen += audioRecordLen * AUDIO_SAMPLE_LEN;
                    			audioSamplesRecorded += audioRecordLen;
		  		}
		  }

		  else if (gChains_ctrl.channelConf[0].audioCodeType == VSYS_AUD_CODEC_G729)
		  {
		  	int i;
			for(i = 0;i <8;++i)
			{
				memcpy(buf_g729,buf_g729_recordbuf+i*160,160);
				va_g729a_encoder(buf_g729, buf_g729_encode);
				fwrite(buf_g729_encode,10,1,fp);
			}
			fileLen += audioRecordLen * AUDIO_SAMPLE_LEN;
                     audioSamplesRecorded += audioRecordLen;
		  }

		  /***zys-AAC****/
		  else if (gChains_ctrl.channelConf[0].audioCodeType == VSYS_AUD_CODEC_AAC)
		  {
			//fwrite(buf, audioRecordLen * AUDIO_SAMPLE_LEN, 1, fp);
		   	AUDIO_AACencode(AACBuf,buf,&AACBufsize);
			fwrite(AACBuf,AACBufsize,1,fp);
			fileLen += audioRecordLen * AUDIO_SAMPLE_LEN;
                    audioSamplesRecorded += audioRecordLen;
		  }

                else
//#endif
                {
                    if (fp)
                        fwrite(buf, audioRecordLen * AUDIO_SAMPLE_LEN, 1, fp);
                    fileLen += audioRecordLen * AUDIO_SAMPLE_LEN;
                    audioSamplesRecorded += audioRecordLen;
                }
            }
            else
            {
                zero_samples_count++;
            }	
        }
        else
        {
            if (fp)
            {
                fclose(fp);
		  fclose(fp_AAC);
		  fclose(fp_test);
                fp = NULL;
		  fp_test = NULL;
		  fp_AAC = NULL;
            }
		/*if (fp_test)
		{
			fclose (fp_test);
			fp_test = NULL;
		}*/
            sleep(2);
            idx = 0;
        }
    }
  //printf ("AUDIO >> Exiting audio capture task............\n");
}

static Int32 InitAudioCaptureDevice (Int32 channels, UInt32 sample_rate, Int32 driver_buf_size)
{
    snd_pcm_hw_params_t *hw_params;
    Int32 err;

    if ((err = snd_pcm_open (&capture_handle, ALSA_CAPTURE_DEVICE, SND_PCM_STREAM_CAPTURE, 0)) < 0)
    {
        fprintf (stderr, "AUDIO >>  cannot open audio device plughw:1,0 (%s)\n", snd_strerror (err));
        return  -1;
    }
    //printf("skip the err info\n");
    //printf ("AUDIO >>  opened %s device\n", ALSA_CAPTURE_DEVICE);
    if ((err = snd_pcm_hw_params_malloc (&hw_params)) < 0)
    {
        AUD_DEVICE_PRINT_ERROR_AND_RETURN("cannot allocate hardware parameter structure (%s)\n", err, capture_handle);
    }

    if ((err = snd_pcm_hw_params_any (capture_handle, hw_params)) < 0)
    {
        AUD_DEVICE_PRINT_ERROR_AND_RETURN("cannot initialize hardware parameter structure (%s)\n", err, capture_handle);
    }

    if ((err = snd_pcm_hw_params_set_access (capture_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
    {
        AUD_DEVICE_PRINT_ERROR_AND_RETURN("cannot set access type (%s)\n", err, capture_handle);
    }

    if ((err = snd_pcm_hw_params_set_format (capture_handle, hw_params, SND_PCM_FORMAT_S16_LE)) < 0)
    {
        AUD_DEVICE_PRINT_ERROR_AND_RETURN("cannot set sample format (%s)\n", err, capture_handle);
    }

    if ((err = snd_pcm_hw_params_set_rate_near (capture_handle, hw_params, &sample_rate, 0)) < 0)
    {
        AUD_DEVICE_PRINT_ERROR_AND_RETURN("cannot set sample rate (%s)\n", err, capture_handle);
    }

    if ((err = snd_pcm_hw_params_set_channels (capture_handle, hw_params, channels)) < 0)
    {
        AUD_DEVICE_PRINT_ERROR_AND_RETURN("cannot set channel count (%s)\n", err, capture_handle);
    }

    if ((err = snd_pcm_hw_params_set_buffer_size (capture_handle, hw_params, driver_buf_size)) < 0)
    {
        AUD_DEVICE_PRINT_ERROR_AND_RETURN("cannot set buffer size (%s)\n", err, capture_handle);
    }

    if ((err = snd_pcm_hw_params (capture_handle, hw_params)) < 0)
    {
        AUD_DEVICE_PRINT_ERROR_AND_RETURN("cannot set parameters (%s)\n", err, capture_handle);
    }

    snd_pcm_hw_params_free (hw_params);

    if ((err = snd_pcm_prepare (capture_handle)) < 0)
    {
        AUD_DEVICE_PRINT_ERROR_AND_RETURN("cannot prepare audio interface for use (%s)\n", err, capture_handle);
    }
	
    return 0;
}


static Int32    RecordAudio(Uint8 *buffer, Int32 *numSamples)
{
    Int32 err = -1;

    if (capture_handle)
    {
        if ((err = snd_pcm_readi (capture_handle, buffer, *numSamples)) != *numSamples)	//length of frames,*numSamples*2=length of data
        {
            //printf (" AUDIO >> read from audio interface failed (%s)\n", snd_strerror (err));
            *numSamples = 0;
            captureStats.errorCnt++;
            captureStats.lastError = err;
        }

    }
    else
    {
        *numSamples = 0;
    }
    return err;
}


static Int32 deInitAudioCaptureDevice(Void)
{

    if (capture_handle)
    {
        snd_pcm_drain(capture_handle);
        snd_pcm_close(capture_handle);
        capture_handle = NULL;
//      printf ("AUDIO >> Device closed\n");
    }
    return 0;
}
