// Include the most common headers from the C standard library
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Include the main libnx system header, for Switch development
#include <switch.h>

// Other stuff
#include "con_manager.hpp"
#include "udp_manager.hpp"
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <optional>
#include <mutex>

// Log text
#define IS_RELEASE 0

static const SocketInitConfig sockInitConf = {
    .bsdsockets_version = 1,

    .tcp_tx_buf_size        = 0x200,
    .tcp_rx_buf_size        = 0x400,
    .tcp_tx_buf_max_size    = 0x400,
    .tcp_rx_buf_max_size    = 0x800,
    // We're not using tcp anyways

    .udp_tx_buf_size = 0x2600,
    .udp_rx_buf_size = 0xA700,

    .sb_efficiency = 2,

    .num_bsd_sessions = 3,
    .bsd_service_type = BsdServiceType_User
};

extern "C" {
    // Sysmodules should not use applet*.
    u32 __nx_applet_type = AppletType_None;
    // Sysmodules will normally only want to use one FS session.
    //u32 __nx_fs_num_sessions = 1;


    // Adjust size as needed.
    #define INNER_HEAP_SIZE 0x80000
    size_t nx_inner_heap_size = INNER_HEAP_SIZE;
    char   nx_inner_heap[INNER_HEAP_SIZE];

    void __libnx_init_time(void);
    void __libnx_initheap(void);
    void __appInit(void);
    void __appExit(void);


    void __libnx_initheap(void)
    {
        void*  addr = nx_inner_heap;
        size_t size = nx_inner_heap_size;

        // Newlib
        extern char* fake_heap_start;
        extern char* fake_heap_end;

        fake_heap_start = (char*)addr;
        fake_heap_end   = (char*)addr + size;
    }

    // Init/exit services, update as needed.
    void __attribute__((weak)) __appInit(void)
    {
        Result rc;
        // Initialize default services.
        rc = smInitialize();
        if (R_FAILED(rc))
            fatalThrow(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));

        if (hosversionGet() == 0) {
            rc = setsysInitialize();
            if (R_SUCCEEDED(rc)) {
                SetSysFirmwareVersion fw;
                rc = setsysGetFirmwareVersion(&fw);
                if (R_SUCCEEDED(rc))
                    hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
                setsysExit();
            }
        }

        // Enable this if you want to use HID.
        rc = hidInitialize();
        if (R_FAILED(rc))
            fatalThrow(MAKERESULT(Module_Libnx, LibnxError_InitFail_HID));

        rc = hiddbgInitialize();
        if (R_FAILED(rc))
            fatalThrow(MAKERESULT(Module_Libnx, LibnxError_InitFail_HID));

        //Enable this if you want to use time.
        rc = timeInitialize();
        if (R_FAILED(rc))
            fatalThrow(MAKERESULT(Module_Libnx, LibnxError_InitFail_Time));

        __libnx_init_time();

        rc = fsInitialize();
        if (R_FAILED(rc))
            fatalThrow(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));

        rc = fsdevMountSdmc();
        if (R_FAILED(rc))
            fatalThrow(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));

        static HiddbgHdlsSessionId* controllerSessionID = new HiddbgHdlsSessionId;
        rc = hiddbgAttachHdlsWorkBuffer(controllerSessionID);
        if (R_FAILED(rc))
            fatalThrow(MAKERESULT(Module_Libnx, LibnxError_InitFail_HID));

        char* buffer = (char*)malloc(sizeof(u64) + 20);
        sprintf(buffer, "session id is %ld", controllerSessionID->id);
        printToFile(buffer);

        rc = pmdmntInitialize();
        if (R_FAILED(rc)) 
            fatalThrow(rc);

        rc = ldrDmntInitialize();
        if (R_FAILED(rc)) 
            fatalThrow(rc);

        rc = pminfoInitialize();
        if (R_FAILED(rc)) 
            fatalThrow(rc);

        rc = socketInitialize(&sockInitConf);
        if (R_FAILED(rc))
            fatalThrow(rc);

        rc = capsscInitialize();
        if (R_FAILED(rc))
            fatalThrow(rc);
        
    }

    void __attribute__((weak)) userAppExit(void);

    void __attribute__((weak)) __appExit(void)
    {
        // Cleanup default services.
        fsdevUnmountAll();
        fsExit();
        timeExit();//Enable this if you want to use time.
        hidExit();// Enable this if you want to use HID.
        smExit();
        socketExit();
    }
}

static Mutex fileMutex;
int printToFile(const char* myString)
{
    #if IS_RELEASE == 0
    time_t currenttime = time(0);
    tm* timeinfo;
    timeinfo = localtime(&currenttime);

    char* outtime = new char[72]; // don't really care about buffer overflows lol
    
    sprintf(outtime, "%02d/%02d/%d %02d:%02d:%02d", timeinfo->tm_mon + 1,
            timeinfo->tm_mday, timeinfo->tm_year + 1900,
            timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);

    mutexLock(&fileMutex);
    FILE *log = fopen("/hidplus/log.txt", "a+t");
    if (log != nullptr) {
            fprintf(log, "[%s] %s\n", outtime, myString);
            fclose(log);
    }
    mutexUnlock(&fileMutex);
    return 0;
    #else
    return -1;
    #endif
}

// Main program entrypoint
u64 mainLoopSleepTime = 50;
static Thread network_thread, logging_thread;
int main(int argc, char* argv[])
{
    // Initialization code can go here.

    // Your code / main loop goes here.
    // If you need threads, you can use threadCreate etc.

    printToFile("READY NEW!");
    printToFile("MEGA READY! :)");
    FakeController testController;

    threadCreate(&network_thread, networkThread, NULL, NULL, 0x1000, 0x30, 3);
    threadCreate(&logging_thread, loggingThread, NULL, NULL, 0x1000, 0x31, 4);
    threadStart(&network_thread);
    threadStart(&logging_thread);
    
    while (appletMainLoop()) // Main loop
    {

        //u64 kDown = hidKeysDown(CONTROLLER_P1_AUTO);

        /*if (kDown & KEY_PLUS)
        {
            // Start the controller
            testController.initialize();
            // Press A
            testController.controllerState.buttons = 1;
            hiddbgSetHdlsState(testController.controllerHandle, &testController.controllerState);
            // Unpress A
            svcSleepThread(1000 * 1e+6L);
            testController.controllerState.buttons = 0;
            hiddbgSetHdlsState(testController.controllerHandle, &testController.controllerState);
        }*/

        svcSleepThread(mainLoopSleepTime * 1e+6L);

    }

    // Deinitialization and resources clean up code can go here.
    return 0;
}
