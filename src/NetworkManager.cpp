#include "NetworkManager.h"

#include <QNEthernet.h>

#include "Config.h"
#include "NetworkOSC.h"
#include "OLED.h"
#include "Utils.h"
#include "WebServer.h"

using namespace qindesign::network;

namespace {

enum class NetState : uint8_t {
  LinkDown,
  WaitingIp,
  Ready
};

static NetState g_state = NetState::LinkDown;
static bool g_stateInitialized = false;
static bool g_connected = false;
static bool g_usingDhcp = false;
static bool g_dhcpFallbackActive = false;
static bool g_linkUp = false;
static volatile bool g_linkEventPending = false;
static volatile bool g_linkEventUp = false;
static bool g_reconfigurePending = false;
static bool g_forceDisplayRefresh = false;

static unsigned long g_linkDebounceUntil = 0;
static unsigned long g_dhcpWaitStartMs = 0;
static unsigned long g_ipMissingSince = 0;

static IPAddress g_lastReadyIp(0, 0, 0, 0);
static NetworkConfig g_lastAppliedConfig{};
static bool g_hasAppliedConfig = false;

static const unsigned long kLinkDebounceMs = 300;
static const unsigned long kIpLossGraceMs = 1500;

static bool hasValidIp(const IPAddress& ip) {
  return ip != IPAddress(0, 0, 0, 0);
}

static const char* stateName(NetState state) {
  switch (state) {
    case NetState::LinkDown: return "LinkDown";
    case NetState::WaitingIp: return "WaitingIp";
    case NetState::Ready: return "Ready";
  }
  return "?";
}

static bool networkLayerChanged(const NetworkConfig& a, const NetworkConfig& b) {
  return a.useDHCP != b.useDHCP ||
         a.staticIP != b.staticIP ||
         a.gateway != b.gateway ||
         a.subnet != b.subnet;
}

static bool networkServiceChanged(const NetworkConfig& a, const NetworkConfig& b) {
  return a.oscPort != b.oscPort;
}

static bool networkDisplayChanged(const NetworkConfig& a, const NetworkConfig& b) {
  return a.oscPort != b.oscPort ||
         a.sendToIP != b.sendToIP ||
         a.useDHCP != b.useDHCP ||
         a.staticIP != b.staticIP;
}

static void showCurrentNetworkStatus(bool linkStable, NetState state, const IPAddress& effectiveIp) {
  if (!display.isInitialized()) {
    return;
  }

  OLEDNetworkState oledState = OLEDNetworkState::LinkDown;
  IPAddress displayIp(0, 0, 0, 0);

  if (!linkStable) {
    oledState = g_linkUp ? OLEDNetworkState::ConnectingLink : OLEDNetworkState::LinkDown;
  } else if (g_dhcpFallbackActive) {
    oledState = OLEDNetworkState::DhcpStaticFallback;
    displayIp = hasValidIp(effectiveIp) ? effectiveIp : netConfig.staticIP;
  } else if (state == NetState::Ready) {
    oledState = g_usingDhcp ? OLEDNetworkState::ConnectedDhcp : OLEDNetworkState::ConnectedStatic;
    displayIp = effectiveIp;
  } else {
    oledState = g_usingDhcp ? OLEDNetworkState::ConnectingDhcp : OLEDNetworkState::ConnectingStatic;
    displayIp = hasValidIp(effectiveIp) ? effectiveIp : netConfig.staticIP;
  }

  display.showNetworkStatus(oledState, displayIp, netConfig.oscPort, netConfig.sendToIP);
}

static void applyStaticConfigRuntime() {
  g_usingDhcp = false;
  Ethernet.setDHCPEnabled(false);
  bool ok = Ethernet.begin(netConfig.staticIP, netConfig.subnet, netConfig.gateway);
  if (!ok) {
    NETWORK_ERROR_PRINT("Static configuration apply failed");
  } else {
    NETWORK_DEBUG_PRINTF("Static config applied: %s", ipToString(netConfig.staticIP).c_str());
  }
}

static void applyDhcpConfigRuntime() {
  g_usingDhcp = true;
  Ethernet.setDHCPEnabled(true);
  bool ok = Ethernet.begin();
  if (!ok) {
    NETWORK_ERROR_PRINT("DHCP begin failed; waiting for link/service loop retry");
  } else {
    NETWORK_DEBUG_PRINT("DHCP begin requested");
  }
}

static void applyConfiguredNetworkMode(unsigned long now) {
  g_dhcpFallbackActive = false;
  g_ipMissingSince = 0;
  g_lastReadyIp = IPAddress(0, 0, 0, 0);

  if (netConfig.useDHCP) {
    applyDhcpConfigRuntime();
    g_dhcpWaitStartMs = now;
  } else {
    applyStaticConfigRuntime();
    g_dhcpWaitStartMs = 0;
  }

  g_lastAppliedConfig = netConfig;
  g_hasAppliedConfig = true;
}

static void handleStateChange(NetState newState, bool linkStable, const IPAddress& effectiveIp) {
  bool stateChanged = !g_stateInitialized || (newState != g_state);
  bool ipChanged = newState == NetState::Ready && effectiveIp != g_lastReadyIp;

  if (!stateChanged && !ipChanged) {
    return;
  }

  if (stateChanged) {
    NETWORK_DEBUG_PRINTF("%s -> %s (ip=%s)",
                         g_stateInitialized ? stateName(g_state) : "Initial",
                         stateName(newState),
                         ipToString(effectiveIp).c_str());
  }

  if (stateChanged && newState == NetState::LinkDown) {
    stopNetworkServices();
  }

  if (newState == NetState::Ready) {
    startNetworkServices();
    startWebServer();
  }

  g_state = newState;
  g_stateInitialized = true;
  g_connected = (newState == NetState::Ready);
  if (newState == NetState::Ready && hasValidIp(effectiveIp)) {
    g_lastReadyIp = effectiveIp;
  } else if (newState == NetState::LinkDown) {
    g_lastReadyIp = IPAddress(0, 0, 0, 0);
  }

  showCurrentNetworkStatus(linkStable, newState, effectiveIp);
}

}  // namespace

bool initNetworkManager() {
  Ethernet.setHostname(kServiceName);

  Ethernet.onLinkState([](bool up) {
    g_linkEventUp = up;
    g_linkEventPending = true;
  });

  g_linkUp = Ethernet.linkState();
  g_linkDebounceUntil = millis() + kLinkDebounceMs;
  g_state = NetState::LinkDown;
  g_stateInitialized = false;
  g_connected = false;
  g_reconfigurePending = false;
  g_forceDisplayRefresh = false;
  g_hasAppliedConfig = false;

  showCurrentNetworkStatus(false, NetState::LinkDown, IPAddress(0, 0, 0, 0));
  applyConfiguredNetworkMode(millis());

  IPAddress initialIp = Ethernet.localIP();
  if (g_linkUp && hasValidIp(initialIp)) {
    g_lastReadyIp = initialIp;
  }

  return g_linkUp && hasValidIp(initialIp);
}

void serviceNetwork() {
  const unsigned long now = millis();
  static bool lastLinkStable = false;

  if (g_linkEventPending) {
    bool up = g_linkEventUp;
    g_linkEventPending = false;
    if (up != g_linkUp) {
      g_linkUp = up;
      g_linkDebounceUntil = now + kLinkDebounceMs;
      NETWORK_DEBUG_PRINTF("Link event up=%d", up ? 1 : 0);
    }
  }

  bool polledLinkUp = Ethernet.linkState();
  if (polledLinkUp != g_linkUp) {
    g_linkUp = polledLinkUp;
    g_linkDebounceUntil = now + kLinkDebounceMs;
    NETWORK_DEBUG_PRINTF("Polled link change up=%d", polledLinkUp ? 1 : 0);
  }

  bool linkStable = lastLinkStable;
  if (g_linkDebounceUntil == 0 || (long)(now - g_linkDebounceUntil) >= 0) {
    linkStable = g_linkUp;
    g_linkDebounceUntil = 0;
  }

  if (g_reconfigurePending) {
    bool needNetworkReapply = !g_hasAppliedConfig || networkLayerChanged(g_lastAppliedConfig, netConfig);
    bool needServiceRestart = !needNetworkReapply && g_hasAppliedConfig && networkServiceChanged(g_lastAppliedConfig, netConfig);
    bool needDisplayRefresh = g_forceDisplayRefresh ||
                              !g_hasAppliedConfig ||
                              networkDisplayChanged(g_lastAppliedConfig, netConfig);

    if (needNetworkReapply) {
      NETWORK_DEBUG_PRINT("Applying requested network reconfigure");
      stopNetworkServices();
      applyConfiguredNetworkMode(now);
      g_stateInitialized = false;
    } else {
      g_lastAppliedConfig = netConfig;
      g_hasAppliedConfig = true;
    }

    if (needServiceRestart && networkIsConnected()) {
      NETWORK_DEBUG_PRINT("Restarting network services after port change");
      restartNetworkServices();
    }

    if (needDisplayRefresh) {
      showCurrentNetworkStatus(linkStable, g_state, networkGetLocalIP());
    }

    g_reconfigurePending = false;
    g_forceDisplayRefresh = false;
  }

  if (linkStable && !lastLinkStable) {
    NETWORK_DEBUG_PRINT("Link stable; reapplying configured network mode");
    applyConfiguredNetworkMode(now);
  } else if (!linkStable && lastLinkStable) {
    NETWORK_ERROR_PRINT("Link lost");
    g_dhcpFallbackActive = false;
    g_dhcpWaitStartMs = 0;
    g_ipMissingSince = 0;
    g_lastReadyIp = IPAddress(0, 0, 0, 0);
  }
  lastLinkStable = linkStable;

  IPAddress currentIp = Ethernet.localIP();
  IPAddress effectiveIp = currentIp;
  bool ipValid = hasValidIp(currentIp);

  if (netConfig.useDHCP && linkStable && !ipValid && g_usingDhcp && !g_dhcpFallbackActive) {
    if (g_dhcpWaitStartMs == 0) {
      g_dhcpWaitStartMs = now;
    }
    if ((long)(now - g_dhcpWaitStartMs) >= (long)kDHCPTimeout) {
      NETWORK_ERROR_PRINT("DHCP timeout; applying runtime static fallback");
      applyStaticConfigRuntime();
      g_dhcpFallbackActive = true;
      currentIp = Ethernet.localIP();
      effectiveIp = hasValidIp(currentIp) ? currentIp : netConfig.staticIP;
      ipValid = hasValidIp(currentIp);
    }
  }

  NetState newState = NetState::LinkDown;
  if (linkStable) {
    if (ipValid) {
      g_ipMissingSince = 0;
      newState = NetState::Ready;
    } else {
      bool canHoldReady = g_stateInitialized && g_state == NetState::Ready && hasValidIp(g_lastReadyIp);
      if (canHoldReady) {
        if (g_ipMissingSince == 0) {
          g_ipMissingSince = now;
        }
        if ((long)(now - g_ipMissingSince) < (long)kIpLossGraceMs) {
          effectiveIp = g_lastReadyIp;
          newState = NetState::Ready;
        } else {
          newState = NetState::WaitingIp;
        }
      } else {
        newState = NetState::WaitingIp;
      }
    }
  } else {
    g_ipMissingSince = 0;
    effectiveIp = IPAddress(0, 0, 0, 0);
  }

  handleStateChange(newState, linkStable, effectiveIp);
}

bool networkIsConnected() {
  return g_connected;
}

bool networkHasLink() {
  return g_stateInitialized && g_state != NetState::LinkDown;
}

bool networkUsingDhcpRuntime() {
  return g_usingDhcp;
}

bool networkDhcpFallbackActive() {
  return g_dhcpFallbackActive;
}

IPAddress networkGetLocalIP() {
  if (g_connected && hasValidIp(g_lastReadyIp)) {
    return g_lastReadyIp;
  }
  return Ethernet.localIP();
}

void networkRequestReconfigure() {
  g_reconfigurePending = true;
  g_forceDisplayRefresh = true;
}
