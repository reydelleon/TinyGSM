/**
 * @file       TinyGsmClientMC20.h
 * @author     Reydel Leon Machado
 * @license    LGPL-3.0
 * @copyright  Copyright (c) 2018 Reydel Leon Machado
 * @date       Nov 2018
 */

#ifndef TinyGsmClientMC20_h
#define TinyGsmClientMC20_h

// #define TINY_GSM_DEBUG Serial
//#define TINY_GSM_USE_HEX

#if !defined(TINY_GSM_RX_BUFFER)
  #define TINY_GSM_RX_BUFFER 64
#endif

#define TINY_GSM_MUX_COUNT 6

#include <TinyGsmCommon.h>

#define GSM_NL "\r\n"
static const char GSM_OK[] TINY_GSM_PROGMEM = "OK" GSM_NL;
static const char GSM_ERROR[] TINY_GSM_PROGMEM = "ERROR" GSM_NL;

enum SimStatus {
  SIM_ERROR = 0,
  SIM_READY = 1,
  SIM_LOCKED = 2,
};

enum RegStatus {
  REG_UNREGISTERED = 0,
  REG_OK_HOME      = 1,
  REG_SEARCHING    = 2,
  REG_DENIED       = 3,
  REG_UNKNOWN      = 4,
  REG_OK_ROAMING   = 5,
};

//============================================================================//
//============================================================================//
//                    Declaration of the TinyGsmMC20 Class
//============================================================================//
//============================================================================//

class TinyGsmMC20
{

//============================================================================//
//============================================================================//
//                         The Internal MC20 Client Class
//============================================================================//
//============================================================================//

public:

class GsmClient : public Client
{
  friend class TinyGsmMC20;
  typedef TinyGsmFifo<uint8_t, TINY_GSM_RX_BUFFER> RxFifo;

public:
  GsmClient() {}

  GsmClient(TinyGsmMC20& modem, uint8_t mux = 0) {
    init(&modem, mux);
  }

  bool init(TinyGsmMC20* modem, uint8_t mux = 0) {
    this->at = modem;
    this->mux = mux;
    sock_available = 0;
    sock_connected = false;
    got_data = false;

    at->sockets[mux] = this;

    return true;
  }

public:
  virtual int connect(const char *host, uint16_t port) {
    // stop();
    TINY_GSM_YIELD();
    rx.clear();
    sock_connected = at->modemConnect(host, port, mux);

    return sock_connected;
  }

  virtual int connect(IPAddress ip, uint16_t port) {
    String host; host.reserve(16);
    host += ip[0];
    host += ".";
    host += ip[1];
    host += ".";
    host += ip[2];
    host += ".";
    host += ip[3];
    return connect(host.c_str(), port);
  }

  virtual void stop() {
    TINY_GSM_YIELD();
    at->sendAT(GF("+QICLOSE="), mux);
    sock_connected = false;
    at->waitResponse(GF(", CLOSE OK"));
    rx.clear();
  }

  virtual size_t write(const uint8_t *buf, size_t size) {
    TINY_GSM_YIELD();
    at->maintain();
    return at->modemSend(buf, size, mux);
  }

  virtual size_t write(uint8_t c) {
    return write(&c, 1);
  }

  virtual size_t write(const char *str) {
    if (str == NULL) return 0;
    return write((const uint8_t *)str, strlen(str));
  }

  virtual int available() {
    TINY_GSM_YIELD();
    if (!rx.size()) {
      at->maintain();
    }
    return rx.size() + sock_available;
  }

  virtual int read(uint8_t *buf, size_t size) {
    TINY_GSM_YIELD();
    at->maintain();
    size_t cnt = 0;  
    while (cnt < size) {
      size_t chunk = TinyGsmMin(size-cnt, rx.size());
      if (chunk > 0) {
        rx.get(buf, chunk);
        buf += chunk;
        cnt += chunk;
        continue;
      }
      // TODO: Read directly into user buffer?
      at->maintain();
      if (sock_available > 0) {
        sock_available -= at->modemRead(TinyGsmMin((uint16_t)rx.free(), sock_available), mux);
      } else {
        break;
      }
    }
    return cnt;
  }

  virtual int read() {
    uint8_t c;
    if (read(&c, 1) == 1) {
      return c;
    }
    return -1;
  }

  virtual int peek() { return -1; } //TODO
  virtual void flush() { at->stream.flush(); }

  virtual uint8_t connected() {
    if (available()) {
      return true;
    }
    
    return sock_connected;
  }
  virtual operator bool() { return connected(); }

  /*
   * Extended API
   */

  String remoteIP() TINY_GSM_ATTR_NOT_IMPLEMENTED;

private:
  TinyGsmMC20*  at;
  uint8_t       mux;
  uint16_t      sock_available;
  bool          sock_connected;
  bool          got_data;
  RxFifo        rx;
};

//============================================================================//
//============================================================================//
//                          The MC20 Secure Client
//============================================================================//
//============================================================================//


class GsmClientSecure : public GsmClient
{
public:
  GsmClientSecure() {}

  GsmClientSecure(TinyGsmMC20& modem, uint8_t mux = 0)
    : GsmClient(modem, mux)
  {}

public:
  virtual int connect(const char *host, uint16_t port) {
    stop();
    TINY_GSM_YIELD();
    rx.clear();
    sock_connected = at->modemConnect(host, port, mux, true);
    return sock_connected;
  }

  virtual void stop() {
    TINY_GSM_YIELD();
    at->sendAT(GF("+QSSLCLOSE="), mux);
    sock_connected = false;
    at->waitResponse(GF("CLOSE OK"));
    rx.clear();
  }

  virtual size_t write(const uint8_t *buf, size_t size) {
    TINY_GSM_YIELD();
    at->maintain();
    return at->modemSend(buf, size, mux, true);
  }

  virtual int read(uint8_t *buf, size_t size) {
    TINY_GSM_YIELD();
    size_t cnt = 0;
    while (cnt < size) {
      size_t chunk = TinyGsmMin(size-cnt, rx.size());
      if (chunk > 0) {
        rx.get(buf, chunk);
        buf += chunk;
        cnt += chunk;
        continue;
      }

      if (!rx.size() && sock_connected) {
        at->maintain();
        //break;
      }
    }
    return cnt;
  }

  virtual int read() {
    uint8_t c;
    if (read(&c, 1) == 1) {
      return c;
    }
    return -1;
  }

  virtual int available() {
    TINY_GSM_YIELD();
    if (!rx.size() && sock_connected) {
      at->maintain();
    }
    return rx.size();
  }
};

//============================================================================//
//============================================================================//
//                          The MC20 Modem Functions
//============================================================================//
//============================================================================//

public:

#ifdef GSM_DEFAULT_STREAM
  TinyGsmMC20(Stream& stream = GSM_DEFAULT_STREAM)
#else
  TinyGsmMC20(Stream& stream)
#endif
    : stream(stream)
  {
    memset(sockets, 0, sizeof(sockets));
  }

  /*
   * Basic functions
   */
  bool begin(unsigned long baudRate = 0) {
    return init(baudRate);
  }

  bool init(unsigned long baudRate = 0) {
    if (!testAT()) return false;

    // sendAT(GF("&FZE0"));  // Factory Reset + Set to user defined params + Echo Off
    // if (waitResponse() != 1) return false;

    sendAT(GF("+IPR="), baudRate, GF("&W"));
    waitResponse();

    // Select foreground context (MC20 provides 2 of them)
    sendAT(GF("+QIFGCNT=0"));
    if (waitResponse() != 1) return false;

    sendAT(GF("+QIMUX=1"));
    if (waitResponse() != 1) return false;

    // Use domain names instead of IPs for connection
    sendAT(GF("+QIDNSIP=1"));
    if (waitResponse() != 1) return false;

    sendAT(GF("+QINDI=1"));

    return true;
  }

  void setBaud(unsigned long baud) {
    sendAT(GF("+IPR="), baud);
  }

  bool testAT(unsigned long timeout = 10000L) {
    for (unsigned long start = millis(); millis() - start < timeout; ) {
      sendAT(GF(""));
      if (waitResponse(200) == 1) {
        delay(100);
        return true;
      }
      delay(100);
    }
    return false;
  }

  void maintain(bool ssl = false) {
    if (!ssl) {
       for (int mux = 0; mux < TINY_GSM_MUX_COUNT; mux++) {
        GsmClient* sock = sockets[mux];
        if (sock && sock->got_data) {
          sock->got_data = false;
          sock->sock_available = modemGetAvailable(mux, ssl);
        }
      }
    }

    while (stream.available()) {
      waitResponse(10, NULL, NULL);
    }
  }

  bool factoryDefault() {
    sendAT(GF("&FZE0&W"));  // Factory + Reset + Echo Off + Write
    waitResponse();
    sendAT(GF("+IPR=0"));   // Auto-baud
    waitResponse();
    sendAT(GF("&W"));       // Write configuration to user profile
    return waitResponse() == 1;
  }

  String getModemInfo() {
    sendAT(GF("I"));
    String res;
    if (waitResponse(1000L, res) != 1) {
      return "";
    }
    res.replace(GSM_NL "OK" GSM_NL, "");
    res.replace(GSM_NL, " ");
    res.trim();
    return res;
  }

  bool hasSSL() {
    return true; // TODO Use an AT command to verify?
  }

  /*
   * Power functions
   */

  bool restart() {
    if (!testAT()) {
      TINY_GSM_DEBUG.println("Modem seems to be off. Turn on and try again.");
      return false;
    }
    sendAT(GF("+CFUN=1,1"));
    if (waitResponse(60000L) != 1) {
      return false;
    }
    delay(3000);
    return init();
  }

  bool poweroff(bool emergency = true) {
    int rsp;
    int mode = emergency ? 0 : 1;
    sendAT(GF("+QPOWD="), mode);
    rsp =  waitResponse(GF("OK"), GF("NORMAL POWER DOWN"));
    return 1 <= rsp <= 2;
  }

  bool radioOff() {
    sendAT(GF("+CFUN=0"));
    if (waitResponse(10000L) != 1) {
      return false;
    }
    delay(3000);
    return true;
  }

  /*
   * SIM card functions
   */

  bool simUnlock(const char *pin) {
    sendAT(GF("+CPIN="), pin);
    return waitResponse() == 1;
  }

  String getSimCCID() {
    sendAT(GF("+QCCID?"));
    if (waitResponse(GF(GSM_NL)) != 1) {
      return "";
    }
    String res = stream.readStringUntil('\n');
    waitResponse();
    res.trim();
    return res;
  }

  String getIMEI() {
    sendAT(GF("+GSN"));
    if (waitResponse(GF(GSM_NL)) != 1) {
      return "";
    }
    String res = stream.readStringUntil('\n');
    waitResponse();
    res.trim();
    return res;
  }

  SimStatus getSimStatus(unsigned long timeout = 10000L) {
    for (unsigned long start = millis(); millis() - start < timeout; ) {
      sendAT(GF("+CPIN?"));
      if (waitResponse(GF(GSM_NL "+CPIN:")) != 1) {
        delay(1000);
        continue;
      }
      int status = waitResponse(GF("READY"), GF("SIM PIN"), GF("SIM PUK"));
      waitResponse();
      switch (status) {
        case 2:
        case 3:  return SIM_LOCKED;
        case 1:  return SIM_READY;
        default: return SIM_ERROR;
      }
    }
    return SIM_ERROR;
  }

  RegStatus getRegistrationStatus() {
    sendAT(GF("+CREG?"));
    if (waitResponse(GF(GSM_NL "+CREG:")) != 1) {
      return REG_UNKNOWN;
    }
    streamSkipUntil(','); // Skip format (0)
    int status = stream.readStringUntil('\n').toInt();
    waitResponse();
    return (RegStatus)status;
  }

  String getOperator() {
    sendAT(GF("+COPS?"));
    if (waitResponse(GF(GSM_NL "+COPS:")) != 1) {
      return "";
    }
    streamSkipUntil('"'); // Skip mode and format
    String res = stream.readStringUntil('"');
    waitResponse();
    return res;
  }

  /*
   * Generic network functions
   */

  int getSignalQuality() {
    sendAT(GF("+CSQ"));
    if (waitResponse(GF(GSM_NL "+CSQ:")) != 1) {
      return 99;
    }
    int res = stream.readStringUntil(',').toInt();
    waitResponse();
    return res;
  }

  bool isNetworkConnected() {
    RegStatus gsmStatus = getRegistrationStatus();

    return (gsmStatus == REG_OK_HOME || gsmStatus == REG_OK_ROAMING);
  }

  bool waitForNetwork(unsigned long timeout = 115000L) {
    for (unsigned long start = millis(); millis() - start < timeout; ) {
      if (isNetworkConnected()) {
        return true;
      }
      delay(250);
    }
    return false;
  }

  bool waitForGpsTimeSync(unsigned long timeout = 120000L) {
    for (unsigned long start = millis(); millis() - start < timeout; ) {
      if (gpsIsTimeSynched()) return true;
      delay(250);
    }
    return false;
  }

  /*
   * WiFi functions
   */

  /*
   * GPRS functions
   */

  bool gprsConnect(const char* apn, const char* user = NULL, const char* pwd = NULL) {
    // Select GPRS as the bearer service for the connections
    sendAT(GF("+QICSGP=1,"), "\"", apn, GF("\",\""), user, GF("\",\""), pwd, "\"");
    if (waitResponse() != 1) return false;

    if (getSimStatus() != 1) return false;

    if (!waitForNetwork()) return false;
    
    // Activate PDP context (Next 3 steps; must be executed in order and together)
    sendAT(GF("+QIREGAPP"));
    if (waitResponse() != 1) return false;
    
    sendAT(GF("+QIACT"));
    if (waitResponse(150000L) != 1) {
      return false;
    }

    sendAT(GF("+QILOCIP"));
    waitResponse(GF(GSM_NL));
    streamSkipUntil('\n');

    return true;
  }

  bool gprsDisconnect() {
    // Deactivate PDP context
    sendAT(GF("+QIDEACT"));
    if (waitResponse(40000L, GF(GSM_NL "DEACT OK")) != 1)
      return false;

    return true;
  }

  bool isGprsConnected() {
    sendAT(GF("+CGATT?"));
    if (waitResponse(GF(GSM_NL "+CGATT:")) != 1) {
      return false;
    }
    int res = stream.readStringUntil('\n').toInt();
    waitResponse();
    if (res != 1)
      return false;

    return localIP() != 0;
  }

  String getLocalIP() {
    sendAT(GF("+CGPADDR=1"));
    if (waitResponse(10000L, GF(GSM_NL "+CGPADDR:")) != 1) {
      return "";
    }
    streamSkipUntil(',');
    String res = stream.readStringUntil('\n');
    if (waitResponse() != 1) {
      return "";
    }
    return res;
  }

  IPAddress localIP() {
    return TinyGsmIpFromString(getLocalIP());
  }

  /*
   * Phone Call functions
   */

  bool setGsmBusy(bool busy = true) TINY_GSM_ATTR_NOT_AVAILABLE;

  bool callAnswer() {
    sendAT(GF("A"));
    return waitResponse() == 1;
  }

  // Returns true on pick-up, false on error/busy
  bool callNumber(const String& number) TINY_GSM_ATTR_NOT_IMPLEMENTED;

  bool callHangup() {
    sendAT(GF("H"));
    return waitResponse() == 1;
  }

  // 0-9,*,#,A,B,C,D
  bool dtmfSend(char cmd, int duration_ms = 100) { // TODO: check
    duration_ms = constrain(duration_ms, 100, 1000);

    sendAT(GF("+VTD="), duration_ms / 100); // VTD accepts in 1/10 of a second
    waitResponse();

    sendAT(GF("+VTS="), cmd);
    return waitResponse(10000L) == 1;
  }

  /*
   * Messaging functions
   */

  String sendUSSD(const String& code) TINY_GSM_ATTR_NOT_IMPLEMENTED;

  bool sendSMS(const String& number, const String& text) {
    sendAT(GF("+CMGF=1"));
    waitResponse();
    //Set GSM 7 bit default alphabet (3GPP TS 23.038)
    sendAT(GF("+CSCS=\"GSM\""));
    waitResponse();
    sendAT(GF("+CMGS=\""), number, GF("\""));
    if (waitResponse(GF(">")) != 1) {
      return false;
    }
    stream.print(text);
    stream.write((char)0x1A);
    stream.flush();
    return waitResponse(60000L) == 1;
  }

  bool sendSMS_UTF16(const String& number, const void* text, size_t len) {
    sendAT(GF("+CMGF=1"));
    waitResponse();
    sendAT(GF("+CSMP=17,167,0,8"));
    waitResponse();

    sendAT(GF("+CMGS=\""), number, GF("\""));
    if (waitResponse(GF(">")) != 1) {
      return false;
    }

    uint16_t* t = (uint16_t*)text;
    for (size_t i=0; i<len; i++) {
      uint8_t c = t[i] >> 8;
      if (c < 0x10) { stream.print('0'); }
      stream.print(c, HEX);
      c = t[i] & 0xFF;
      if (c < 0x10) { stream.print('0'); }
      stream.print(c, HEX);
    }
    stream.write((char)0x1A);
    stream.flush();
    return waitResponse(60000L) == 1;
  }


  /*
   * Location functions
   */

  bool gpsIsOn() {
    sendAT(GF("+QGNSSC?"));
    waitResponse(GF(GSM_NL "+QGNSSC:"));
    int mode = stream.readStringUntil('\n').toInt();
    waitResponse();

    return mode == 1;
  }

  bool gpsActivate() {
    if (gpsIsOn()) return true;

    sendAT(GF("+QGNSSC=1"));
    return waitResponse() == 1 ? true : false;
  }

  bool gpsDeactivate() {
    if (!gpsIsOn()) return true;

    sendAT(GF("+QGNSSC=0"));
    return waitResponse() == 1 ? true : false;
  }

  String getGpsData() {
    sendAT(GF("+QGNSSRD?"));
    if (waitResponse(GF(GSM_NL "+QGNSSRD:")) != 1) {
      return "";
    }

    String res = stream.readStringUntil('\n');
    for (int i = 0; i < 9; i++) {
      res += "\r\n" + stream.readStringUntil('\n');
    }

    waitResponse();
    res.trim();
    return res;
  }

  bool gpsEnableEPO() {
    if (!waitForNetwork()) return false;
    if (!waitForGpsTimeSync()) return false;

    sendAT(GF("+QGNSSEPO=1"));
    if (waitResponse() != 1) return false;

    return true;
  }

  bool gpsDisableEPO() {
    sendAT(GF("+QGNSSEPO=0"));
    if (waitResponse() != 1) return false;

    return true;
  }

  bool gpsTriggerEPO() {
    sendAT(GF("+QGEPOAID"));
    if (waitResponse() != 1) return false;

    return true;
  }

  bool gpsSetRefLocation(const char lat[], const char lng[]) {
    sendAT(GF("+QGREFLOC="), GF(lat), ',', GF(lng));
    if (waitResponse() != 1) return false;

    return true;
  }

  bool gpsIsTimeSynched() {
    sendAT(GF("+QGNSSTS?"));
    if (waitResponse(GF(GSM_NL "+QGNSSTS:")) != 1) return false;

    int status = stream.readStringUntil('\n').toInt();

    return status == 1;
  }

  /*
   * Battery functions
   */
  uint16_t getBattVoltage() TINY_GSM_ATTR_NOT_IMPLEMENTED;

  int getBattPercent() TINY_GSM_ATTR_NOT_IMPLEMENTED;

protected:

  bool modemConnect(const char* host, uint16_t port, uint8_t mux, bool ssl = false) {
    if (ssl) {
      // +QSSLOPEN=<ssid>,<ctxindex>,<ipaddr/domainname>,<port>,<connectmode>[,<timeout>]
      sendAT(GF("+QSSLOPEN="), mux, ',', mux, GF(",\""), host, GF("\","), port, GF(",0")); // default timeout is 90 sec
      if (waitResponse() != 1) return false;
      if(waitResponse(90000L, GF(GSM_NL "+QSSLOPEN:")) != 1) return false; // Fix this. Need to account for the right MUX in the response.
      int connectedMux = stream.readStringUntil(',').toInt();
      int connStatus = stream.readStringUntil('\n').toInt();
      if (connectedMux != mux || connStatus != 0) return false;
    } else {
      sendAT(GF("+QIOPEN="), mux, ',', GF("\"TCP"), GF("\",\""), host, GF("\","), port);
      if (waitResponse() != 1) return false;
      if(waitResponse(75000L, GF("CONNECT OK")) != 1) return false; // Fix this. Need to account for the right MUX in the response.
    }

    return true;
  }

  int modemSend(const void* buff, size_t len, uint8_t mux, bool ssl = false) {
    if (ssl) {
      sendAT(GF("+QSSLSEND="), mux, ',', len);
      if (waitResponse(GF(">")) != 1) {
        return 0;
      }
    } else {
      sendAT(GF("+QISEND="), mux, ',', len);
      if (waitResponse(GF(">")) != 1) {
        return 0;
      }
    }

    stream.write((uint8_t*)buff, len);
    stream.flush();
    if (waitResponse(GF(GSM_NL "SEND OK")) != 1) {
      return 0;
    }

    if (!ssl) {
      while(true) {
        sendAT(GF("+QISACK="), mux);
        waitResponse(GF("+QISACK:"));
        streamSkipUntil(',');
        streamSkipUntil(',');
        int unAckData = stream.readStringUntil('\n').toInt();
        waitResponse();
        if (unAckData == 0) break;
        delay(200);
      }
    }

    return len; 
  }

  size_t modemRead(size_t size, uint8_t mux, bool ssl = false) {
    if (ssl) {
      // QSSLRECV=<cid>,<ssid>,<length>
      sendAT(GF("+QSSLRECV=0,"), mux, ',', size);
      if (waitResponse(GF("+QSSLRECV:"), GF("OK"), GF("ERROR")) != 1) {
        return 0;
      }
    } else {
      sendAT(GF("+QIRD="), 0, ',', 1, ',', mux, ',', size);
      if (waitResponse(GF("+QIRD:"), GF("OK"), GF("ERROR")) != 1) {
        return 0;
      }
    }
    
    streamSkipUntil(','); // Skip addr + port
    streamSkipUntil(','); // Skip type

    size_t len = stream.readStringUntil('\n').toInt();

    for (size_t i=0; i<len; i++) {
      while (!stream.available()) { TINY_GSM_YIELD(); }
      char c = stream.read();
      sockets[mux]->rx.put(c);
    }

    waitResponse();
    
    // DBG("### READ:", mux, ",", len);
    return len;
  }

  size_t modemGetAvailable(uint8_t mux, bool ssl = false) {
    size_t result = 0;
    if (ssl) {
      // QSSLRECV=<cid>,<ssid>,<length>
      return modemRead(1500, mux, true); // We need to read eagerly, since we don't have a way to determine how much data there is
    } else {
      sendAT(GF("+QIRD="), 0, ',', 1, ',', mux, ',', 0);

      if (waitResponse(GF("+QIRD:"), GF("OK"), GF("ERROR")) == 1) {
        streamSkipUntil(','); // Skip addr + port
        streamSkipUntil(','); // Skip type
        result = stream.readStringUntil('\n').toInt();
        DBG("### STILL:", mux, "has", result);
        waitResponse();
      }
    }
    
    if (!result) {
      sockets[mux]->sock_connected = modemGetConnected(mux);
    }
    return result;
  }

  bool modemGetConnected(uint8_t mux) {
    sendAT(GF("+QSSLSTATE"));

    waitResponse();
    waitResponse();
    if (waitResponse(GF("+QSSLSTATE:")))
      return false;

    streamSkipUntil(','); // Skip mux
    streamSkipUntil(','); // Skip socket type
    streamSkipUntil(','); // Skip remote ip
    streamSkipUntil(','); // Skip remote port
    String res = stream.readStringUntil(','); // socket state
    streamSkipUntil('\n');

    waitResponse();

    char resChar[res.length()+1];
    strcpy(resChar, res.c_str());
    // 0 Initial, 1 Opening, 2 Connected, 3 Listening, 4 Closing
    return strcmp(resChar, "CONNECTED");
  }

public:

  /* Utilities */

  template<typename T>
  void streamWrite(T last) {
    stream.print(last);
  }

  template<typename T, typename... Args>
  void streamWrite(T head, Args... tail) {
    stream.print(head);
    streamWrite(tail...);
  }

  bool streamSkipUntil(char c) {
    const unsigned long timeout = 1000L;
    unsigned long startMillis = millis();
    while (millis() - startMillis < timeout) {
      while (millis() - startMillis < timeout && !stream.available()) {
        TINY_GSM_YIELD();
      }
      if (stream.read() == c)
        return true;
    }
    return false;
  }

  template<typename... Args>
  void sendAT(Args... cmd) {
    streamWrite("AT", cmd..., GSM_NL);
    stream.flush();
    TINY_GSM_YIELD();
    //DBG("### AT:", cmd...);
  }

  // TODO: Optimize this!
  uint8_t waitResponse(uint32_t timeout, String& data,
                       GsmConstStr r1=GFP(GSM_OK), GsmConstStr r2=GFP(GSM_ERROR),
                       GsmConstStr r3=NULL, GsmConstStr r4=NULL, GsmConstStr r5=NULL)
  {
    /*String r1s(r1); r1s.trim();
    String r2s(r2); r2s.trim();
    String r3s(r3); r3s.trim();
    String r4s(r4); r4s.trim();
    String r5s(r5); r5s.trim();
    DBG("### ..:", r1s, ",", r2s, ",", r3s, ",", r4s, ",", r5s);*/
    data.reserve(64);
    int index = 0;
    unsigned long startMillis = millis();
    do {
      TINY_GSM_YIELD();
      while (stream.available() > 0) {
        int a = stream.read();
        if (a <= 0) continue; // Skip 0x00 bytes, just in case
        data += (char)a;
        if (r1 && data.endsWith(r1)) {
          index = 1;
          goto finish;
        } else if (r2 && data.endsWith(r2)) {
          index = 2;
          goto finish;
        } else if (r3 && data.endsWith(r3)) {
          index = 3;
          goto finish;
        } else if (r4 && data.endsWith(r4)) {
          index = 4;
          goto finish;
        } else if (r5 && data.endsWith(r5)) {
          index = 5;
          goto finish;
        } else if (data.endsWith(GF(GSM_NL "+QIURC:"))) {
          stream.readStringUntil('\"');
          String urc = stream.readStringUntil('\"');
          stream.readStringUntil(',');
          if (urc == "closed") {
            int mux = stream.readStringUntil('\n').toInt();
            DBG("### URC CLOSE:", mux);
            if (mux >= 0 && mux < TINY_GSM_MUX_COUNT && sockets[mux]) {
              sockets[mux]->sock_connected = false;
            }
          } else {
            stream.readStringUntil('\n');
          }
          data = "";
        } else if (data.endsWith(GF(GSM_NL "+QIRDI:"))) {
          int context = stream.readStringUntil(',').toInt();
          streamSkipUntil(','); // Skip device role (client/server)
          int mux = stream.readStringUntil(',').toInt();
          streamSkipUntil('\n');
          // DBG("### URC QIRDI:", mux);
          if (mux >= 0 && mux < TINY_GSM_MUX_COUNT && sockets[mux]) {
            sockets[mux]->got_data = true;
          }
          data = "";
        } else if (data.endsWith(GF(GSM_NL "+QSSLURC:"))) {
          stream.readStringUntil('\"');
          String urc = stream.readStringUntil('\"');
          stream.readStringUntil(',');
          if (urc == "recv") {
            int mux = stream.readStringUntil('\n').toInt();
            // DBG("### QSSLURC RECV:", mux);
            int free = sockets[mux]->rx.free();
            int len = modemRead(1500, mux, true);

            if (len > free) {
              // DBG("### Buffer overflow: ", len, "->", free);
            } else {
              // DBG("### Got: ", len, "->", free);
            }

            if (len > sockets[mux]->available()) { // TODO
              // DBG("### Fewer characters received than expected: ", sockets[mux]->available(), " vs ", len);
            }
          } else if (urc == "closed") {
            int mux = stream.readStringUntil('\n').toInt();
            // DBG("### QSSLURC CLOSE:", mux);
            if (mux >= 0 && mux < TINY_GSM_MUX_COUNT && sockets[mux]) {
              sockets[mux]->sock_connected = false;
            }
          } else {
            stream.readStringUntil('\n');
          }
          data = "";
        }
      }
    } while (millis() - startMillis < timeout);
finish:
    if (!index) {
      data.trim();
      if (data.length()) {
        DBG("### Unhandled:", data);
      }
      data = "";
    }
    //DBG('<', index, '>');
    return index;
  }

  uint8_t waitResponse(uint32_t timeout,
                       GsmConstStr r1=GFP(GSM_OK), GsmConstStr r2=GFP(GSM_ERROR),
                       GsmConstStr r3=NULL, GsmConstStr r4=NULL, GsmConstStr r5=NULL)
  {
    String data;
    return waitResponse(timeout, data, r1, r2, r3, r4, r5);
  }

  uint8_t waitResponse(GsmConstStr r1=GFP(GSM_OK), GsmConstStr r2=GFP(GSM_ERROR),
                       GsmConstStr r3=NULL, GsmConstStr r4=NULL, GsmConstStr r5=NULL)
  {
    return waitResponse(1000, r1, r2, r3, r4, r5);
  }

public:
  Stream&       stream;

protected:
  GsmClient*    sockets[TINY_GSM_MUX_COUNT];
};

#endif
