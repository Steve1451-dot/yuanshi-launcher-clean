/*
    Derived from Borealis' Switch platform wrapper.
    Copyright 2019 natinusala, WerWolv and p-sam
    Licensed under the Apache License, Version 2.0.
    http://www.apache.org/licenses/LICENSE-2.0
    Distributed on an AS IS BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND.
*/
#ifdef CLOUDGENSHIN_NATIVE
#include <switch.h>
#include <unistd.h>

static int cloud_nxlink_socket=-1;

void userAppInit(void) {
    appletLockExit();
    SocketInitConfig config=*socketGetDefaultInitConfig();
    const AppletType type=appletGetAppletType();
    if(type==AppletType_Application || type==AppletType_SystemApplication) {
        config.num_bsd_sessions=12;config.sb_efficiency=8;
    } else {
        config.num_bsd_sessions=2;config.sb_efficiency=1;
    }
    socketInitialize(&config);
#ifdef DEBUG
    cloud_nxlink_socket=nxlinkStdio();
#endif
    romfsInit();
    hidsysInitialize();
    // The native app and Borealis never call any INS APIs. Requesting ins:s
    // can wait indefinitely on yuzu before main(), so do not open this unused
    // service. Keep required sockets, fonts, FS, power and NIFM unchanged.
    plInitialize(PlServiceType_User);
    setsysInitialize();
    setInitialize();
    psmInitialize();
    nifmInitialize(NifmServiceType_User);
    lblInitialize();
}

void userAppExit(void) {
    lblExit();nifmExit();psmExit();setExit();setsysExit();plExit();
    hidsysExit();romfsExit();
    if(cloud_nxlink_socket!=-1) close(cloud_nxlink_socket);
    socketExit();appletUnlockExit();
}
#endif
