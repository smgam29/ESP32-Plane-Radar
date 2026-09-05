#pragma once

void statusScreenPortal(const char* host_url);
void statusScreenConnectFailed();
void statusScreenWifiReset();
/** Temporary identification screen for distinguishing radars on one LAN. */
void statusScreenDeviceIdentity(const char* device_id, const char* ip_address);

/** Saved-network connect animation (call Tick until connect finishes). */
void statusScreenConnectingBegin(const char* ssid);
void statusScreenConnectingTick();
