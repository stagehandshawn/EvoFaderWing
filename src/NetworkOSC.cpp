// NetworkOSC.cpp

#include "NetworkOSC.h"
#include "Utils.h"
#include "FaderControl.h"



//================================
// GLOBAL NETWORK OBJECTS
//================================

// Add to TOP of src/NetworkOSC.cpp:
EthernetUDP udp;


//================================
// NETWORK SETUP
//================================

void setupNetwork() {
  debugPrint("Setting up network...");
  
  delay(100); //make sure we see network debug message before looking for dhcp so we know we are booting up

  // Start Ethernet with configured settings
  if (netConfig.useDHCP) {
    debugPrint("Using DHCP...");
    if (!Ethernet.begin() || !Ethernet.waitForLocalIP(kDHCPTimeout)) {
      debugPrint("Failed DHCP, switching to static IP");
      Ethernet.begin(netConfig.staticIP, netConfig.subnet, netConfig.gateway);
    }
  } else {
    debugPrint("Using static IP...");
    Ethernet.begin(netConfig.staticIP, netConfig.subnet, netConfig.gateway);
  }

  IPAddress ip = Ethernet.localIP();
  debugPrintf("IP Address: %u.%u.%u.%u\n", ip[0], ip[1], ip[2], ip[3]);

  // Start UDP for OSC
  udp.begin(netConfig.receivePort);
  
  // Set up mDNS for service discovery
  MDNS.begin(kServiceName);
  MDNS.addService("_osc", "_udp", netConfig.receivePort);
  debugPrint("OSC and mDNS initialized");
}

//================================
// OSC MESSAGE HANDLING
//================================

// // Consolidated OSC message handler
// void handleOscMessage() {
//   int size = udp.parsePacket();
//   if (size <= 0) return;

//   const uint8_t *data = udp.data();
//   LiteOSCParser parser;

//   if (!parser.parse(data, size)) {
//     debugPrint("Invalid OSC message.");
//     return;
//   }

//   const char* addr = parser.getAddress();

//   // Handle page update messages
//   if (strstr(addr, "/updatePage/current") != NULL) {
//     if (parser.getTag(0) == 'i') {
//       handlePageUpdate(addr, parser.getInt(0));
//     }
//   }
//   // Handle color messages
//   else if (strstr(addr, "/Color") != NULL) {
//     if (parser.getTag(0) == 's') {
//       handleColorOsc(addr, parser.getString(0));
//     }
//   }
//   // Handle fader movement messages
//   else if (strstr(addr, "/Page") != NULL && strstr(addr, "/Fader") != NULL) {
//     if (parser.getTag(0) == 'i') {
//       int value = parser.getInt(0);
      
//       handleOscMovement(addr, value);

//     }
//   }
// }

// Returns the index of the fader with the given OSC ID, or -1 if not found
int getFaderIndexFromID(int id) {
  for (int i = 0; i < NUM_FADERS; i++) {
    if (faders[i].oscID == id) {
      return i;
    }
  }
  return -1;
}

// Handles fader movement OSC messages
void handleOscMovement(const char *address, int value) {
  int pageNum = -1;
  int faderID = -1;

  // Extract both page number and fader ID from the OSC address
  if (sscanf(address, "/Page%d/Fader%d", &pageNum, &faderID) == 2) {
    
    // If we received a message for a different page, update current page
    if (pageNum != currentOSCPage) {
      debugPrintf("Page changed from %d to %d (via fader message)\n", currentOSCPage, pageNum);
    }
    currentOSCPage = pageNum;
    
    int faderIndex = getFaderIndexFromID(faderID);  

    if (faders[faderIndex].touched) return;   //if touched then don't update using osc or we will get feedback

            // When you receive an OSC message:
            debugPrintf("Fader %d new setpoint %d (via fader message)\n", faderID, value);
      setFaderSetpoint(faderIndex, value); // oscValue is 0-100
      moveAllFadersToSetpoints(); // This will move all faders as needed
  }
}


// New function to handle page update messages
void handlePageUpdate(const char *address, int value) {
  if (strstr(address, "/updatePage/current") != NULL) {
    if (value != currentOSCPage) {
      debugPrintf("Page changed from %d to %d (via updatePage command)\n", currentOSCPage, value);
    }
    currentOSCPage = value;
  }
}



void sendOscUpdate(Fader& f, int value, bool force) {
  unsigned long now = millis();
//Do not send osc of fader is not touched
  // if (!f.touched){
  //   return;
  // }

  // Only send if value changed significantly or enough time passed or force flag is set
  if (force || (abs(value - f.lastSentOscValue) >= OSC_VALUE_THRESHOLD && 
      now - f.lastOscSendTime > OSC_RATE_LIMIT)) {
    
    char oscAddress[32];
    snprintf(oscAddress, sizeof(oscAddress), "/Page%d/Fader%d", currentOSCPage, f.oscID);
    

    debugPrintf("Sending OSC update for Fader %d on Page %d → value: %d\n", f.oscID, currentOSCPage, value);
    
    // Create OSC message - This is a simplified placeholder
    // OSC message format: [address]\0,[tags]\0,data...
    
    uint8_t buffer[64];
    int size = 0;
    
    // Copy address with null terminator
    int addrLen = strlen(oscAddress);
    memcpy(buffer + size, oscAddress, addrLen + 1);
    size += addrLen + 1;
    
    // Pad to 4-byte boundary
    while ((size & 3) != 0) buffer[size++] = 0;
    
    // Add type tag - for integer value
    buffer[size++] = ',';
    buffer[size++] = 'i'; // Integer type
    buffer[size++] = 0;   // Null terminator
    buffer[size++] = 0;   // Padding to 4-byte boundary
    
    // Add value (big-endian 32-bit integer)
    buffer[size++] = (value >> 24) & 0xFF;
    buffer[size++] = (value >> 16) & 0xFF;
    buffer[size++] = (value >> 8) & 0xFF;
    buffer[size++] = value & 0xFF;
    
    // Send OSC packet
    udp.beginPacket(netConfig.sendToIP, netConfig.sendPort);
    udp.write(buffer, size);
    udp.endPacket();
    
    f.lastOscSendTime = now;
    f.lastSentOscValue = value;
  }
}

void handleColorOsc(const char *address, const char *colorString) {
  // Extract the fader ID from the address
  int faderID = 0;
  
  // Search for "Color" in the address string
  const char* colorPos = strstr(address, "Color");
  if (colorPos) {
    // Extract fader ID after "Color"
    faderID = atoi(colorPos + 5); // +5 to skip past "Color"
    
    // Find the fader with matching OSC ID
    for (int i = 0; i < NUM_FADERS; i++) {
      if (faders[i].oscID == faderID) {
        parseColorValues(colorString, faders[i]);
        debugPrintf("Color update for Fader %d: R=%d, G=%d, B=%d\n", 
                   i, faders[i].red, faders[i].green, faders[i].blue);
        break;
      }
    }
  }
}

//================================
// OSC UTILITY FUNCTIONS
//================================

// Parse color values from a string like "255;157;0;255"
void parseColorValues(const char *colorString, Fader& f) {
  char buffer[64];
  strncpy(buffer, colorString, 63);
  buffer[63] = '\0'; // Ensure null-termination
  
  // Parse red component
  char *ptr = strtok(buffer, ";");
  if (ptr != NULL) {
    f.red = constrain(atoi(ptr), 0, 255);
    
    // Parse green component
    ptr = strtok(NULL, ";");
    if (ptr != NULL) {
      f.green = constrain(atoi(ptr), 0, 255);
      
      // Parse blue component
      ptr = strtok(NULL, ";");
      if (ptr != NULL) {
        f.blue = constrain(atoi(ptr), 0, 255);
        
        // Alpha value is in the fourth position, but we ignore it
      }
    }
  }
  
  f.colorUpdated = true;
}

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
    debugPrint("Unsupported OSC typeTag.");
    return;
  }

  udp.beginPacket(netConfig.sendToIP, netConfig.sendPort);
  udp.write(buffer, len);
  udp.endPacket();
}






// Add this function to your NetworkOSC.cpp file

// Handle bundled fader update messages
void handleBundledFaderUpdate(LiteOSCParser& parser) {
  // Expected format: /faderUpdate,iiiiiiiiiiissssssssss,PAGE,F201,F202...F210,C201,C202...C210
  
  if (parser.getArgCount() < 21) {
    debugPrint("Invalid bundled fader message - not enough arguments");
    return;
  }
  
  // Parse page number (argument 0)
  if (parser.getTag(0) != 'i') {
    debugPrint("Invalid bundled fader message - page not integer");
    return;
  }
  
  int pageNum = parser.getInt(0);
  
  // Update current page if it changed
  if (pageNum != currentOSCPage) {
    debugPrintf("Page changed from %d to %d (via bundled message)\n", currentOSCPage, pageNum);
    currentOSCPage = pageNum;
  }
  
  // Track if any setpoints need updating
  bool needToMoveFaders = false;
  
  // Parse fader values (arguments 1-10 for faders 201-210)
  for (int i = 0; i < 10; i++) {
    int argIndex = i + 1; // Arguments 1-10
    int faderOscID = 201 + i; // Fader IDs 201-210
    
    if (parser.getTag(argIndex) != 'i') {
      debugPrintf("Invalid fader value type for fader %d\n", faderOscID);
      continue;
    }
    
    int oscValue = parser.getInt(argIndex);
    int faderIndex = getFaderIndexFromID(faderOscID);
    
    if (faderIndex >= 0 && faderIndex < NUM_FADERS) {
      // Only update if fader is not currently being touched (avoid feedback)
      if (!faders[faderIndex].touched) {
        // Check if the value actually changed before updating
        //int currentSetpoint = faders[faderIndex].setpoint;
        int currentOscvalue = readFadertoOSC(faders[faderIndex]);
        
        // Convert OSC value (0-100) to fader range if needed
        // (You may need to adjust this based on how your setFaderSetpoint works)
        if (abs(oscValue - currentOscvalue) > Fconfig.targetTolerance) {
          debugPrintf("Updating fader %d setpoint: %d -> %d\n", faderOscID, currentOscvalue, oscValue);
          setFaderSetpoint(faderIndex, oscValue);
          needToMoveFaders = true;
        }
      }
    } else {
      debugPrintf("Fader index not found for OSC ID %d\n", faderOscID);
    }
  }
  
  // Move all faders to their new setpoints if any changed
  if (needToMoveFaders) {
    debugPrint("Moving faders to new setpoints");
    moveAllFadersToSetpoints();
  }
  
  // Parse color values (arguments 11-20 for faders 201-210)
  for (int i = 0; i < 10; i++) {
    int argIndex = i + 11; // Arguments 11-20
    int faderOscID = 201 + i; // Fader IDs 201-210
    
    if (parser.getTag(argIndex) != 's') {
      debugPrintf("Invalid color value type for fader %d\n", faderOscID);
      continue;
    }
    
    const char* colorString = parser.getString(argIndex);
    int faderIndex = getFaderIndexFromID(faderOscID);
    
    if (faderIndex >= 0 && faderIndex < NUM_FADERS && !faders[faderIndex].touched) {
      // Parse and update color values
      parseColorValues(colorString, faders[faderIndex]);
      //debugPrintf("Updated color for fader %d: %s\n", faderOscID, colorString);
    } else {
      debugPrintf("Fader index not found for color update, OSC ID %d\n", faderOscID);
    }
  }
  
  debugPrint("Bundled fader update complete");
}

// Modified handleOscMessage function - replace your existing one  WE WILL BE FIXING AND OR REPLACEING THIS NOW WE Use A BUNDLE
void handleOscMessage() {
  int size = udp.parsePacket();
  if (size <= 0) return;

  const uint8_t *data = udp.data();
  LiteOSCParser parser;

  if (!parser.parse(data, size)) {
    debugPrint("Invalid OSC message.");
    return;
  }

  const char* addr = parser.getAddress();

  // Handle bundled fader update messages (NEW)
  if (strstr(addr, "/faderUpdate") != NULL) {
    handleBundledFaderUpdate(parser);
  }
  // Handle page update messages (EXISTING)
  else if (strstr(addr, "/updatePage/current") != NULL) {
    if (parser.getTag(0) == 'i') {
      handlePageUpdate(addr, parser.getInt(0));
    }
  }
  // Handle color messages (EXISTING)
  else if (strstr(addr, "/Color") != NULL) {
    if (parser.getTag(0) == 's') {
      handleColorOsc(addr, parser.getString(0));
    }
  }
  // Handle individual fader movement messages (EXISTING)
  else if (strstr(addr, "/Page") != NULL && strstr(addr, "/Fader") != NULL) {
    if (parser.getTag(0) == 'i') {
      int value = parser.getInt(0);
      handleOscMovement(addr, value);
    }
  }
}




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