#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>
#include <IPAddress.h>

bool initNetworkManager();
void serviceNetwork();

bool networkIsConnected();
bool networkHasLink();
bool networkUsingDhcpRuntime();
bool networkDhcpFallbackActive();
IPAddress networkGetLocalIP();

void networkRequestReconfigure();

#endif  // NETWORK_MANAGER_H
