#ifndef PLUGINSTRUCTS_P053_DATA_STRUCT_H
#define PLUGINSTRUCTS_P053_DATA_STRUCT_H

#include "../../_Plugin_Helper.h"

#ifdef USES_P053

#define P053_LOW_LEVEL_DEBUG


# include <ESPeasySerial.h>

// Can be unset for memory-tight  builds to remove support for the PMSx003ST and PMS2003/PMS3003 sensor models
# define PLUGIN_053_ENABLE_EXTRA_SENSORS

// #define PLUGIN_053_ENABLE_S_AND_T // Enable setting to support S and T types, in addition to bas PMSx003 and PMSx003ST

# if defined(SIZE_1M) && defined(PLUGIN_BUILD_MINIMAL_OTA) && defined(PLUGIN_053_ENABLE_EXTRA_SENSORS) // Turn off for 1M OTA builds
#  undef PLUGIN_053_ENABLE_EXTRA_SENSORS
# endif // if defined(SIZE_1M) && defined(PLUGIN_BUILD_MINIMAL_OTA) && defined(PLUGIN_053_ENABLE_EXTRA_SENSORS)


# define PMSx003_SIG1 0x42
# define PMSx003_SIG2 0x4d

// Packet sizes
# define PMS1003_5003_7003_SIZE 32
# define PMS5003_S_SIZE         32
# define PMS5003_T_SIZE         32
# define PMS5003_ST_SIZE        40
# define PMS2003_3003_SIZE      24

// Do not change values, as they are being stored
enum class PMSx003_type {
  PMS1003_5003_7003 = 0, // PMSx003 = PMS1003 / PMS5003 / PMS7003
  # ifdef PLUGIN_053_ENABLE_EXTRA_SENSORS
  PMS2003_3003 = 1,      // PMS2003/PMS3003
  PMS5003_S    = 2,      // PMS5003S // Not supported yet
  PMS5003_T    = 3,      // PMS5003T // Not supported yet
  PMS5003_ST   = 4       // PMS5003ST
  # endif // ifdef PLUGIN_053_ENABLE_EXTRA_SENSORS
};

const __FlashStringHelper* toString(PMSx003_type sensorType);


# define PLUGIN_053_OUTPUT_PART 0     // Particles pm1.0/pm2.5/pm10
# define PLUGIN_053_OUTPUT_THC  1     // pm2.5/Temp/Hum/HCHO
# define PLUGIN_053_OUTPUT_CNT  2     // cnt1.0/cnt2.5/cnt10

# define PLUGIN_053_EVENT_NONE      0 // Events: None
# define PLUGIN_053_EVENT_PARTICLES 1 // Particles/temp/humi/hcho
# define PLUGIN_053_EVENT_PARTCOUNT 2 // also Particle count

# define PLUGIN_053_SENSOR_MODEL_SELECTOR PCONFIG(0)
# define PLUGIN_053_OUTPUT_SELECTOR       PCONFIG(1)
# define PLUGIN_053_EVENT_OUT_SELECTOR    PCONFIG(2)

// Helper define to make code a bit more readable.
# define GET_PLUGIN_053_SENSOR_MODEL_SELECTOR static_cast<PMSx003_type>(PLUGIN_053_SENSOR_MODEL_SELECTOR)


struct P053_data_struct : public PluginTaskData_base {
public:

  P053_data_struct(int8_t                  rxPin,
                   int8_t                  txPin,
                   const ESPEasySerialPort port,
                   int8_t                  resetPin,
                   PMSx003_type            sensortype);

  P053_data_struct() = delete;

  ~P053_data_struct();

  bool initialized() const;

private:

  void    SerialRead16(uint16_t &value,
                       uint16_t *checksum);

  void    SerialFlush();

  uint8_t packetSize() const;

public:

  bool packetAvailable();

private:

  void sendEvent(const String             & baseEvent,
                 const __FlashStringHelper *name,
                 float                      value);

public:

  bool processData(struct EventStruct *event);

  bool checkAndClearValuesReceived();

private:

  ESPeasySerial     *easySerial            = nullptr;
  bool               values_received       = false;
  const PMSx003_type Plugin_053_sensortype;
};


#endif // ifdef USES_P053
#endif // ifndef PLUGINSTRUCTS_P053_DATA_STRUCT_H
