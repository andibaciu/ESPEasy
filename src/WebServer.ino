#define _HEAD false
#define _TAIL true
#define CHUNKED_BUFFER_SIZE          400


//********************************************************************************
// Core part of WebServer, the chunked streaming buffer
// This must remain in the WebServer.ino file at the top.
//********************************************************************************
void sendContentBlocking(String& data);
void sendHeaderBlocking(bool json, const String& origin = "");

class StreamingBuffer {
private:
  bool lowMemorySkip;

public:
  uint32_t initialRam;
  uint32_t beforeTXRam;
  uint32_t duringTXRam;
  uint32_t finalRam;
  uint32_t maxCoreUsage;
  uint32_t maxServerUsage;
  unsigned int sentBytes;
  uint32_t flashStringCalls;
  uint32_t flashStringData;
private:
  String buf;

public:

  StreamingBuffer(void) : lowMemorySkip(false),
    initialRam(0), beforeTXRam(0), duringTXRam(0), finalRam(0), maxCoreUsage(0),
    maxServerUsage(0), sentBytes(0), flashStringCalls(0), flashStringData(0)
  {
    buf.reserve(CHUNKED_BUFFER_SIZE + 50);
    buf = "";
  }
  StreamingBuffer operator= (String& a)                 { flush(); return addString(a); }
  StreamingBuffer operator= (const String& a)           { flush(); return addString(a); }
  StreamingBuffer operator+= (char a)                   { return addString(String(a)); }
  StreamingBuffer operator+= (long unsigned int  a)     { return addString(String(a)); }
  StreamingBuffer operator+= (float a)                  { return addString(String(a)); }
  StreamingBuffer operator+= (int a)                    { return addString(String(a)); }
  StreamingBuffer operator+= (uint32_t a)               { return addString(String(a)); }
  StreamingBuffer operator+= (const String& a)          { return addString(a); }

  StreamingBuffer operator+= (PGM_P str) {
    ++flashStringCalls;
    if (!str) return *this; // return if the pointer is void
    if (lowMemorySkip) return *this;
    int flush_step = CHUNKED_BUFFER_SIZE - this->buf.length();
    if (flush_step < 1) flush_step = 0;
    unsigned int pos = 0;
    const unsigned int length = strlen_P((PGM_P)str);
    if (length == 0) return *this;
    flashStringData += length;
    while (pos < length) {
      if (flush_step == 0) {
        sendContentBlocking(this->buf);
        flush_step = CHUNKED_BUFFER_SIZE;
      }
      this->buf += (char)pgm_read_byte(&str[pos]);
      ++pos;
      --flush_step;
    }
    checkFull();
    return *this;
  }


  StreamingBuffer addString(const String& a) {
    if (lowMemorySkip) return *this;
    int flush_step = CHUNKED_BUFFER_SIZE - this->buf.length();
    if (flush_step < 1) flush_step = 0;
    int pos = 0;
    const int length = a.length();
    while (pos < length) {
      if (flush_step == 0) {
        sendContentBlocking(this->buf);
        flush_step = CHUNKED_BUFFER_SIZE;
      }
      this->buf += a[pos];
      ++pos;
      --flush_step;
    }
    checkFull();
    return *this;
  }

  void flush() {
    if (lowMemorySkip) {
      this->buf = "";
    } else {
      sendContentBlocking(this->buf);
    }
  }

  void checkFull(void) {
    if (lowMemorySkip) this->buf = "";
    if (this->buf.length() > CHUNKED_BUFFER_SIZE) {
      trackTotalMem();
      sendContentBlocking(this->buf);
    }
  }

  void startStream() {
    startStream(false, "");
  }

  void startStream(const String& origin) {
    startStream(false, origin);
  }

  void startJsonStream() {
    startStream(true, "*");
  }

private:
  void startStream(bool json, const String& origin) {
    maxCoreUsage = maxServerUsage = 0;
    initialRam = ESP.getFreeHeap();
    beforeTXRam = initialRam;
    sentBytes = 0;
    buf = "";
    if (beforeTXRam < 3000) {
      lowMemorySkip = true;
      WebServer.send(200, "text/plain", "Low memory. Cannot display webpage :-(");
       #if defined(ESP8266)
         tcpCleanup();
       #endif
      return;
    } else
      sendHeaderBlocking(json, origin);
  }

  void trackTotalMem() {
    beforeTXRam = ESP.getFreeHeap();
    if ((initialRam - beforeTXRam) > maxServerUsage)
      maxServerUsage = initialRam - beforeTXRam;
  }

public:

  void trackCoreMem() {
    duringTXRam = ESP.getFreeHeap();
    if ((initialRam - duringTXRam) > maxCoreUsage)
      maxCoreUsage = (initialRam - duringTXRam);
  }

  void endStream(void) {
    if (!lowMemorySkip) {
      if (buf.length() > 0) sendContentBlocking(buf);
      buf = "";
      sendContentBlocking(buf);
      finalRam = ESP.getFreeHeap();
      /*
      if (loglevelActiveFor(LOG_LEVEL_DEBUG)) {
        String log = String("Ram usage: Webserver only: ") + maxServerUsage +
                     " including Core: " + maxCoreUsage +
                     " flashStringCalls: " + flashStringCalls +
                     " flashStringData: " + flashStringData;
        addLog(LOG_LEVEL_DEBUG, log);
      }
      */
    } else {
      addLog(LOG_LEVEL_ERROR, String("Webpage skipped: low memory: ") + finalRam);
      lowMemorySkip = false;
    }
  }
} TXBuffer;

void sendContentBlocking(String& data) {
  checkRAM(F("sendContentBlocking"));
  uint32_t freeBeforeSend = ESP.getFreeHeap();
  const uint32_t length = data.length();
#ifndef BUILD_NO_DEBUG
  addLog(LOG_LEVEL_DEBUG_DEV, String("sendcontent free: ") + freeBeforeSend + " chunk size:" + length);
#endif
  freeBeforeSend = ESP.getFreeHeap();
  if (TXBuffer.beforeTXRam > freeBeforeSend)
    TXBuffer.beforeTXRam = freeBeforeSend;
  TXBuffer.duringTXRam = freeBeforeSend;
#if defined(ESP8266) && defined(ARDUINO_ESP8266_RELEASE_2_3_0)
  String size = formatToHex(length) + "\r\n";
  // do chunked transfer encoding ourselves (WebServer doesn't support it)
  WebServer.sendContent(size);
  if (length > 0) WebServer.sendContent(data);
  WebServer.sendContent("\r\n");
#else  // ESP8266 2.4.0rc2 and higher and the ESP32 webserver supports chunked http transfer
  unsigned int timeout = 0;
  if (freeBeforeSend < 5000) timeout = 100;
  if (freeBeforeSend < 4000) timeout = 1000;
  const uint32_t beginWait = millis();
  WebServer.sendContent(data);
  while ((ESP.getFreeHeap() < freeBeforeSend) &&
         !timeOutReached(beginWait + timeout)) {
    if (ESP.getFreeHeap() < TXBuffer.duringTXRam)
      TXBuffer.duringTXRam = ESP.getFreeHeap();
    ;
    TXBuffer.trackCoreMem();
    checkRAM(F("duringDataTX"));
    delay(1);
  }
#endif

  TXBuffer.sentBytes += length;
  data = "";
  delay(0);
}

void sendHeaderBlocking(bool json, const String& origin) {
  checkRAM(F("sendHeaderBlocking"));
  WebServer.client().flush();
  String contenttype;
  if (json)
    contenttype = F("application/json");
  else
    contenttype = F("text/html");

#if defined(ESP8266) && defined(ARDUINO_ESP8266_RELEASE_2_3_0)
  WebServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  WebServer.sendHeader(F("Accept-Ranges"), F("none"));
  WebServer.sendHeader(F("Cache-Control"), F("no-cache"));
  WebServer.sendHeader(F("Transfer-Encoding"), F("chunked"));
  if (json)
    WebServer.sendHeader(F("Access-Control-Allow-Origin"),"*");
  WebServer.send(200, contenttype, "");
#else
  unsigned int timeout = 0;
  uint32_t freeBeforeSend = ESP.getFreeHeap();
  if (freeBeforeSend < 5000) timeout = 100;
  if (freeBeforeSend < 4000) timeout = 1000;
  const uint32_t beginWait = millis();
  WebServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  WebServer.sendHeader(F("Cache-Control"), F("no-cache"));
  if (origin.length() > 0) {
    WebServer.sendHeader(F("Access-Control-Allow-Origin"), origin);
  }
  WebServer.send(200, contenttype, "");
  // dont wait on 2.3.0. Memory returns just too slow.
  while ((ESP.getFreeHeap() < freeBeforeSend) &&
         !timeOutReached(beginWait + timeout)) {
    checkRAM(F("duringHeaderTX"));
    delay(1);
  }
#endif
  delay(0);
}

void sendHeadandTail(const String& tmplName, boolean Tail = false, boolean rebooting = false) {
  String pageTemplate = "";
  String fileName = tmplName;
  fileName += F(".htm");
  fs::File f = tryOpenFile(fileName, "r");

  if (f) {
    pageTemplate.reserve(f.size());
    while (f.available()) pageTemplate += (char)f.read();
    f.close();
  } else {
    // TODO TD-er: Should send data directly to TXBuffer instead of using large strings.
    getWebPageTemplateDefault(tmplName, pageTemplate);
  }
  checkRAM(F("sendWebPage"));
  // web activity timer
  lastWeb = millis();

  if (Tail) {
    TXBuffer += pageTemplate.substring(
        11 + // Size of "{{content}}"
        pageTemplate.indexOf(F("{{content}}")));  // advance beyond content key
  } else {
    int indexStart = 0;
    int indexEnd = 0;
    int readPos = 0; // Position of data sent to TXBuffer
    String varName;  //, varValue;
    String meta;
    if(rebooting){
      meta = F("<meta http-equiv='refresh' content='10 url=/'>");
    }
    while ((indexStart = pageTemplate.indexOf(F("{{"), indexStart)) >= 0) {
      TXBuffer += pageTemplate.substring(readPos, indexStart);
      readPos = indexStart;
      if ((indexEnd = pageTemplate.indexOf(F("}}"), indexStart)) > 0) {
        varName = pageTemplate.substring(indexStart + 2, indexEnd);
        indexStart = indexEnd + 2;
        readPos = indexEnd + 2;
        varName.toLowerCase();

        if (varName == F("content")) {  // is var == page content?
          break;  // send first part of result only
        } else if (varName == F("error")) {
          getErrorNotifications();
        }
        else if(varName == F("meta")) {
          TXBuffer += meta;
        }
        else {
          getWebPageTemplateVar(varName);
        }
      } else {  // no closing "}}"
        // eat "{{"
        readPos += 2;
        indexStart += 2;
      }
    }
  }
  if (shouldReboot) {
    //we only add this here as a seperate chunk to prevent using too much memory at once
    html_add_script(false);
    TXBuffer += DATA_REBOOT_JS;
    html_add_script_end();
  }
}

void sendHeadandTail_stdtemplate(boolean Tail = false, boolean rebooting = false) {
  sendHeadandTail(F("TmplStd"), Tail, rebooting);
}

//********************************************************************************
// Web Interface init
//********************************************************************************
//#include "core_version.h"
#define HTML_SYMBOL_WARNING "&#9888;"
#define HTML_SYMBOL_INPUT   "&#8656;"
#define HTML_SYMBOL_OUTPUT  "&#8658;"
#define HTML_SYMBOL_I_O     "&#8660;"


#if defined(ESP8266)
  #define TASKS_PER_PAGE 12
#endif
#if defined(ESP32)
  #define TASKS_PER_PAGE 32
#endif

#define strncpy_webserver_arg(D,N) safe_strncpy(D, WebServer.arg(N).c_str(), sizeof(D));
#define update_whenset_FormItemInt(K,V) { int tmpVal; if (getCheckWebserverArg_int(K, tmpVal)) V=tmpVal;}




void WebServerInit()
{
  // Prepare webserver pages
  WebServer.on("/", handle_root);
  WebServer.on(F("/advanced"), handle_advanced);
  WebServer.on(F("/config"), handle_config);
  WebServer.on(F("/control"), handle_control);
  WebServer.on(F("/controllers"), handle_controllers);
  WebServer.on(F("/devices"), handle_devices);
  WebServer.on(F("/download"), handle_download);

  
  WebServer.on(F("/dumpcache"), handle_dumpcache);    // C016 specific entrie
  WebServer.on(F("/cache_json"), handle_cache_json);  // C016 specific entrie
  WebServer.on(F("/cache_csv"), handle_cache_csv);    // C016 specific entrie


  WebServer.on(F("/factoryreset"), handle_factoryreset);
  WebServer.on(F("/favicon.ico"), handle_favicon);
  WebServer.on(F("/filelist"), handle_filelist);
  WebServer.on(F("/hardware"), handle_hardware);
  WebServer.on(F("/i2cscanner"), handle_i2cscanner);
  WebServer.on(F("/json"), handle_json); // Also part of WEBSERVER_NEW_UI
  WebServer.on(F("/log"), handle_log);
  WebServer.on(F("/login"), handle_login);
  WebServer.on(F("/logjson"), handle_log_JSON); // Also part of WEBSERVER_NEW_UI
#ifndef NOTIFIER_SET_NONE
  WebServer.on(F("/notifications"), handle_notifications);
#endif
  WebServer.on(F("/pinstates"), handle_pinstates);
  WebServer.on(F("/rules"), handle_rules_new);
  WebServer.on(F("/rules/"), Goto_Rules_Root);
  WebServer.on(F("/rules/add"), []()
  {
    handle_rules_edit(WebServer.uri(),true);
  });
  WebServer.on(F("/rules/backup"), handle_rules_backup);
  WebServer.on(F("/rules/delete"), handle_rules_delete);
#ifdef FEATURE_SD
  WebServer.on(F("/SDfilelist"), handle_SDfilelist);
#endif
  WebServer.on(F("/setup"), handle_setup);
  WebServer.on(F("/sysinfo"), handle_sysinfo);
#ifdef WEBSERVER_SYSVARS
  WebServer.on(F("/sysvars"), handle_sysvars);
#endif // WEBSERVER_SYSVARS
#ifdef WEBSERVER_TIMINGSTATS
  WebServer.on(F("/timingstats"), handle_timingstats);
#endif // WEBSERVER_TIMINGSTATS
  WebServer.on(F("/tools"), handle_tools);
  WebServer.on(F("/upload"), HTTP_GET, handle_upload);
  WebServer.on(F("/upload"), HTTP_POST, handle_upload_post, handleFileUpload);
  WebServer.on(F("/wifiscanner"), handle_wifiscanner);

#ifdef WEBSERVER_NEW_UI
  WebServer.on(F("/factoryreset_json"), handle_factoryreset_json);
  WebServer.on(F("/filelist_json"), handle_filelist_json);
  WebServer.on(F("/i2cscanner_json"), handle_i2cscanner_json);
  WebServer.on(F("/node_list_json"), handle_nodes_list_json);
  WebServer.on(F("/pinstates_json"), handle_pinstates_json);
  WebServer.on(F("/sysinfo_json"), handle_sysinfo_json);
  WebServer.on(F("/timingstats_json"), handle_timingstats_json);
  WebServer.on(F("/upload_json"), HTTP_POST, handle_upload_json, handleFileUpload);
  WebServer.on(F("/wifiscanner_json"), handle_wifiscanner_json);
#endif // WEBSERVER_NEW_UI

  WebServer.onNotFound(handleNotFound);

  #if defined(ESP8266)
  {
    uint32_t maxSketchSize;
    bool use2step;
    if (OTA_possible(maxSketchSize, use2step)) {
      httpUpdater.setup(&WebServer);
    }
  }
  #endif

  #if defined(ESP8266)
  if (Settings.UseSSDP)
  {
    WebServer.on(F("/ssdp.xml"), HTTP_GET, []() {
      WiFiClient client(WebServer.client());
      client.setTimeout(CONTROLLER_CLIENTTIMEOUT_DFLT);
      SSDP_schema(client);
    });
    SSDP_begin();
  }
  #endif
}

void setWebserverRunning(bool state) {
  if (webserver_state == state)
    return;
  if (state) {
    if (!webserver_init) {
      WebServerInit();
      webserver_init = true;
    }
    WebServer.begin();
    addLog(LOG_LEVEL_INFO, F("Webserver: start"));
  } else {
    WebServer.stop();
    addLog(LOG_LEVEL_INFO, F("Webserver: stop"));
  }
  webserver_state = state;
}


void getWebPageTemplateDefault(const String& tmplName, String& tmpl)
{
  tmpl.reserve(576);
  const bool addJS = true;
  const bool addMeta = true;
  if (tmplName == F("TmplAP"))
  {
    getWebPageTemplateDefaultHead(tmpl, !addMeta, !addJS);
    tmpl += F("<body>"
              "<header class='apheader'>"
              "<h1>Welcome to ESP Easy Mega AP</h1>"
              "</header>");
    getWebPageTemplateDefaultContentSection(tmpl);
    getWebPageTemplateDefaultFooter(tmpl);
  }
  else if (tmplName == F("TmplMsg"))
  {
    getWebPageTemplateDefaultHead(tmpl, !addMeta, !addJS);
    tmpl += F("<body>");
    getWebPageTemplateDefaultHeader(tmpl, F("{{name}}"), false);
    getWebPageTemplateDefaultContentSection(tmpl);
    getWebPageTemplateDefaultFooter(tmpl);
  }
  else if (tmplName == F("TmplDsh"))
  {
    getWebPageTemplateDefaultHead(tmpl, !addMeta, addJS);
    tmpl += F(
        "<body>"
        "{{content}}"
        "</body></html>"
            );
  }
  else   //all other template names e.g. TmplStd
  {
    getWebPageTemplateDefaultHead(tmpl, addMeta, addJS);
    tmpl += F("<body class='bodymenu'>"
              "<span class='message' id='rbtmsg'></span>");
    getWebPageTemplateDefaultHeader(tmpl, F("{{name}} {{logo}}"), true);
    getWebPageTemplateDefaultContentSection(tmpl);
    getWebPageTemplateDefaultFooter(tmpl);
  }
}

void getWebPageTemplateDefaultHead(String& tmpl, bool addMeta, bool addJS) {
  tmpl += F("<!DOCTYPE html><html lang='en'>"
              "<head>"
                "<meta charset='utf-8'/>"
                "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
                "<title>{{name}}</title>");
  if (addMeta) tmpl += F("{{meta}}");
  if (addJS) tmpl += F("{{js}}");

  tmpl += F("{{css}}"
            "</head>");
}

void getWebPageTemplateDefaultHeader(String& tmpl, const String& title, bool addMenu) {
  tmpl += F("<header class='headermenu'>"
            "<h1>ESP Easy Mega: ");
  tmpl += title;
  tmpl += F("</h1><BR>");
  if (addMenu) tmpl += F("{{menu}}");
  tmpl += F("</header>");
}

void getWebPageTemplateDefaultContentSection(String& tmpl) {
  tmpl += F("<section>"
              "<span class='message error'>"
                "{{error}}"
              "</span>"
              "{{content}}"
            "</section>"
          );
}

void getWebPageTemplateDefaultFooter(String& tmpl) {
  tmpl += F("<footer>"
              "<br>"
              "<h6>Powered by <a href='http://www.letscontrolit.com' style='font-size: 15px; text-decoration: none'>Let's Control It</a> community</h6>"
            "</footer>"
            "</body></html>"
          );
}

void getErrorNotifications() {
  // Check number of MQTT controllers active.
  int nrMQTTenabled = 0;
  for (byte x = 0; x < CONTROLLER_MAX; x++) {
    if (Settings.Protocol[x] != 0) {
      byte ProtocolIndex = getProtocolIndex(Settings.Protocol[x]);
      if (Settings.ControllerEnabled[x] && Protocol[ProtocolIndex].usesMQTT) {
        ++nrMQTTenabled;
      }
    }
  }
  if (nrMQTTenabled > 1) {
    // Add warning, only one MQTT protocol should be used.
    addHtmlError(F("Only one MQTT controller should be active."));
  }
  // Check checksum of stored settings.
}


#define MENU_INDEX_MAIN          0
#define MENU_INDEX_CONFIG        1
#define MENU_INDEX_CONTROLLERS   2
#define MENU_INDEX_HARDWARE      3
#define MENU_INDEX_DEVICES       4
#define MENU_INDEX_RULES         5
#define MENU_INDEX_NOTIFICATIONS 6
#define MENU_INDEX_TOOLS         7
static byte navMenuIndex = MENU_INDEX_MAIN;


void getWebPageTemplateVar(const String& varName )
{
 // serialPrint(varName); serialPrint(" : free: "); serialPrint(ESP.getFreeHeap());   serialPrint("var len before:  "); serialPrint (varValue.length()) ;serialPrint("after:  ");
 //varValue = "";

  if (varName == F("name"))
  {
    TXBuffer += Settings.Name;
  }

  else if (varName == F("unit"))
  {
    TXBuffer += String(Settings.Unit);
  }

  else if (varName == F("menu"))
  {
    static const __FlashStringHelper* gpMenu[8][3] = {
      // See https://github.com/letscontrolit/ESPEasy/issues/1650
      // Icon,        Full width label,   URL
      F("&#8962;"),   F("Main"),          F("/"),              //0
      F("&#9881;"),   F("Config"),        F("/config"),        //1
      F("&#128172;"), F("Controllers"),   F("/controllers"),   //2
      F("&#128204;"), F("Hardware"),      F("/hardware"),      //3
      F("&#128268;"), F("Devices"),       F("/devices"),       //4
      F("&#10740;"),  F("Rules"),         F("/rules"),         //5
      F("&#9993;"),   F("Notifications"), F("/notifications"), //6
      F("&#128295;"), F("Tools"),         F("/tools"),         //7
    };

    TXBuffer += F("<div class='menubar'>");

    for (byte i = 0; i < 8; i++)
    {
      if (i == MENU_INDEX_RULES && !Settings.UseRules)   //hide rules menu item
        continue;
#ifdef NOTIFIER_SET_NONE
      if (i == MENU_INDEX_NOTIFICATIONS)   //hide notifications menu item
        continue;
#endif

      TXBuffer += F("<a class='menu");
      if (i == navMenuIndex)
        TXBuffer += F(" active");
      TXBuffer += F("' href='");
      TXBuffer += gpMenu[i][2];
      TXBuffer += "'>";
      TXBuffer += gpMenu[i][0];
      TXBuffer += F("<span class='showmenulabel'>");
      TXBuffer += gpMenu[i][1];
      TXBuffer += F("</span>");
      TXBuffer += F("</a>");
    }

    TXBuffer += F("</div>");
  }

  else if (varName == F("logo"))
  {
    if (SPIFFS.exists(F("esp.png")))
    {
      TXBuffer = F("<img src=\"esp.png\" width=48 height=48 align=right>");
    }
  }

  else if (varName == F("css"))
  {
    if (SPIFFS.exists(F("esp.css")))   //now css is written in writeDefaultCSS() to SPIFFS and always present
    //if (0) //TODO
    {
      TXBuffer = F("<link rel=\"stylesheet\" type=\"text/css\" href=\"esp.css\">");
    }
   else
    {
      TXBuffer += F("<style>");
      // Send CSS per chunk to avoid sending either too short or too large strings.
      TXBuffer += DATA_ESPEASY_DEFAULT_MIN_CSS;
      TXBuffer += F("</style>");
    }
  }


  else if (varName == F("js"))
  {
    html_add_autosubmit_form();
  }

  else if (varName == F("error"))
  {
    //print last error - not implemented yet
  }

  else if (varName == F("debug"))
  {
    //print debug messages - not implemented yet
  }

  else
  {
    if (loglevelActiveFor(LOG_LEVEL_ERROR)) {
      String log = F("Templ: Unknown Var : ");
      log += varName;
      addLog(LOG_LEVEL_ERROR, log);
    }
    //no return string - eat var name
  }

 }


void writeDefaultCSS(void)
{
  return; //TODO

  if (!SPIFFS.exists(F("esp.css")))
  {
    String defaultCSS;

    fs::File f = tryOpenFile(F("esp.css"), "w");
    if (f)
    {
      if (loglevelActiveFor(LOG_LEVEL_INFO)) {
        String log = F("CSS  : Writing default CSS file to SPIFFS (");
        log += defaultCSS.length();
        log += F(" bytes)");
        addLog(LOG_LEVEL_INFO, log);
      }
      defaultCSS= PGMT(DATA_ESPEASY_DEFAULT_MIN_CSS);
      f.write((const unsigned char*)defaultCSS.c_str(), defaultCSS.length());   //note: content must be in RAM - a write of F("XXX") does not work
      f.close();
    }

  }
}


int8_t level = 0;
int8_t lastLevel = -1;

void json_quote_name(const String& val) {
  if (lastLevel == level) TXBuffer += ",";
  if (val.length() > 0) {
    TXBuffer += '\"';
    TXBuffer += val;
    TXBuffer += '\"';
    TXBuffer += ':';
  }
}

void json_quote_val(const String& val) {
  TXBuffer += '\"';
  TXBuffer += val;
  TXBuffer += '\"';
}

void json_open(bool arr = false, const String& name = String()) {
  json_quote_name(name);
  TXBuffer += arr ? "[" : "{";
  lastLevel = level;
  level++;
}

void json_init() {
  level = 0;
  lastLevel = -1;
}

void json_close(bool arr = false) {
  TXBuffer += arr ? "]" : "}";
  level--;
  lastLevel = level;
}

void json_number(const String& name, const String& value) {
  json_quote_name(name);
  json_quote_val(value);
  lastLevel = level;
}

void json_prop(const String& name, const String& value) {
  json_quote_name(name);
  json_quote_val(value);
  lastLevel = level;
}











//********************************************************************************
// Web Interface device page
//********************************************************************************
//19480 (11128)

// change of device: cleanup old device and reset default settings
void setTaskDevice_to_TaskIndex(byte taskdevicenumber, byte taskIndex) {
  struct EventStruct TempEvent;
  TempEvent.TaskIndex = taskIndex;
  String dummy;

  //let the plugin do its cleanup by calling PLUGIN_EXIT with this TaskIndex
  PluginCall(PLUGIN_EXIT, &TempEvent, dummy);
  taskClear(taskIndex, false); // clear settings, but do not save
  ClearCustomTaskSettings(taskIndex);

  Settings.TaskDeviceNumber[taskIndex] = taskdevicenumber;
  if (taskdevicenumber != 0) // set default values if a new device has been selected
  {
    //NOTE: do not enable task by default. allow user to enter sensible valus first and let him enable it when ready.
    PluginCall(PLUGIN_SET_DEFAULTS, &TempEvent, dummy);
    PluginCall(PLUGIN_GET_DEVICEVALUENAMES, &TempEvent, dummy); //the plugin should populate ExtraTaskSettings with its default values.
  } else {
    // New task is empty task, thus save config now.
    taskClear(taskIndex, true); // clear settings, and save
  }
}

void setBasicTaskValues(byte taskIndex, unsigned long taskdevicetimer,
                        bool enabled, const String& name, int pin1, int pin2, int pin3) {
    LoadTaskSettings(taskIndex); // Make sure ExtraTaskSettings are up-to-date
    byte DeviceIndex = getDeviceIndex(Settings.TaskDeviceNumber[taskIndex]);
    if (taskdevicetimer > 0) {
      Settings.TaskDeviceTimer[taskIndex] = taskdevicetimer;
    } else {
      if (!Device[DeviceIndex].TimerOptional) // Set default delay, unless it's optional...
        Settings.TaskDeviceTimer[taskIndex] = Settings.Delay;
      else
        Settings.TaskDeviceTimer[taskIndex] = 0;
    }
    Settings.TaskDeviceEnabled[taskIndex] = enabled;
    safe_strncpy(ExtraTaskSettings.TaskDeviceName, name.c_str(), sizeof(ExtraTaskSettings.TaskDeviceName));
    if (pin1 >= 0) Settings.TaskDevicePin1[taskIndex] = pin1;
    if (pin2 >= 0) Settings.TaskDevicePin2[taskIndex] = pin2;
    if (pin3 >= 0) Settings.TaskDevicePin3[taskIndex] = pin3;
    SaveTaskSettings(taskIndex);
}




//********************************************************************************
// Add a task select dropdown list
//********************************************************************************
void addTaskSelect(const String& name,  int choice)
{
  String deviceName;

  TXBuffer += F("<select id='selectwidth' name='");
  TXBuffer += name;
  TXBuffer += F("' onchange='return dept_onchange(frmselect)'>");

  for (byte x = 0; x < TASKS_MAX; x++)
  {
    deviceName = "";
    if (Settings.TaskDeviceNumber[x] != 0 )
    {
      byte DeviceIndex = getDeviceIndex(Settings.TaskDeviceNumber[x]);

      if (Plugin_id[DeviceIndex] != 0)
        deviceName = getPluginNameFromDeviceIndex(DeviceIndex);
    }
    LoadTaskSettings(x);
    TXBuffer += F("<option value='");
    TXBuffer += x;
    TXBuffer += '\'';
    if (choice == x)
      TXBuffer += F(" selected");
    if (Settings.TaskDeviceNumber[x] == 0)
      addDisabled();
    TXBuffer += '>';
    TXBuffer += x + 1;
    TXBuffer += F(" - ");
    TXBuffer += deviceName;
    TXBuffer += F(" - ");
    TXBuffer += ExtraTaskSettings.TaskDeviceName;
    TXBuffer += F("</option>");
  }
}



//********************************************************************************
// Add a Value select dropdown list, based on TaskIndex
//********************************************************************************
void addTaskValueSelect(const String& name, int choice, byte TaskIndex)
{
  TXBuffer += F("<select id='selectwidth' name='");
  TXBuffer += name;
  TXBuffer += "'>";

  byte DeviceIndex = getDeviceIndex(Settings.TaskDeviceNumber[TaskIndex]);

  for (byte x = 0; x < Device[DeviceIndex].ValueCount; x++)
  {
    TXBuffer += F("<option value='");
    TXBuffer += x;
    TXBuffer += '\'';
    if (choice == x)
      TXBuffer += F(" selected");
    TXBuffer += '>';
    TXBuffer += ExtraTaskSettings.TaskDeviceValueNames[x];
    TXBuffer += F("</option>");
  }
}












//********************************************************************************
// Login state check
//********************************************************************************
boolean isLoggedIn()
{
  String www_username = F(DEFAULT_ADMIN_USERNAME);
  if (!clientIPallowed()) return false;
  if (SecuritySettings.Password[0] == 0) return true;
  if (!WebServer.authenticate(www_username.c_str(), SecuritySettings.Password))
      //Basic Auth Method with Custom realm and Failure Response
      //return server.requestAuthentication(BASIC_AUTH, www_realm, authFailResponse);
      //Digest Auth Method with realm="Login Required" and empty Failure Response
      //return server.requestAuthentication(DIGEST_AUTH);
      //Digest Auth Method with Custom realm and empty Failure Response
      //return server.requestAuthentication(DIGEST_AUTH, www_realm);
      //Digest Auth Method with Custom realm and Failure Response
  {
#ifdef CORE_PRE_2_5_0
    // See https://github.com/esp8266/Arduino/issues/4717
    HTTPAuthMethod mode = BASIC_AUTH;
#else
    HTTPAuthMethod mode = DIGEST_AUTH;
#endif
    String message = F("Login Required (default user: ");
    message += www_username;
    message += ')';
    WebServer.requestAuthentication(mode, message.c_str());
    return false;
  }
  return true;
}












String getControllerSymbol(byte index)
{
  String ret = F("<p style='font-size:20px'>&#");
  ret += 10102 + index;
  ret += F(";</p>");
  return ret;
}

/*
String getValueSymbol(byte index)
{
  String ret = F("&#");
  ret += 10112 + index;
  ret += ';';
  return ret;
}
*/



void createSvgRectPath(unsigned int color, int xoffset, int yoffset, int size, int height, int range, float SVG_BAR_WIDTH) {
  float width = SVG_BAR_WIDTH * size / range;
  if (width < 2) width = 2;
  TXBuffer += formatToHex(color, F("<path fill=\"#"));
  TXBuffer += F("\" d=\"M");
  TXBuffer += toString(SVG_BAR_WIDTH * xoffset / range, 2);
  TXBuffer += ' ';
  TXBuffer += yoffset;
  TXBuffer += 'h';
  TXBuffer += toString(width, 2);
  TXBuffer += 'v';
  TXBuffer += height;
  TXBuffer += 'H';
  TXBuffer += toString(SVG_BAR_WIDTH * xoffset / range, 2);
  TXBuffer += F("z\"/>\n");
}

void createSvgTextElement(const String& text, float textXoffset, float textYoffset) {
  TXBuffer += F("<text style=\"line-height:1.25\" x=\"");
  TXBuffer += toString(textXoffset, 2);
  TXBuffer += F("\" y=\"");
  TXBuffer += toString(textYoffset, 2);
  TXBuffer += F("\" stroke-width=\".3\" font-family=\"sans-serif\" font-size=\"8\" letter-spacing=\"0\" word-spacing=\"0\">\n");
  TXBuffer += F("<tspan x=\"");
  TXBuffer += toString(textXoffset, 2);
  TXBuffer += F("\" y=\"");
  TXBuffer += toString(textYoffset, 2);
  TXBuffer += "\">";
  TXBuffer += text;
  TXBuffer += F("</tspan>\n</text>");
}

unsigned int getSettingsTypeColor(SettingsType settingsType) {
  switch (settingsType) {
    case BasicSettings_Type:
      return 0x5F0A87;
    case TaskSettings_Type:
      return 0xEE6352;
    case CustomTaskSettings_Type:
      return 0x59CD90;
    case ControllerSettings_Type:
      return 0x3FA7D6;
    case CustomControllerSettings_Type:
      return 0xFAC05E;
    case NotificationSettings_Type:
      return 0xF79D84;
    default:
      break;
  }
  return 0;
}

#define SVG_BAR_HEIGHT 16
#define SVG_BAR_WIDTH 400

void write_SVG_image_header(int width, int height) {
  write_SVG_image_header(width, height, false);
}

void write_SVG_image_header(int width, int height, bool useViewbox) {
  TXBuffer += F("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"");
  TXBuffer += width;
  TXBuffer += F("\" height=\"");
  TXBuffer += height;
  TXBuffer += F("\" version=\"1.1\"");
  if (useViewbox) {
    TXBuffer += F(" viewBox=\"0 0 100 100\"");
  }
  TXBuffer += '>';
}

/*
void getESPeasyLogo(int width_pixels) {
  write_SVG_image_header(width_pixels, width_pixels, true);
  TXBuffer += F("<g transform=\"translate(-33.686 -7.8142)\">");
  TXBuffer += F("<rect x=\"49\" y=\"23.1\" width=\"69.3\" height=\"69.3\" fill=\"#2c72da\" stroke=\"#2c72da\" stroke-linecap=\"round\" stroke-linejoin=\"round\" stroke-width=\"30.7\"/>");
  TXBuffer += F("<g transform=\"matrix(3.3092 0 0 3.3092 -77.788 -248.96)\">");
  TXBuffer += F("<path d=\"m37.4 89 7.5-7.5M37.4 96.5l15-15M37.4 96.5l15-15M37.4 104l22.5-22.5M44.9 104l15-15\" fill=\"none\" stroke=\"#fff\" stroke-linecap=\"round\" stroke-width=\"2.6\"/>");
  TXBuffer += F("<circle cx=\"58\" cy=\"102.1\" r=\"3\" fill=\"#fff\"/>");
  TXBuffer += F("</g></g></svg>");
}
*/

#ifndef BUILD_MINIMAL_OTA
void getConfig_dat_file_layout() {
  const int shiftY = 2;
  float yOffset = shiftY;
  write_SVG_image_header(SVG_BAR_WIDTH + 250, SVG_BAR_HEIGHT + shiftY);

  int max_index, offset, max_size;
  int struct_size = 0;

  // background
  const uint32_t realSize = getFileSize(TaskSettings_Type);
  createSvgRectPath(0xcdcdcd, 0, yOffset, realSize, SVG_BAR_HEIGHT - 2, realSize, SVG_BAR_WIDTH);

  for (int st = 0; st < SettingsType_MAX; ++st) {
    SettingsType settingsType = static_cast<SettingsType>(st);
    if (settingsType != NotificationSettings_Type) {
      unsigned int color = getSettingsTypeColor(settingsType);
      getSettingsParameters(settingsType, 0, max_index, offset, max_size, struct_size);
      for (int i = 0; i < max_index; ++i) {
        getSettingsParameters(settingsType, i, offset, max_size);
        // Struct position
        createSvgRectPath(color, offset, yOffset, max_size, SVG_BAR_HEIGHT - 2, realSize, SVG_BAR_WIDTH);
      }
    }
  }
  // Text labels
  float textXoffset = SVG_BAR_WIDTH + 2;
  float textYoffset = yOffset + 0.9 * SVG_BAR_HEIGHT;
  createSvgTextElement(F("Config.dat"), textXoffset, textYoffset);
  TXBuffer += F("</svg>\n");
}

void getStorageTableSVG(SettingsType settingsType) {
  uint32_t realSize = getFileSize(settingsType);
  unsigned int color = getSettingsTypeColor(settingsType);
  const int shiftY = 2;

  int max_index, offset, max_size;
  int struct_size = 0;
  getSettingsParameters(settingsType, 0, max_index, offset, max_size, struct_size);
  if (max_index == 0) return;
  // One more to add bar indicating struct size vs. reserved space.
  write_SVG_image_header(SVG_BAR_WIDTH + 250, (max_index + 1) * SVG_BAR_HEIGHT + shiftY);
  float yOffset = shiftY;
  for (int i = 0; i < max_index; ++i) {
    getSettingsParameters(settingsType, i, offset, max_size);
    // background
    createSvgRectPath(0xcdcdcd, 0, yOffset, realSize, SVG_BAR_HEIGHT - 2, realSize, SVG_BAR_WIDTH);
    // Struct position
    createSvgRectPath(color, offset, yOffset, max_size, SVG_BAR_HEIGHT - 2, realSize, SVG_BAR_WIDTH);
    // Text labels
    float textXoffset = SVG_BAR_WIDTH + 2;
    float textYoffset = yOffset + 0.9 * SVG_BAR_HEIGHT;
    createSvgTextElement(formatHumanReadable(offset, 1024), textXoffset, textYoffset);
    textXoffset = SVG_BAR_WIDTH + 60;
    createSvgTextElement(formatHumanReadable(max_size, 1024), textXoffset, textYoffset);
    textXoffset = SVG_BAR_WIDTH + 130;
    createSvgTextElement(String(i), textXoffset, textYoffset);
    yOffset += SVG_BAR_HEIGHT;
  }
  // usage
  createSvgRectPath(0xcdcdcd, 0, yOffset, max_size, SVG_BAR_HEIGHT - 2, max_size, SVG_BAR_WIDTH);
  // Struct size (used part of the reserved space)
  if (struct_size != 0) {
    createSvgRectPath(color, 0, yOffset, struct_size, SVG_BAR_HEIGHT - 2, max_size, SVG_BAR_WIDTH);
  }
  // Text labels
  float textXoffset = SVG_BAR_WIDTH + 2;
  float textYoffset = yOffset + 0.9 * SVG_BAR_HEIGHT;
  if (struct_size != 0) {
    String text = formatHumanReadable(struct_size, 1024);
    text += '/';
    text += formatHumanReadable(max_size, 1024);
    text += F(" per item");
    createSvgTextElement(text, textXoffset, textYoffset);
  } else {
    createSvgTextElement(F("Variable size"), textXoffset, textYoffset);
  }
  TXBuffer += F("</svg>\n");
}
#endif

#ifdef ESP32


int getPartionCount(byte pType) {
  esp_partition_type_t partitionType = static_cast<esp_partition_type_t>(pType);
  esp_partition_iterator_t _mypartiterator = esp_partition_find(partitionType, ESP_PARTITION_SUBTYPE_ANY, NULL);
  int nrPartitions = 0;
  if (_mypartiterator) {
    do {
      ++nrPartitions;
    } while ((_mypartiterator = esp_partition_next(_mypartiterator)) != NULL);
  }
  esp_partition_iterator_release(_mypartiterator);
  return nrPartitions;
}

void getPartitionTableSVG(byte pType, unsigned int partitionColor) {
  int nrPartitions = getPartionCount(pType);
  if (nrPartitions == 0) return;
  const int shiftY = 2;

  uint32_t realSize = getFlashRealSizeInBytes();
  esp_partition_type_t partitionType = static_cast<esp_partition_type_t>(pType);
  const esp_partition_t * _mypart;
  esp_partition_iterator_t _mypartiterator = esp_partition_find(partitionType, ESP_PARTITION_SUBTYPE_ANY, NULL);
  write_SVG_image_header(SVG_BAR_WIDTH + 250, nrPartitions * SVG_BAR_HEIGHT + shiftY);
  float yOffset = shiftY;
  if (_mypartiterator) {
    do {
      _mypart = esp_partition_get(_mypartiterator);
      createSvgRectPath(0xcdcdcd, 0, yOffset, realSize, SVG_BAR_HEIGHT - 2, realSize, SVG_BAR_WIDTH);
      createSvgRectPath(partitionColor, _mypart->address, yOffset, _mypart->size, SVG_BAR_HEIGHT - 2, realSize, SVG_BAR_WIDTH);
      float textXoffset = SVG_BAR_WIDTH + 2;
      float textYoffset = yOffset + 0.9 * SVG_BAR_HEIGHT;
      createSvgTextElement(formatHumanReadable(_mypart->size, 1024), textXoffset, textYoffset);
      textXoffset = SVG_BAR_WIDTH + 60;
      createSvgTextElement(_mypart->label, textXoffset, textYoffset);
      textXoffset = SVG_BAR_WIDTH + 130;
      createSvgTextElement(getPartitionType(_mypart->type, _mypart->subtype), textXoffset, textYoffset);
      yOffset += SVG_BAR_HEIGHT;
    } while ((_mypartiterator = esp_partition_next(_mypartiterator)) != NULL);
  }
  TXBuffer += F("</svg>\n");
  esp_partition_iterator_release(_mypartiterator);
}

#endif
