// NetworkOSC.cpp

#include "NetworkOSC.h"
#include "Utils.h"
#include "FaderControl.h"
#include "Config.h"
#include "ExecutorStatus.h"
#include "KeyLedControl.h"
#include "NeoPixelControl.h"
#include "OLED.h"
#include "NetworkManager.h"
#include <AsyncUDP_Teensy41.h>
#include <string.h>


//================================
// GLOBAL NETWORK OBJECTS
//================================

AsyncUDP oscUdp;
static bool networkServicesStarted = false;
bool deskLocked = false;
bool wingCmdMode = false;
bool wingCmdExecMode = false;
bool wingCmdCopySrc = false;
bool wingCmdThru = false;

//================================
// OSC QUEUE (keeps UDP callback short)
//================================

static constexpr size_t OSC_MAX_PACKET_SIZE = 1536;     // Max bytes we will accept per packet (covers worst-case color/int bundles with margin)
static constexpr size_t OSC_QUEUE_DEPTH = 12;           // Number of packets buffered
static constexpr uint8_t OSC_MAX_PACKETS_PER_LOOP = 4;  // Process this many packets per loop iteration
static constexpr uint32_t OSC_PROCESS_BUDGET_US = 8000; // Stop processing if we exceed this budget in micro seconds

struct OscQueueItem {
  uint16_t len;
  uint32_t arrivalMs;
  uint8_t data[OSC_MAX_PACKET_SIZE];
};

static OscQueueItem oscQueue[OSC_QUEUE_DEPTH];
static volatile uint8_t oscQueueHead = 0;
static volatile uint8_t oscQueueTail = 0;
static volatile uint8_t oscQueueCount = 0;
static volatile uint32_t oscQueueDrops = 0;
static volatile uint32_t oscOversizeDrops = 0;

// Forward declarations for async callbacks
void handleBundledExecutorUpdate(LiteOSCParser& parser);
void handleColorUpdate(LiteOSCParser& parser);
void handleWingStatus(LiteOSCParser& parser);
void handleLedBrightness(LiteOSCParser& parser);
static void handleOscPacket(const uint8_t* data, size_t len);
static bool enqueueOscPacket(const uint8_t* data, size_t len);
static bool dequeueOscPacket(OscQueueItem& out);

//================================
// NETWORK SETUP
//================================

static bool enqueueOscPacket(const uint8_t* data, size_t len) {
  if (len > OSC_MAX_PACKET_SIZE) {
    oscOversizeDrops++;
    return false;
  }

  bool queued = false;
  noInterrupts();
  if (oscQueueCount < OSC_QUEUE_DEPTH) {
    OscQueueItem& slot = oscQueue[oscQueueHead];
    slot.len = static_cast<uint16_t>(len);
    slot.arrivalMs = millis();
    memcpy(slot.data, data, len);
    oscQueueHead = (oscQueueHead + 1) % OSC_QUEUE_DEPTH;
    oscQueueCount++;
    queued = true;
  } else {
    oscQueueDrops++;
  }
  interrupts();
  return queued;
}

static bool dequeueOscPacket(OscQueueItem& out) {
  bool hasPacket = false;
  noInterrupts();
  if (oscQueueCount > 0) {
    out = oscQueue[oscQueueTail];
    oscQueueTail = (oscQueueTail + 1) % OSC_QUEUE_DEPTH;
    oscQueueCount--;
    hasPacket = true;
  }
  interrupts();
  return hasPacket;
}

static void attachUdpHandler() {
  oscUdp.onPacket([](AsyncUDPPacket &packet) {
    const uint8_t* data = packet.data();
    const size_t len = packet.length();

    if (!enqueueOscPacket(data, len)) {
      static uint32_t lastDropPrint = 0;
      const uint32_t now = millis();
      if (now - lastDropPrint > 500) { // rate-limit debug spam
        if (len > OSC_MAX_PACKET_SIZE) {
          OSC_ERROR_PRINTF("Drop oversize packet %u bytes (max %u)", len, OSC_MAX_PACKET_SIZE);
        } else {
          OSC_ERROR_PRINTF("Queue full (%u/%u) dropping incoming packet", oscQueueCount, OSC_QUEUE_DEPTH);
        }
        lastDropPrint = now;
      }
    }
  });
}

void startNetworkServices() {
  if (networkServicesStarted) {
    return;
  }

  bool mdnsOk = MDNS.begin(kServiceName);
  if (!mdnsOk) {
    NETWORK_ERROR_PRINT("Failed to initialize mDNS");
  }
  MDNS.addService("_osc", "_udp", netConfig.oscPort);

  if (oscUdp.listen(netConfig.oscPort)) {
    attachUdpHandler();
    networkServicesStarted = true;
    NETWORK_DEBUG_PRINTF("OSC services started on port %u", netConfig.oscPort);
  } else {
    NETWORK_ERROR_PRINT("Failed to start AsyncUDP listener");
  }
}

void stopNetworkServices() {
  if (!networkServicesStarted) {
    return;
  }
  oscUdp.close();
  networkServicesStarted = false;
  NETWORK_DEBUG_PRINT("OSC services stopped");
}

void restartNetworkServices() {
  stopNetworkServices();
  delay(10);
  startNetworkServices();
}



//================================
//OSC MESSAGE HANDLING
//================================

static void handleOscPacket(const uint8_t* data, size_t len) {
  LiteOSCParser parser;

  if (!parser.parse(data, len)) {
    OSC_ERROR_PRINT("Invalid OSC message.");
    return;
  }

  const char* addr = parser.getAddress();

  if (strstr(addr, "/execUpdate") != NULL) {
    handleBundledExecutorUpdate(parser);
  } else if (strstr(addr, "/colorUpdate") != NULL) {
    handleColorUpdate(parser);
  } else if (strstr(addr, "/wingStatus") != NULL) {
    handleWingStatus(parser);
  } else if (strstr(addr, "/ledBrightness") != NULL) {
    handleLedBrightness(parser);
  } else if (strstr(addr, "/updatePage/current") != NULL) {
    if (parser.getTag(0) == 'i') {
      handlePageUpdate(addr, parser.getInt(0));
    }
  }
}

// Pull queued packets from the UDP callback and process a few each loop.
void processOscQueue() {
  uint8_t processed = 0;
  elapsedMicros budget;
  OscQueueItem pkt{};

  while (processed < OSC_MAX_PACKETS_PER_LOOP && dequeueOscPacket(pkt)) {
    handleOscPacket(pkt.data, pkt.len);
    processed++;

    if (budget >= OSC_PROCESS_BUDGET_US) {
      break;
    }
  }

  static uint32_t lastDropLog = 0;
  const uint32_t now = millis();
  if ((oscQueueDrops || oscOversizeDrops) && (now - lastDropLog > 1000)) {
    uint32_t drops = 0;
    uint32_t oversize = 0;
    uint8_t depth = 0;

    noInterrupts();
    drops = oscQueueDrops;
    oversize = oscOversizeDrops;
    depth = oscQueueCount;
    oscQueueDrops = 0;
    oscOversizeDrops = 0;
    interrupts();

    OSC_ERROR_PRINTF("queue drops=%lu oversize=%lu depth=%u", drops, oversize, depth);
    lastDropLog = now;
  }
}

// Page update message handling
void handlePageUpdate(const char *address, int value) {
  if (strstr(address, "/updatePage/current") != NULL) {
    if (value != currentOSCPage) {
      OSC_DEBUG_PRINTF("Page changed from %d to %d (via updatePage command)", currentOSCPage, value);
    }
    currentOSCPage = value;
  }
}


static bool parseSimpleColorString(const char* colorString, uint8_t& r, uint8_t& g, uint8_t& b) {
  if (!colorString) return false;

  char buffer[64];
  strncpy(buffer, colorString, sizeof(buffer) - 1);
  buffer[sizeof(buffer) - 1] = '\0';

  char* token = strtok(buffer, ",;");
  if (!token) return false;
  r = constrain(atoi(token), 0, 255);

  token = strtok(nullptr, ",;");
  if (!token) return false;
  g = constrain(atoi(token), 0, 255);

  token = strtok(nullptr, ",;");
  if (!token) return false;
  b = constrain(atoi(token), 0, 255);

  return true;
}

static void applyColorToExecutor(int oscId, const char* colorString) {
  uint8_t r, g, b;
  if (!parseSimpleColorString(colorString, r, g, b)) {
    return;
  }

  setExecutorColorByID(oscId, r, g, b);

  if (oscId >= 201 && oscId <= 210) {
    int faderIndex = getFaderIndexFromID(oscId);
    if (faderIndex >= 0 && faderIndex < NUM_FADERS) {
      faders[faderIndex].red = r;
      faders[faderIndex].green = g;
      faders[faderIndex].blue = b;
    }
  }
}


// Handle wing status update: two separate int args — arg0=deskLock (0|1), arg1=cmdFlags bitmask
void handleWingStatus(LiteOSCParser& parser) {
  if (parser.getArgCount() < 2 || parser.getTag(0) != 'i' || parser.getTag(1) != 'i') {
    OSC_ERROR_PRINT("Invalid wingStatus message (expected 2 int args: deskLock, cmdFlags)");
    return;
  }

  bool newDeskLocked = parser.getInt(0) != 0;
  int  flags         = parser.getInt(1);

  bool newCmdMode     = (flags & WING_STATUS_CMD_MODE) != 0;
  bool newCmdExecMode = (flags & WING_STATUS_CMD_EXEC_MODE) != 0;
  bool newCmdCopySrc  = (flags & WING_STATUS_CMD_COPY_SRC) != 0;
  bool newCmdThru     = (flags & WING_STATUS_CMD_THRU) != 0;
  OSC_DEBUG_PRINTF("Wing status: deskLock=%d cmdFlags=%d cmd=%d cmdExec=%d copySrc=%d thru=%d",
                   newDeskLocked ? 1 : 0, flags, newCmdMode ? 1 : 0, newCmdExecMode ? 1 : 0, newCmdCopySrc ? 1 : 0, newCmdThru ? 1 : 0);

  // Always update CMD mode flags — they change independently of deskLocked
  wingCmdMode     = newCmdMode;
  wingCmdExecMode = newCmdExecMode;
  wingCmdCopySrc  = newCmdCopySrc;
  wingCmdThru     = newCmdThru;

  if (newDeskLocked == deskLocked) {
    return;
  }

  deskLocked = newDeskLocked;

  if (!deskLocked) {
    resetFaderOwnership();
    display.markDirty();
    display.showIPAddress(networkGetLocalIP(), netConfig.oscPort, netConfig.sendToIP);
  } else {
    display.clear();
    display.setCursor(40, 16);
    display.setTextSize(TEXT_SIZE_MEDIUM);
    display.print("DESK");
    display.setCursor(28, 36);
    display.print("LOCKED");
    display.display();
  }
}

// Handle bundled executor updates: page + 10 fader setpoints + 40 executor statuses
void handleBundledExecutorUpdate(LiteOSCParser& parser) {
  const int expectedArgs = 1 + 10 + NUM_EXECUTORS_TRACKED;

  if (parser.getArgCount() < expectedArgs) {
    OSC_ERROR_PRINT("Invalid exec bundle - not enough arguments");
    return;
  }

  if (parser.getTag(0) != 'i') {
    OSC_ERROR_PRINT("Invalid exec bundle - page not integer");
    return;
  }

  int pageNum = parser.getInt(0);
  currentOSCPage = pageNum;

  bool stateChanged = false;
  bool needToMoveFaders = false;
  bool blockFaderUpdates = calibrationInProgress;
  int changedFaders = 0;
  int changedExecs = 0;

  // Fader values (201-210) occupy args 1-10
  for (int i = 0; i < 10; i++) {
    int argIndex = i + 1;
    int faderOscID = 201 + i;

    if (parser.getTag(argIndex) != 'i') {
      OSC_ERROR_PRINTF("Invalid fader value type for fader %d", faderOscID);
      continue;
    }

    int oscValue = parser.getInt(argIndex);
    int faderIndex = getFaderIndexFromID(faderOscID);

    if (blockFaderUpdates) {
      continue;
    }

    if (faderIndex >= 0 && faderIndex < NUM_FADERS) {
      bool localOwnershipActive = faders[faderIndex].touched;
      if (Fconfig.allowFaderOscWithoutTouch) {
        localOwnershipActive = localOwnershipActive || faders[faderIndex].manualOverride;
      }

      if (!localOwnershipActive) {
        int currentOscvalue = readFadertoOSC(faders[faderIndex]);
        if (abs(oscValue - currentOscvalue) > Fconfig.targetTolerance) {
          setFaderSetpoint(faderIndex, oscValue);
          needToMoveFaders = true;
          changedFaders++;
        }
      }
    } else {
      OSC_ERROR_PRINTF("Fader index not found for OSC ID %d", faderOscID);
    }
  }

  // Executor statuses (101-410) start after the fader block
  for (int i = 0; i < NUM_EXECUTORS_TRACKED; ++i) {
    int argIndex = 11 + i;
    if (parser.getTag(argIndex) != 'i') {
      OSC_ERROR_PRINTF("Invalid exec status type for executor %d", EXECUTOR_IDS[i]);
      continue;
    }

    int raw = parser.getInt(argIndex);
    uint8_t status = raw < 0 ? 0 : (raw > 2 ? 2 : raw); // 0=empty,1=off,2=on
    if (setExecutorStateByIndex(i, status)) {
      stateChanged = true;
      changedExecs++;
    }
  }

  if (stateChanged) {
    markKeyLedsDirty();
  }

  if (changedFaders > 0 || changedExecs > 0) {
    OSC_DEBUG_PRINTF("Exec update received: page=%d faders=%d execs=%d",
                     pageNum, changedFaders, changedExecs);
  }

  if (needToMoveFaders) {
    moveAllFadersToSetpoints();
  }
}

// Handle bundled color updates: page + 40 color strings (execs 101-410)
void handleColorUpdate(LiteOSCParser& parser) {
  const int expectedArgs = 1 + NUM_EXECUTORS_TRACKED;

  if (parser.getArgCount() < expectedArgs) {
    OSC_ERROR_PRINT("Invalid color bundle - not enough arguments");
    return;
  }

  if (parser.getTag(0) != 'i') {
    OSC_ERROR_PRINT("Invalid color bundle - page not integer");
    return;
  }

  int pageNum = parser.getInt(0);
  currentOSCPage = pageNum;
  int appliedColors = 0;

  for (int i = 0; i < NUM_EXECUTORS_TRACKED; ++i) {
    int argIndex = i + 1;
    if (parser.getTag(argIndex) != 's') {
      OSC_ERROR_PRINTF("Invalid color type for executor %d", EXECUTOR_IDS[i]);
      continue;
    }
    applyColorToExecutor(EXECUTOR_IDS[i], parser.getString(argIndex));
    appliedColors++;
  }

  // Left disabled for now because grandMA can send color bundles continuously
  // during normal fader movement, which floods the serial monitor.
  // if (appliedColors > 0) {
  //   OSC_DEBUG_PRINTF("Color update received: page=%d colors=%d", pageNum, appliedColors);
  // }
}

// Handle LED brightness update from MA3 DeskLightsCollect.
// Format: /ledBrightness,iiii,<faderBg>,<faderFb>,<execBg>,<execFb>
// All values 0-255.
//   faderBg/Fb  → FaderConfig (idle / touched)
//   execBg      → execMaBrightness (used for status=1 when useMA3OccupiedBrightness is on)
//   execFb      → ExecConfig.activeBrightness (status=2)
void handleLedBrightness(LiteOSCParser& parser) {
  if (parser.getArgCount() < 4) {
    OSC_ERROR_PRINT("Invalid ledBrightness message (expected 4 int args)");
    return;
  }
  for (int i = 0; i < 4; i++) {
    if (parser.getTag(i) != 'i') {
      OSC_ERROR_PRINTF("Invalid ledBrightness arg type at index %d", i);
      return;
    }
  }

  uint8_t faderBg = (uint8_t)constrain(parser.getInt(0), 0, 255);
  uint8_t faderFb = (uint8_t)constrain(parser.getInt(1), 0, 255);
  uint8_t execBg  = (uint8_t)constrain(parser.getInt(2), 0, 255);
  uint8_t execFb  = (uint8_t)constrain(parser.getInt(3), 0, 255);

  Fconfig.baseBrightness      = faderBg;
  Fconfig.touchedBrightness   = faderFb;
  execMaBrightness            = execBg;   // cached; used for status=1 when toggle is on
  execConfig.activeBrightness = execFb;   // status=2 (active) from MA3 LEDFeedback
  // execConfig.baseBrightness (status=1 fallback) stays at EEPROM/web-UI value

  updateBaseBrightnessPixels();  // push new baseBrightness to all currently untouched faders
  invalidateNeoPixelRenderCache();
  markKeyLedsDirty();

  OSC_DEBUG_PRINTF("LED brightness: faderBg=%d faderFb=%d execBg=%d execFb=%d",
                   faderBg, faderFb, execBg, execFb);
}



// Put together and send an OSC message

void sendOscMessage(const char* address, const char* typeTag, const void* value) {
  uint8_t buffer[128];
  int len = 0;

  // Write address
  int addrLen = strlen(address);
  memcpy(buffer + len, address, addrLen);
  len += addrLen;
  buffer[len++] = '\0';
  while (len % 4 != 0) buffer[len++] = '\0';

  // Write type tag
  int tagLen = strlen(typeTag);
  memcpy(buffer + len, typeTag, tagLen);
  len += tagLen;
  buffer[len++] = '\0';
  while (len % 4 != 0) buffer[len++] = '\0';

  // Add argument(s)
  if (strcmp(typeTag, ",i") == 0) {
    int v = *(int*)value;
    uint32_t netOrder = htonl(v);
    memcpy(buffer + len, &netOrder, 4);
    len += 4;
  } else if (strcmp(typeTag, ",s") == 0) {
    const char* str = (const char*)value;
    int strLen = strlen(str);
    memcpy(buffer + len, str, strLen);
    len += strLen;
    buffer[len++] = '\0';
    while (len % 4 != 0) buffer[len++] = '\0';
  } else {
    OSC_ERROR_PRINT("Unsupported OSC typeTag.");
    return;
  }

  oscUdp.writeTo(buffer, len, netConfig.sendToIP, netConfig.oscPort);
}




//================================
// OSC HELPER FUNCTIONS
//================================


// Checks if the buffer starts as a valid bundle
bool isBundleStart(const uint8_t *buf, size_t len) {
  if (len < 16 || (len & 0x03) != 0) {
    return false;
  }
  if (strncmp((const char*)buf, "#bundle", 8) != 0) {
    return false;
  }
  return true;
}


//================================
// OSC DEBUG FUNCTION
//================================

// Print an OSC message for debugging
void printOSC(Print &out, const uint8_t *b, int len) {
  LiteOSCParser osc;

  // Check if it's a bundle
  if (isBundleStart(b, len)) {
    out.println("#bundle (not parsed)");
    return;
  }

  // Parse the message
  if (!osc.parse(b, len)) {
    if (osc.isMemoryError()) {
      out.println("#MemoryError");
    } else {
      out.println("#ParseError");
    }
    return;
  }

  // Print address
  out.printf("%s", osc.getAddress());

  // Print arguments
  int size = osc.getArgCount();
  for (int i = 0; i < size; i++) {
    if (i == 0) {
      out.print(": ");
    } else {
      out.print(", ");
    }
    
    // Print based on type
    switch (osc.getTag(i)) {
      case 'i':
        out.printf("int(%d)", osc.getInt(i));
        break;
      case 'f':
        out.printf("float(%f)", osc.getFloat(i));
        break;
      case 's':
        out.printf("string(\"%s\")", osc.getString(i));
        break;
      case 'T':
        out.print("true");
        break;
      case 'F':
        out.print("false");
        break;
      default:
        out.printf("unknown(%c)", osc.getTag(i));
    }
  }
  out.println();
}
