/*******************************************************************************
*                                                                             *
* Copyright (c) 2009 Texas Instruments Incorporated - http://www.ti.com/      *
*                        ALL RIGHTS RESERVED                                  *
*                                                                             *
******************************************************************************/

#include "helloWorldLink_priv.h"
#include "helloWorldAlg.h"
#include "ti/sdo/fc/dskt2/dskt2.h"
#include <mcfw/src_bios6/utils/utils_mem.h>

static UInt8 gScratchId = 1;

/* ===================================================================
*  @func     HelloWorldLink_algDelete
*
*  @desc     Function creates the instance of hellow world algorithm
*
*  @modif    This function modifies the following structures
*
*  @inputs   This function takes the following inputs
*            HelloWorldLink_Obj
*            Object to hello world link
*
*  @outputs  
*            
*
*  @return   Status
*			  FVID2_SOK: If outout object created successfuly 
*  ==================================================================
*/

Int32 HelloWorldLink_algDelete(HelloWorldLink_Obj * pObj)
{
    Int32 scratchId = gScratchId;

    Vps_printf(" %d: HELLOWORLD    : Algorithm Delete in progress !!!\n",
               Utils_getCurTimeInMsec());

    if(pObj->algHndl == NULL)
        return FVID2_EFAIL;

    /* Deactivate algorithm */
    DSKT2_deactivateAlg(scratchId, (IALG_Handle)pObj->algHndl);

    DSKT2_freeAlg(scratchId, (IALG_Handle)pObj->algHndl);

    Vps_printf(" %d: HELLOWORLD    : Algorithm Delete Done !!!\n",
               Utils_getCurTimeInMsec());

	return FVID2_SOK;
}

/* ===================================================================
*  @func     HelloWorldLink_algCreate
*
*  @desc     Function creates the instance of hellow world algorithm
*
*  @modif    This function modifies the following structures
*
*  @inputs   This function takes the following inputs
*            HelloWorldLink_Obj
*            Object to hello world link
*
*  @outputs  
*            
*
*  @return   Status
*			  FVID2_SOK: If outout object created successfuly 
*  ==================================================================
*/

static Int32 HelloWorldLink_algCreate(HelloWorldLink_Obj * pObj)
{
    HELLOWORLDALG_createPrm       algCreatePrm;
    IALG_Fxns           *algFxns = (IALG_Fxns *)&HELLOWORLDALG_TI_IALG;

    Vps_printf(" %d: HELLOWORLD    : Algorithm Create in progress !!!\n",
               Utils_getCurTimeInMsec());

    algCreatePrm.maxWidth    = pObj->createArgs.maxWidth;
    algCreatePrm.maxHeight   = pObj->createArgs.maxHeight;
    algCreatePrm.maxStride   = pObj->createArgs.maxStride;
    algCreatePrm.maxChannels = pObj->createArgs.maxChannels;
    /*************************************************************************/
	/* Create algorithm instance and get hello world algo handle.            */
    /* DSKT2 is memory manager and creates instance for algorithm that has   */
	/* XDAIS/Alg interface APIs implemented (numAlloc, memAlloc and algInit) */
    /*************************************************************************/

    pObj->algHndl = DSKT2_createAlg((Int)gScratchId,
            (IALG_Fxns *)algFxns, NULL,(IALG_Params *)&algCreatePrm);

    if(pObj->algHndl == NULL)
    {
        Vps_printf(" %d: HELLOWORLD    : Algorithm Create ERROR !!!\n",
               Utils_getCurTimeInMsec());
        return FVID2_EFAIL;
    }


	/*************************************************************************/
    /* Once algorithm instace is created, initialize channel specific        */
    /* parameters here                                                       */
    /*************************************************************************/
	
    /* for(chNum = 0; chNum < algCreatePrm.maxChannels; chNum++)
	{
	    HELLOWORLDALG_TI_setPrms(pObj->algHndl, HELLOWORLDALG_chPrm, chNum)
	} */
	

    Vps_printf(" %d: HELLOWORLD    : Algorithm Create Done !!!\n",
               Utils_getCurTimeInMsec());

 return FVID2_SOK;
}



/* ===================================================================
*  @func     HelloWorldLink_createOutObj
*
*  @desc     Function creates the queue for output buffer and allocate
*            the buffers. These buffers can be send as input to next
*            links (running on any core)
*
*  @modif    This function modifies the following structures
*
*  @inputs   This function takes the following inputs
*            HelloWorldLink_Obj
*            Object to hello world link
*
*  @outputs  
*            
*
*  @return   Status
*			  FVID2_SOK: If outout object created successfuly 
*  ==================================================================
*/

static Int32 HelloWorldLink_createOutObj(HelloWorldLink_Obj * pObj)
{
    HelloWorldLink_OutObj *pOutObj;
    System_LinkChInfo *pOutChInfo;
    Int32 status;
    UInt32 bufIdx;
    Int i,j,queueId,chId;
    UInt32 totalBufCnt;

    /*************************************************************************/
    /* One link can have multiple output queues with different/same output   */
    /* data queued to it's output queue. Create here outobj for all output   */
    /* queue                                                                 */
    /*************************************************************************/
    for(queueId = 0; queueId < HELLOWORLD_LINK_MAX_OUT_QUE; queueId++)
    {

        pOutObj = &pObj->outObj[queueId];    

        pObj->outObj[queueId].numAllocPools = 1;

        pOutObj->bufSize[0] = HELLOWORLD_LINK_OUT_BUF_SIZE;

        /*********************************************************************/
        /* Set the buffer alignment as per need. Typically 128 suggested for */
        /* better cache and DMA efficiency.                                  */
        /*********************************************************************/
        pOutObj->bufSize[0] = VpsUtils_align(pOutObj->bufSize[0], 
            HELLOWORLD_BUFFER_ALIGNMENT);

        /*********************************************************************/
        /* Create output queue                                               */
        /*********************************************************************/
        status = Utils_bitbufCreate(&pOutObj->bufOutQue, TRUE, FALSE,
            pObj->outObj[queueId].numAllocPools);
        UTILS_assert(status == FVID2_SOK);

        totalBufCnt = 0;

        /*********************************************************************/
        /* Allocate output buffers                                           */
        /*********************************************************************/
        for (i = 0; i < pOutObj->numAllocPools; i++)
        {
            /*****************************************************************/		
            /* Number of output buffers per channel. In this example hello   */
            /* world, outNumBufs set via user input.                         */
            /*****************************************************************/
            pOutObj->outNumBufs[i] = (pObj->createArgs.maxChannels * 
                pObj->createArgs.numBufsPerCh);

            for (j = 0; j < pObj->createArgs.maxChannels; j++)
            {
                pOutObj->ch2poolMap[j] =  i;
            }

            /*****************************************************************/		
            /* Allocate the buffer from shared memory pool for bitstream     */
            /* buffer. If you like to allocate memory for frame buffers,     */
            /* you can either call Utils_tilerFrameAlloc() to allocate       */
            /* memory from tiler memory or you can call Utils_memFrameAlloc  */
            /* to allocate memory from shared buffer pool                    */
            /*****************************************************************/			
            status = Utils_memBitBufAlloc(&(pOutObj->outBufs[totalBufCnt]),
                pOutObj->bufSize[i],
                pOutObj->outNumBufs[i]);
            UTILS_assert(status == FVID2_SOK);

            /*****************************************************************/		
            /* Push the buffers to the output queue that's just been created */
            /*****************************************************************/			
            for (bufIdx = 0; bufIdx < pOutObj->outNumBufs[i]; bufIdx++)
            {
                UTILS_assert((bufIdx + totalBufCnt) < HELLOWORLD_LINK_MAX_OUT_FRAMES);
                pOutObj->outBufs[bufIdx + totalBufCnt].allocPoolID = i;
                pOutObj->outBufs[bufIdx + totalBufCnt].doNotDisplay =
                    FALSE;
                status =
                    Utils_bitbufPutEmptyBuf(&pOutObj->bufOutQue,
                    &pOutObj->outBufs[bufIdx +
                    totalBufCnt]);
                UTILS_assert(status == FVID2_SOK);
            }
            totalBufCnt += pOutObj->outNumBufs[i];
        }
    }

    pObj->info.numQue = HELLOWORLD_LINK_MAX_OUT_QUE;

    /*************************************************************************/
    /* queInfo is used by next link to create it's instance.                 */
    /* Set numCh - number of channel information                             */
    /*************************************************************************/
    for (queueId = 0u; queueId < HELLOWORLD_LINK_MAX_OUT_QUE; queueId++)
    {
        pObj->info.queInfo[queueId].numCh = pObj->inQueInfo.numCh;
    }

    /*************************************************************************/
    /* Set the information for output buffer of each channel. Again, next    */
    /* link  connected to hello world link will use this information to     */
    /* create it's own instance.                                             */
    /*************************************************************************/
    for (chId = 0u; chId < pObj->inQueInfo.numCh; chId++)
    {
        for (queueId = 0u; queueId < HELLOWORLD_LINK_MAX_OUT_QUE; queueId++)
        {
            pOutChInfo = &pObj->info.queInfo[queueId].chInfo[chId];
            pOutChInfo->bufType = SYSTEM_BUF_TYPE_VIDBITSTREAM;
            pOutChInfo->codingformat = NULL;
            pOutChInfo->memType = NULL;
            pOutChInfo->scanFormat = pObj->inQueInfo.chInfo[chId].scanFormat;
            pOutChInfo->width = pObj->inQueInfo.chInfo[chId].width;
            pOutChInfo->height = pObj->inQueInfo.chInfo[chId].height;
        }
    }

    return (status);
}


/* ===================================================================
*  @func     HelloWorldLink_algCreate
*
*  @desc     Creates HelloWorld link instance
*
*  @modif    This function modifies the following structures
*
*  @inputs   This function takes the following inputs
*            <HelloWorldLink_Obj>
*            Object to hello world link
*            <HelloWorldLink_CreateParams>
*            Create time parameters passed by the user
*
*  @outputs  <argument name>
*            Description of usage
*
*  @return   Status of instance creation
*  ==================================================================
*/
Int32 HelloWorldLink_create(HelloWorldLink_Obj * pObj, 
                            HelloWorldLink_CreateParams * pPrm)
{
    Int32 status;

    Vps_printf(" %d: HELLOWORLD : Create in progress !!!\n", 
        Utils_getCurTimeInMsec());

    /*************************************************************************/
    /* copy the create time parameters passed from host to local object      */
    /*************************************************************************/
    memcpy(&pObj->createArgs, pPrm, sizeof(*pPrm));

    /*************************************************************************/
    /* Get frame header information from previous link input queue            */
    /*************************************************************************/
    status = System_linkGetInfo(pPrm->inQueParams.prevLinkId, &pObj->inTskInfo);
    UTILS_assert(status == FVID2_SOK);

    /*************************************************************************/
    /* Make sure queid information provided in create params is less then    */
    /* total number of queues in previous link (returned by                  */
    /* System_linkGetInfo module                                             */
    /*************************************************************************/
    UTILS_assert(pPrm->inQueParams.prevLinkQueId < pObj->inTskInfo.numQue);

    /*************************************************************************/
    /* Make a local copy of previous link queue number connected to          */
    /* helloWorld link                                                       */
    /*************************************************************************/
    memcpy(&pObj->inQueInfo,
        &pObj->inTskInfo.queInfo[pPrm->inQueParams.prevLinkQueId],
        sizeof(pObj->inQueInfo));

    /*************************************************************************/
    /* Check if helloWorld link instance can handle the number of channels   */
    /* from previous links                                                   */
    /*************************************************************************/
    UTILS_assert(pObj->inQueInfo.numCh <= HELLOWORLD_LINK_MAX_CH);


    /*************************************************************************/
    /* Create an instance to hello world algorithm                           */
    /*************************************************************************/
    status = HelloWorldLink_algCreate(pObj);
    UTILS_assert(status == FVID2_SOK);

    /*************************************************************************/
    /* If you have more algorithms that sequentially process upon the        */
    /* received input frame, you can create instances to those algorithms    */
    /* in series here                                                        */
    /*************************************************************************/
    //xxxLink_Create();
    //yyyLink_Create(); 


    /*************************************************************************/
    /* All links creates and manages the output buffers they produce. Input  */
    /* buffers managed by prior link that produced that buffer               */
    /* This is generic link and hence user input taken from A8 if link needs */
    /* to produce and hence create an output buffer.                         */
    /*************************************************************************/
    if (pObj->createArgs.createOutBuf1)
        HelloWorldLink_createOutObj(pObj);

    Vps_printf(" %d: HELLOWORLD : Create Done !!!\n", Utils_getCurTimeInMsec());
    return FVID2_SOK;
}


/* ===================================================================
*  @func     HelloWorldLink_algDelete
*
*  @desc     Delete HelloWorld link instance
*
*  @modif    This function modifies the following structures
*
*  @inputs   This function takes the following inputs
*            <HelloWorldLink_Obj>
*            Object to hello world link
*
*  @outputs  <argument name>
*            Description of usage
*
*  @return   Status of instance creation
*  ==================================================================
*/
Int32 HelloWorldLink_delete(HelloWorldLink_Obj * pObj)
{    
    Int32 status;
    Int32 i,outId,bitbuf_index;
    HelloWorldLink_OutObj *pOutObj;

    Vps_printf(" %d: HELLOWORLD : Delete in progress !!!\n", 
        Utils_getCurTimeInMsec());

    /*************************************************************************/
    /* Make a call to your algrithm instance deletion here. At this place    */
    /* you should free memory and DMA resource (if any) held by your algo    */
    /*************************************************************************/
    //status = HelloWorldLink_algDelete(&pObj->Alg1);
    //UTILS_assert(status == FVID2_SOK);

    /*************************************************************************/
    /* Free the output buffer and it's queue created by helloWorld link      */
    /*************************************************************************/
    for (outId = 0; outId < HELLOWORLD_LINK_MAX_OUT_QUE; outId++)
    {
        {
            pOutObj = &pObj->outObj[outId];

            status = Utils_bitbufDelete(&pOutObj->bufOutQue);
            UTILS_assert(status == FVID2_SOK);
            bitbuf_index = 0;

            for (i = 0; i < pOutObj->numAllocPools; i++)
            {
                UTILS_assert((pOutObj->outBufs[bitbuf_index].bufSize ==
                    pOutObj->bufSize[i]));
                status = Utils_memBitBufFree(&pOutObj->outBufs[bitbuf_index],
                    pOutObj->outNumBufs[i]);
                UTILS_assert(status == FVID2_SOK);
                bitbuf_index += pOutObj->outNumBufs[i];
            }
        }
    }

    Vps_printf(" %d: HELLOWORLD : Delete Done !!!\n", Utils_getCurTimeInMsec());

    return FVID2_SOK;
}

/* ===================================================================
*  @func     HelloWorldLink_algProcessData
*
*  @desc     Process upon the input frame received from previous link
*            At this place, make a call to algorithm process call
*
*  @modif    This function modifies the following structures
*
*  @inputs   This function takes the following inputs
*            <HelloWorldLink_Obj>
*            Object to hello world link
*
*  @outputs  <argument name>
*            Description of usage
*
*  @return   Status of process call
*  ==================================================================
*/
Int32 HelloWorldLink_processData(HelloWorldLink_Obj * pObj)
{
    UInt32 frameId;
    System_LinkInQueParams *pInQueParams;
    FVID2_Frame *pFrame;
    FVID2_FrameList frameList;
    Bitstream_Buf *pOutBuf;
    Utils_BitBufHndl *bufOutQue;
    Int32 status;
    System_FrameInfo *pInFrameInfo;
    Bitstream_BufList outBitBufList;
    Bitstream_BufList tempBitBufList; 
	HELLOWORLDALG_Result *pHelloWorldResult = NULL;
    Uint8* pBufferStart;
    UInt8 *pBuffer;
    int i,j;
    UInt8 *y0pixel,*y1pixel,*upixel,*vpixel;


    status = FVID2_EFAIL;
    pInQueParams = &pObj->createArgs.inQueParams;

    bufOutQue = &pObj->outObj[0].bufOutQue;
    outBitBufList.numBufs=0;
    /*************************************************************************/
    /* Get input frames from previous link output queue                      */
    /*************************************************************************/
    System_getLinksFullFrames(pInQueParams->prevLinkId,
        pInQueParams->prevLinkQueId, &frameList);

    /*************************************************************************/
    /* If input frame received from queue, get the output buffer from out    */
    /* queue and process the frame                                           */
    /*************************************************************************/
    if (frameList.numFrames)
    {
        Vps_printf(" %d frameList.numFrames:%d  !!!\n", 
        Utils_getCurTimeInMsec(),frameList.numFrames);
        //pObj->totalFrameCount += frameList.numFrames;

        /*********************************************************************/
        /* Get the outout buffer for each input buffer and process the frame */
        /*                                                                   */
        /*********************************************************************/
        for(frameId=0; frameId< frameList.numFrames; frameId++)
        {
            Bool doFrameDrop = FALSE;

			pFrame = frameList.frames[frameId];

            //pChObj = &pObj->chObj[chIdx];

            //pChObj->inFrameRecvCount++;


            /*********************************************************************/
            /* If current frame needs to be processed, get the output buffer     */
            /* from output queue                                                 */
            /*********************************************************************/
            if(pObj->createArgs.createOutBuf1)
            {
                pOutBuf = NULL;
		//  Vps_printf(" bufOutQue->fullQue.count :%d \r\n",bufOutQue->empytQue[0].count);
                status = Utils_bitbufGetEmptyBuf(bufOutQue,
                    &pOutBuf,
                    0, //pObj->outObj.ch2poolMap[chIdx], /*Need to change later.*/
                    BIOS_NO_WAIT);
		//   Vps_printf(" bufOutQue->fullQue.count :%d \r\n",bufOutQue->emptyQue[0].count);

                if(!((status == FVID2_SOK) && (pOutBuf)))
                {
                    doFrameDrop = TRUE;
                }

                /*********************************************************************/
                /* Captured YUV buffers time stamp is passed by all links to next    */
                /* buffers bu copying the input buffer times stamp to it's output    */
                /* buffer. This is used for benchmarking system latency.             */
                /*********************************************************************/
                pInFrameInfo = (System_FrameInfo *) pFrame->appData;
                pOutBuf->lowerTimeStamp = (UInt32)(pInFrameInfo->ts64 & 0xFFFFFFFF);
                pOutBuf->upperTimeStamp = (UInt32)(pInFrameInfo->ts64 >> 32);
                pOutBuf->channelNum = pFrame->channelNum;
                //pOutBuf->fillLength = ;
                //pOutBuf->frameWidth = ;
                //pOutBuf->frameHeight = ;

				pHelloWorldResult = (HELLOWORLDALG_Result *) pOutBuf->addr;
            }

            /*********************************************************************/
            /* Now input and output buffers available and this frame need not be */
            /* skipped, make a call to the algorithm process call and process the*/
            /* frame.                                                            */
            /*********************************************************************/
            if(doFrameDrop == FALSE)
            {
                /*********************************************************************/
                /* Pointer to Luma buffer for processing.                            */
                /*********************************************************************/
                //pObj->chParams[chIdx].curFrame = pFrame->addr[0][0];

                pInFrameInfo = (System_FrameInfo *) pFrame->appData;

                //curTime = Utils_getCurTimeInMsec();
          //      Vps_printf("width:%d  ,height: %d  \r\n",pInFrameInfo->rtChInfo.width,pInFrameInfo->rtChInfo.height);   
                pBufferStart=pFrame->addr[0][0];
                for(i=0;i<pInFrameInfo->rtChInfo.height;i++){
                      pBuffer=pBufferStart+(i*pInFrameInfo->rtChInfo.width*2);
                      for(j=0;j<pInFrameInfo->rtChInfo.width/2;j++){
                               y0pixel=pBuffer;
                               y1pixel=pBuffer+2;
                               upixel=pBuffer+1;
                               vpixel=pBuffer+3;
                      if(i==1&&j<5)
			// Vps_printf("y0pixel: %d ,upixel: %d ,y1pixel: %d ,vpixel: %d \r\n",*y0pixel,*upixel,*y1pixel,*vpixel);
                               *upixel=0;
                               *vpixel=0;
                               pBuffer+=4;
			       //if(i==1&&j<5)
			//  Vps_printf("y0pixel: %d ,upixel: %d ,y1pixel: %d ,vpixel: %d \r\n",*y0pixel,*upixel,*y1pixel,*vpixel);
                      }

                      

                }
		//  Vps_printf("pbufferstart:%d  ,pBuffer:%d  ,y0pixel:%d  ,vpixel:%d   \r\n",pBufferStart,pBuffer,y0pixel,vpixel);
               
                /*********************************************************************/
                /* Make a call to algorithm process call here                        */
                /*********************************************************************/
                HELLOWORLDALG_TI_process(pObj->algHndl, 0, pHelloWorldResult);//HelloWorldLink_algProcess();

                /*********************************************************************/
                /* Benchmark frame process time                                      */
                /*********************************************************************/
                //pChObj->inFrameProcessTime += (Utils_getCurTimeInMsec() - curTime);

                //pChObj->inFrameProcessCount++;
                outBitBufList.bufs[outBitBufList.numBufs] = pOutBuf;
                outBitBufList.numBufs++;
                tempBitBufList.numBufs=outBitBufList.numBufs;

            }
            else
            {
                //pChObj->inFrameUserSkipCount++;
            }
        }
    }
   
    /*********************************************************************/
    /* Release the input buffer to previous link queue                   */
    /*********************************************************************/
    System_putLinksEmptyFrames(pInQueParams->prevLinkId,
        pInQueParams->prevLinkQueId, &frameList);

    /*********************************************************************/
    /* If output buffers available, push them to the output queue        */
    /*********************************************************************/
    if(pObj->createArgs.createOutBuf1)
    {
        if (outBitBufList.numBufs)
        {
	  // Vps_printf("2.numBufs: %d \r\n",outBitBufList.numBufs);
	    // Vps_printf(" bufOutQue->fullQue->maxElements :%d \r\n",bufOutQue->fullQue.maxElements);
          //  Vps_printf("3. bufOutQue->fullQue.count :%d \r\n",bufOutQue->fullQue.count);
            status = Utils_bitbufPutFull(bufOutQue,
                &outBitBufList);
            UTILS_assert(status == FVID2_SOK);
            
            Utils_bitbufPutEmpty(&pObj->outObj[0].bufOutQue,&outBitBufList);    
            Utils_bitbufGetFull(&pObj->outObj[0].bufOutQue,&tempBitBufList,100);
	    //  Vps_printf("4. bufOutQue->fullQue.count :%d \r\n",bufOutQue->fullQue.count);
            /*********************************************************************/
            /* Inform next link of output buffer availability                    */
            /*********************************************************************/
          //  System_sendLinkCmd(pObj->createArgs.outQueParams.nextLink,
            //    SYSTEM_CMD_NEW_DATA);
            status = FVID2_SOK;        
        }
        else
        {
            status = FVID2_EFAIL; 
        } 
    }
    return status;
}
