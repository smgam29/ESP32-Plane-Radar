#pragma once

void statusScreenPortal(const char* host_url);
void statusScreenConnectFailed();
void statusScreenWifiReset();

/** Saved-network connect animation (call Tick until connect finishes). */
void statusScreenConnectingBegin(const char* ssid);
void statusScreenConnectingTick();
