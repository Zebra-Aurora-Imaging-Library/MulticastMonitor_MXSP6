﻿/*************************************************************************************/
/*
 * File name: MulticastMonitor.cpp
 *
 * Synopsis:  This program shows how to perform IP Multicast with GigE Vision devices.
 *
 *            To do this you must have a network capable of delivering a Multicast
 *            service over IPv4. This requires the use of routers and LAN switches
 *            that support the Internet Group Management Protocol (IGMP). Some manual
 *            configuration of you LAN switches might be required. More information
 *            can be found in the IP Multicast section of Matrox GigE Vision
 *            Assistant's help file.
 *
 *      Note: This example must be used along with the MulticastMaster program
 *            connected to the same GigE Vision device and running on another PC.
 *
 * Copyright © Matrox Electronic Systems Ltd., 1992-YYYY.
 * All Rights Reserved
 */
 
#include <mil.h>
#if M_MIL_USE_WINDOWS
#include <windows.h>
#endif

 /* Number of images in the buffering grab queue.
    Generally, increasing this number gives better real-time grab.
  */
#define BUFFERING_SIZE_MAX 20
#define IPV4_ADDRESS_SIZE  20

/* User's processing function hook data structure. */
typedef struct
   {
   MIL_ID  MilDigitizer;
   MIL_ID  MilDisplay;
   MIL_ID  MilImageDisp;
   MIL_ID  MilGrabBufferList[BUFFERING_SIZE_MAX];
   MIL_INT MilGrabBufferListSize;
   MIL_INT ProcessedImageCount;
   MIL_INT CorruptImageCount;
   MIL_INT FrameSizeX;
   MIL_INT FrameSizeY;
   MIL_INT FramePixelFormat;
   bool DataFormatChanged;
   MIL_INT64 SourceDataFormat;
   MIL_STRING MulticastAddress;
   MIL_ID Event;
   MIL_STRING DeviceVendor;
   MIL_STRING DeviceModel;
   } HookDataStruct;

/* Function prototypes.                  */
void AllocateGrabBuffers(MIL_INT MilSystem, HookDataStruct* HookDataPtr);
void FreeGrabBuffers(HookDataStruct* HookDataPtr);
void AdaptToDataFormatChange(MIL_INT MilSystem, HookDataStruct* HookDataPtr);
void PrintCameraInfo(HookDataStruct* HookDataPtr);
MIL_INT MFTYPE ProcessingFunction(MIL_INT HookType,
                                  MIL_ID HookId,
                                  void* HookDataPtr);
void GetMulticastInfo(MIL_STRING& oMulticastAddress, MIL_INT& oUdpPort);

// need link with Ws2_32.lib
#pragma comment(lib, "Ws2_32.lib")

/* Main function. */
/* ---------------*/

int MosMain(void)
{
   MIL_ID MilApplication;
   MIL_ID MilSystem     ;
   MIL_INT ProcessFrameCount  = 0;
   MIL_INT DigProcessInProgress = M_FALSE;
   MIL_INT SystemType = 0;
   MIL_DOUBLE ProcessFrameRate= 0;
   HookDataStruct UserHookData;
   MIL_STRING MulticastAddr;
   MIL_INT PortNumber = 0;

   /* Allocate defaults. */
   MappAllocDefault(M_DEFAULT, &MilApplication, &MilSystem, M_NULL, M_NULL, M_NULL);

   /* This example only runs on a MIL GigE Vision system type. */
   MsysInquire(MilSystem, M_SYSTEM_TYPE, &SystemType);
   if(SystemType != M_SYSTEM_GIGE_VISION_TYPE)
      {
      MosPrintf(MIL_TEXT("This example requires a M_GIGE_VISION system type.\n"));
      MosPrintf(MIL_TEXT("Please change system type in milconfig.\n"));
      MosPrintf(MIL_TEXT("\nPress <Enter> to quit.\n"));
      MosGetch();
      MappFreeDefault(MilApplication, MilSystem, M_NULL, M_NULL, M_NULL);
      return 0;
      }
      
   /* Print message and instructions. */
   MosPrintf(MIL_TEXT("This example demonstrates the use of IP Multicast with GigE Vision "));
   MosPrintf(MIL_TEXT("devices.\n"));
   MosPrintf(MIL_TEXT("It allocates a monitor digitizer that can grab from a GigE Vision\n"));
   MosPrintf(MIL_TEXT("device provided a Multicast master digitizer is allocated on the "));
   MosPrintf(MIL_TEXT("same device.\n\n"));
   MosPrintf(MIL_TEXT("This example must be used along with MulticastMaster.cpp connected "));
   MosPrintf(MIL_TEXT("to the same\n"));
   MosPrintf(MIL_TEXT("GigE Vision device and running on another PC.\n\n"));   
   MosPrintf(MIL_TEXT("A monitor Multicast digitizer does not have read access to the GigE "));
   MosPrintf(MIL_TEXT("Vision\n"));   
   MosPrintf(MIL_TEXT("device. Because of this some manual configuration of the digitizer "));
   MosPrintf(MIL_TEXT("is required.\n\n"));
   MosPrintf(MIL_TEXT("Press <Enter> to continue.\n"));
   MosGetch();

   /* Allocate synchronization event. */
   MthrAlloc(MilSystem, M_EVENT, M_NOT_SIGNALED+M_AUTO_RESET, M_NULL, M_NULL,
      &UserHookData.Event);

   /* Allocate a display and buffers. */
   MdispAlloc(MilSystem, M_DEFAULT, MIL_TEXT("M_DEFAULT"), M_DEFAULT,
      &UserHookData.MilDisplay);

   /* Allocate a monitor Multicast digitizer.                                        */
   /* The default gigevision_multicast_monitor.dcf sets the following parameters:    */
   /* SizeX: 640                                                                     */
   /* SizeY: 480                                                                     */
   /* PixelFormat: Mono8                                                             */
   /*                                                                                */
   /* This example will override these default setting after the first grab has been */
   /* made. The MdigProcess hook function will compare these default settings with   */
   /* the settings of the currently grabbed frame. If needed an event will be set    */
   /* will momentarily stop the grab apply the new settings, re-allocate grab buffers*/
   /* and resume grabbing.                                                           */
   MdigAlloc(MilSystem, M_DEFAULT, MIL_TEXT("gigevision_multicast_monitor.dcf"),
      M_GC_MULTICAST_MONITOR, &UserHookData.MilDigitizer);

   /* Allocate buffers. */
   AllocateGrabBuffers(MilSystem, &UserHookData);
   MdispSelect(UserHookData.MilDisplay, UserHookData.MilImageDisp);

   /* Manual digitizer configuration */
   
   /* If the GigE Vision device supports packet resends and the user wants to use      */
   /* packet resends on the monitor enable it manually.                                */
   MdigControl(UserHookData.MilDigitizer, M_GC_PACKET_RESEND, M_ENABLE);

   /* Inquire the pixel format as specified from the DCF. */
   MdigInquire(UserHookData.MilDigitizer, M_GC_PIXEL_FORMAT, &UserHookData.FramePixelFormat);

   /* Validate if the gigevision_multicast_monitor.dcf has multicast parameters set.   */
   MdigInquire(UserHookData.MilDigitizer, M_GC_STREAM_CHANNEL_MULTICAST_ADDRESS_STRING,
      MulticastAddr);
   MdigInquire(UserHookData.MilDigitizer, M_GC_LOCAL_STREAM_PORT, &PortNumber);
   if(MulticastAddr == MIL_TEXT("0.0.0.0") || PortNumber == 0)
      {
      /* No multicast info found. Ask the user for the Multicast IP address and UDP    */
      /* port number to use.                                                           */
      GetMulticastInfo(MulticastAddr, PortNumber);   
      /* Pass the Multicast IP addresss and UDP port number to MIL and apply the settings.*/
      MdigControl(UserHookData.MilDigitizer, M_GC_STREAM_CHANNEL_MULTICAST_ADDRESS_STRING,
         MulticastAddr);
      MdigControl(UserHookData.MilDigitizer, M_GC_LOCAL_STREAM_PORT, PortNumber);
      MdigControl(UserHookData.MilDigitizer, M_GC_UPDATE_MULTICAST_INFO, M_DEFAULT);
      }

   /* Initialize the User's processing function data structure. */
   UserHookData.ProcessedImageCount = 0;
   UserHookData.CorruptImageCount   = 0;
   UserHookData.FrameSizeX          = 0;
   UserHookData.FrameSizeY          = 0;
   UserHookData.DataFormatChanged   = false;

   /* Get default ROI parameters. They will get updated from the MdigProcess() hook    */
   /* function if needed when the first grab is performed.                             */
   MdigInquire(UserHookData.MilDigitizer, M_SIZE_X, &UserHookData.FrameSizeX);
   MdigInquire(UserHookData.MilDigitizer, M_SIZE_Y, &UserHookData.FrameSizeY);

   /* Print info related to the device we are connected to. */
   PrintCameraInfo(&UserHookData);

   /* Start the processing. The processing function is called for every frame grabbed. */
   MdigProcess(UserHookData.MilDigitizer, UserHookData.MilGrabBufferList,
      UserHookData.MilGrabBufferListSize, M_START, M_DEFAULT, ProcessingFunction,
      &UserHookData);

   /* NOTE: Now the main() is free to perform other tasks 
                                                    while the processing is executing. */
   /* -------------------------------------------------------------------------------- */

   /* Adjust the monitor digitizer according to received image information. */
   AdaptToDataFormatChange(MilSystem, &UserHookData);

   MdigInquire(UserHookData.MilDigitizer, M_DIG_PROCESS_IN_PROGRESS, &DigProcessInProgress);
   if(DigProcessInProgress == M_TRUE)
      {
      /* Stop the processing. */
      MdigProcess(UserHookData.MilDigitizer, UserHookData.MilGrabBufferList,
         UserHookData.MilGrabBufferListSize, M_STOP, M_DEFAULT, ProcessingFunction,
         &UserHookData);
      }

   /* Print statistics. */
   MdigInquire(UserHookData.MilDigitizer, M_PROCESS_FRAME_COUNT,  &ProcessFrameCount);
   MdigInquire(UserHookData.MilDigitizer, M_PROCESS_FRAME_RATE,   &ProcessFrameRate);
   MosPrintf(MIL_TEXT("\n\n%lld frames grabbed at %.1f frames/sec (%.1f ms/frame).\n"),
      (long long)ProcessFrameCount, ProcessFrameRate, 1000.0/ProcessFrameRate);
   MosPrintf(MIL_TEXT("Press <Enter> to end.\n\n"));
   MosGetch();

   FreeGrabBuffers(&UserHookData);

   MdispFree(UserHookData.MilDisplay);
   MdigFree(UserHookData.MilDigitizer);

   MthrFree(UserHookData.Event);

   /* Release defaults. */
   MappFreeDefault(MilApplication, MilSystem, M_NULL, M_NULL, M_NULL);

   return 0;
}

/* Get Multicast IP address and UDP port number to use.                      */
/* -----------------------------------------------------------------------   */
void GetMulticastInfo(MIL_STRING& oMulticastAddress, MIL_INT& oUdpPort)
   {
   MIL_INT32 lUdpPort = 0;
   MosPrintf(MIL_TEXT("Enter Multicast Address to use (between 224.0.0.0 - 239.255.255.255)"));
   MosPrintf(MIL_TEXT("\n\n"));
   oMulticastAddress.assign(IPV4_ADDRESS_SIZE, MIL_TEXT('\0'));
#if M_MIL_USE_WINDOWS
   MOs_scanf_s(MIL_TEXT("%15s"), &oMulticastAddress[0], IPV4_ADDRESS_SIZE);
#else
   scanf(MIL_TEXT("%15s"), &oMulticastAddress[0]);
#endif

   for (size_t i = 0; i < oMulticastAddress.size(); i++)
      {
      if (oMulticastAddress[i] == MIL_TEXT('\0'))
         {
         oMulticastAddress.resize(i);
         break;
         }
      }

   MosPrintf(MIL_TEXT("Enter UDP port number to use  : "));
#if M_MIL_USE_WINDOWS
   MOs_scanf_s(MIL_TEXT("%d"), &lUdpPort);
#else
   scanf(MIL_TEXT("%d"), (int *)&lUdpPort);
#endif
   oUdpPort = (MIL_INT)lUdpPort;
   }

/* Allocate acquisition and display buffers.                                 */
/* -----------------------------------------------------------------------   */
void AllocateGrabBuffers(MIL_INT MilSystem, HookDataStruct* HookDataPtr)
   {
   MIL_INT n = 0;
   MdigInquire(HookDataPtr->MilDigitizer, M_SOURCE_DATA_FORMAT,
      &HookDataPtr->SourceDataFormat);

   /* Allocate the display buffer and clear it. */
   MbufAllocColor(MilSystem,
                  MdigInquire(HookDataPtr->MilDigitizer, M_SIZE_BAND, M_NULL),
                  MdigInquire(HookDataPtr->MilDigitizer, M_SIZE_X, M_NULL),
                  MdigInquire(HookDataPtr->MilDigitizer, M_SIZE_Y, M_NULL),
                  MdigInquire(HookDataPtr->MilDigitizer, M_TYPE, M_NULL),
                  M_IMAGE+M_DISP+M_GRAB+M_PROC+HookDataPtr->SourceDataFormat,
                  &HookDataPtr->MilImageDisp);
   MbufClear(HookDataPtr->MilImageDisp, M_COLOR_BLACK);

   /* Allocate the grab buffers and clear them. */
   MappControl(M_ERROR, M_PRINT_DISABLE);
   for(HookDataPtr->MilGrabBufferListSize = 0; 
       HookDataPtr->MilGrabBufferListSize<BUFFERING_SIZE_MAX;
       HookDataPtr->MilGrabBufferListSize++)
      {
      MbufAllocColor(MilSystem,
                     MdigInquire(HookDataPtr->MilDigitizer, M_SIZE_BAND, M_NULL),
                     MdigInquire(HookDataPtr->MilDigitizer, M_SIZE_X, M_NULL),
                     MdigInquire(HookDataPtr->MilDigitizer, M_SIZE_Y, M_NULL),
                     MdigInquire(HookDataPtr->MilDigitizer, M_TYPE, M_NULL),
                     M_IMAGE+M_GRAB+M_PROC+HookDataPtr->SourceDataFormat,
                     &HookDataPtr->MilGrabBufferList[HookDataPtr->MilGrabBufferListSize]);

      if (HookDataPtr->MilGrabBufferList[HookDataPtr->MilGrabBufferListSize])
         {
         MbufClear(HookDataPtr->MilGrabBufferList[HookDataPtr->MilGrabBufferListSize],
            M_COLOR_WHITE);
         }
      else
         break;
      }
   MappControl(M_ERROR, M_PRINT_ENABLE);


   }

/* Free MIL acquisition and display buffers.                                 */
/* -----------------------------------------------------------------------   */
void FreeGrabBuffers(HookDataStruct* HookDataPtr)
   {
   while(HookDataPtr->MilGrabBufferListSize > 0)
      MbufFree(HookDataPtr->MilGrabBufferList[--HookDataPtr->MilGrabBufferListSize]);

   MbufFree(HookDataPtr->MilImageDisp);
   }

/* This routine queries periodically to determine if a data format change    */
/* has been detected. If so then the grab is stopped, the new data format    */
/* is applied, the grab buffers are re-allocated and the grab is re started. */
/* -----------------------------------------------------------------------   */
void AdaptToDataFormatChange(MIL_INT MilSystem, HookDataStruct* HookDataPtr)
   {
   MIL_INT DigProcessInProgress = M_FALSE;
   bool Done = false;

   do
      {     
      /* Sleep. */
      MthrWait(HookDataPtr->Event, M_EVENT_WAIT+M_EVENT_TIMEOUT(1000), M_NULL);

      /* Validate if data format has changed. */
      if(HookDataPtr->DataFormatChanged)
         {
         // Reset variable
         HookDataPtr->DataFormatChanged = (HookDataPtr->DataFormatChanged ? false : true);

         /* The camera's data format has changed we must:               */
         /* 1- Stop any grabs that had previously been started.         */
         /* 2- Update digitizer's data format.                          */
         /* 3- Reallocate grab buffers that conform to the data format. */
         /* 4- Restart the grab.                                        */

         /* 1- Stop grabbing? */
         MdigInquire(HookDataPtr->MilDigitizer, M_DIG_PROCESS_IN_PROGRESS,
            &DigProcessInProgress);
         if(DigProcessInProgress)
            {
            MdigProcess(HookDataPtr->MilDigitizer, HookDataPtr->MilGrabBufferList,
               HookDataPtr->MilGrabBufferListSize, M_STOP, M_DEFAULT, ProcessingFunction,
               HookDataPtr);
            }

         /* 2- Update data format. */
         MdigControl(HookDataPtr->MilDigitizer, M_SOURCE_SIZE_X, HookDataPtr->FrameSizeX);
         MdigControl(HookDataPtr->MilDigitizer, M_SOURCE_SIZE_Y, HookDataPtr->FrameSizeY);
         MdigControl(HookDataPtr->MilDigitizer, M_GC_PIXEL_FORMAT,
            HookDataPtr->FramePixelFormat);

         PrintCameraInfo(HookDataPtr);
         
         /* 3- Reallocate grab buffers. */
         FreeGrabBuffers(HookDataPtr);
         AllocateGrabBuffers(MilSystem, HookDataPtr);
         MdispSelect(HookDataPtr->MilDisplay, HookDataPtr->MilImageDisp);

         /* 4- Resume grab. */
         MdigProcess(HookDataPtr->MilDigitizer, HookDataPtr->MilGrabBufferList,
            HookDataPtr->MilGrabBufferListSize, M_START, M_DEFAULT, ProcessingFunction,
            HookDataPtr);
         }

      /* Must we quit? */
      if(MosKbhit())
         {
         MosGetch();
         Done = true;
         }
      }
   while(!Done);
   }

/* Prints information regarding the device this slave digitizer is connected to. */
/* -----------------------------------------------------------------------       */
void PrintCameraInfo(HookDataStruct* HookDataPtr)
   {
   MIL_INT PortNumber = 0;

#if M_MIL_USE_WINDOWS
   /* Clear console. */
   system("cls");
#endif

   if(HookDataPtr->DeviceVendor.empty() && HookDataPtr->DeviceModel.empty())
      {
      /* Inquire camera vendor name. */
      MdigInquire(HookDataPtr->MilDigitizer, M_CAMERA_VENDOR, HookDataPtr->DeviceVendor);
      
      /* Inquire camera model name. */
      MdigInquire(HookDataPtr->MilDigitizer, M_CAMERA_MODEL, HookDataPtr->DeviceModel);
      }

   /* Inquire the Multicast address used. */
   MdigInquire(HookDataPtr->MilDigitizer, M_GC_STREAM_CHANNEL_MULTICAST_ADDRESS_STRING,
      HookDataPtr->MulticastAddress);
   MdigInquire(HookDataPtr->MilDigitizer, M_GC_LOCAL_STREAM_PORT, &PortNumber);

   /* Print camera info. */
   MosPrintf(MIL_TEXT("\n------------------- Monitor digitizer connection status. ---------"));
   MosPrintf(MIL_TEXT("------------\n\n"));
   MosPrintf(MIL_TEXT("Connected to             %s %s\n"), HookDataPtr->DeviceVendor.c_str(),
      HookDataPtr->DeviceModel.c_str());
   MosPrintf(MIL_TEXT("Device pixel format:     0x%x\n"), (int)HookDataPtr->FramePixelFormat);
   MosPrintf(MIL_TEXT("Device AOI:              %lld x %lld\n"), (long long)HookDataPtr->FrameSizeX,
      (long long)HookDataPtr->FrameSizeY);
   MosPrintf(MIL_TEXT("Multicast address:       %s\n"), HookDataPtr->MulticastAddress.c_str());
   MosPrintf(MIL_TEXT("UDP Port:                %lld\n"), (long long)PortNumber);

   MosPrintf(MIL_TEXT("\nPress <Enter> to stop.\n\n"));
   }

/* User's processing function called every time a grab buffer is modified. */
/* -----------------------------------------------------------------------*/

/* Local defines. */
#define STRING_LENGTH_MAX  20
#define STRING_POS_X       20
#define STRING_POS_Y       20

MIL_INT MFTYPE ProcessingFunction(MIL_INT HookType,
                                  MIL_ID HookId,
                                  void* HookDataPtr)
   {
   HookDataStruct *UserHookDataPtr = (HookDataStruct *)HookDataPtr;
   MIL_ID ModifiedBufferId;
   MIL_TEXT_CHAR Text[STRING_LENGTH_MAX]= {MIL_TEXT('\0'),};
   MIL_INT IsFrameCorrupt = M_FALSE;
   MIL_INT FrameSizeX = 0;
   MIL_INT FrameSizeY = 0;
   MIL_INT FramePixelFormat = 0;
   MIL_INT FramePacketSize = 0;

   /* Retrieve the MIL_ID of the grabbed buffer. */
   MdigGetHookInfo(HookId, M_MODIFIED_BUFFER+M_BUFFER_ID,   &ModifiedBufferId);
   MdigGetHookInfo(HookId, M_CORRUPTED_FRAME,               &IsFrameCorrupt);
   MdigGetHookInfo(HookId, M_GC_FRAME_SIZE_X,               &FrameSizeX);
   MdigGetHookInfo(HookId, M_GC_FRAME_SIZE_Y,               &FrameSizeY);
   MdigGetHookInfo(HookId, M_GC_FRAME_PIXEL_TYPE,           &FramePixelFormat);
   MdigGetHookInfo(HookId, M_GC_PACKET_SIZE,                &FramePacketSize);

   UserHookDataPtr->ProcessedImageCount++;
   if(IsFrameCorrupt)
      UserHookDataPtr->CorruptImageCount++;

   /* Check if a data format change has occurred. */
   if((FrameSizeX       != UserHookDataPtr->FrameSizeX) ||
      (FrameSizeY       != UserHookDataPtr->FrameSizeY) ||
      (FramePixelFormat != UserHookDataPtr->FramePixelFormat))
      {
      UserHookDataPtr->FrameSizeX = FrameSizeX;
      UserHookDataPtr->FrameSizeY = FrameSizeY;
      UserHookDataPtr->FramePixelFormat = FramePixelFormat;

      // Do not set on first grab, we must initialize data once first.
      UserHookDataPtr->DataFormatChanged = M_TRUE;
      // Wake up main thread to perform buffer re-allocation.
      MthrControl(UserHookDataPtr->Event, M_EVENT_SET, M_SIGNALED);
      }

   /* Print and draw the frame count. */
   MosSprintf(Text, STRING_LENGTH_MAX, MIL_TEXT("%lld"), 
                                       (long long)UserHookDataPtr->ProcessedImageCount);
   MgraText(M_DEFAULT, ModifiedBufferId, STRING_POS_X, STRING_POS_Y, Text);

   /* Perform the processing and update the display. */
   MbufCopy(ModifiedBufferId, UserHookDataPtr->MilImageDisp);
   
   
   return 0;
   }
