
/*------------------------------------------------------------------------------------------------------*\
|                                                                                                        |
| PITS V3 Firmware                                                                                       |
|                                                                                                        |
\*------------------------------------------------------------------------------------------------------*/

#include <EEPROM.h>
#include <SoftwareSerial.h>
#include <avr/pgmspace.h>

#define   VERSION   F("V1.00")

//------------------------------------------------------------------------------------------------------

  #define LED_OK                 6
  #define LED_WARN               7
  #define LORA_NSS              10
  #define LORA_DIO0              5
  #define A0_MULTIPLIER      12.92
  #define WIREBUS                4
  #define APRS_ENABLE            9
  #define APRS_PTT               8
  #define APRS_DATA              3
  #define APRS_TX               A1   // 15
  #define APRS_RX               A2   // 16 

//------------------------------------------------------------------------------------------------------

#define SENTENCE_LENGTH       100                  // This is more than sufficient for the standard sentence.  Extend if needed; shorten if you are tight on memory.
#define PAYLOAD_LENGTH         16
#define FIELDLIST_LENGTH       16
#define COMMAND_BUFFER_LENGTH  70

//------------------------------------------------------------------------------------------------------
//
//  Globals

struct TSettings
{
  // Common
  char PayloadID[PAYLOAD_LENGTH];
  char FieldList[FIELDLIST_LENGTH];

  // GPS
  unsigned int FlightModeAltitude;
  
  // LoRa
  float LORA_Frequency;
  unsigned char ImplicitOrExplicit;             // 1=Implicit, 0=Explicit
  unsigned char ErrorCoding;
  unsigned char Bandwidth;
  unsigned char SpreadingFactor;
  unsigned char LowDataRateOptimize;
  unsigned char LoRaCount;
  unsigned int  LoRaCycleTime;
  int           LoRaSlot;

  // SSDV
  unsigned int  LowImageCount;
  unsigned int  HighImageCount;
  unsigned int  High;

  // APRS
  double        APRS_Frequency;
  char          APRS_Callsign[7];               // Max 6 characters
  char          APRS_SSID;
  int           APRS_PathAltitude;
  char          APRS_HighUseWide2;
  int           APRS_TxInterval;
  char          APRS_PreEmphasis;
  char          APRS_Random;
  char          APRS_TelemInterval;

  // DS18B20
  unsigned char DS18B20_Address[8];
} Settings;

struct TGPS
{
  int           Day, Month, Year;
  int           Hours, Minutes, Seconds;
  unsigned long SecondsInDay;					// Time in seconds since midnight
  float         Longitude, Latitude;
  long          Altitude;
  unsigned int  Satellites;
  int           Speed;
  int           Direction;
  byte          FixType;
  byte          psm_status;
  int           Temperatures[2];             // C
  byte          InternalTemperature;        // Index of internal temperature
  int           BatteryVoltage;               // mV
  byte          FlightMode;
  byte          PowerMode;
  float         PredictedLongitude, PredictedLatitude;
  int           UseHostPosition;
} GPS;


int ShowGPS=1;
int ShowLoRa=1;
int HostPriority=0;
unsigned char SSDVBuffer[256];
unsigned int SSDVBufferLength=0;

SoftwareSerial APRS_Serial(APRS_RX, APRS_TX);

//------------------------------------------------------------------------------------------------------

void setup()
{
  // Serial port
  
  Serial.begin(38400);
  Serial.println("");
  Serial.print(F("PITSV3 AVR Firmware "));
  Serial.println(VERSION);
      
  Serial.print(F("Free memory = "));
  Serial.println(freeRam());

  if ((EEPROM.read(0) == 'D') && (EEPROM.read(1) == 'A'))
  {
    // Store current (default) settings
    LoadSettings();
  }
  else
  {
    SetDefaults();
  }

  SetupLEDs();
  
  SetupGPS();
  
  SetupADC();
  
  SetupLoRa();

  Setupds18b20();

  SetupAPRS();
}

void SetDefaults(void)
{
  // Common settings
  strcpy(Settings.PayloadID, "CHANGEME");
  strcpy(Settings.FieldList, "0123456789A");

  // GPS Settings
  Settings.FlightModeAltitude = 2000;

  // LoRa Settings
  Settings.LORA_Frequency = 434.275;
  Settings.LoRaCount = 1;
  Settings.LoRaCycleTime = 0;
  Settings.LoRaSlot = -1;
  LoRaDefaults();

  // SSDV Settings
  Settings.LowImageCount = 4;
  Settings.HighImageCount = 8;
  Settings.High = 2000;

  // APRS Settings
  Settings.APRS_Frequency = 144.8;
  strcpy(Settings.APRS_Callsign, "");
  Settings.APRS_SSID = 0;
  Settings.APRS_PathAltitude = 1500;
  Settings.APRS_HighUseWide2 = 1;
  Settings.APRS_TxInterval = 20;  // 60;
  Settings.APRS_PreEmphasis = 1;
  Settings.APRS_Random = 0; // 30;
  Settings.APRS_TelemInterval = 0;  // 2
  
  Serial.println(F("Placing default settings in EEPROM"));
  
  SaveSettings();
}


void loop()
{  
  CheckHost();

  if (!HostPriority)
  {
    CheckGPS();

    CheckLEDs();

    CheckLoRa();
  
    CheckADC();
  
    Checkds18b20();

    CheckAPRS();
  }
}

void ShowVersion(void)
{
  Serial.print(F("VER="));
  Serial.println(VERSION);
}

int freeRam(void)
{
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}

void LoadSettings(void)
{
  unsigned int i;
  unsigned char *ptr;

  ptr = (unsigned char *)(&Settings);
  for (i=0; i<sizeof(Settings); i++, ptr++)
  {
    *ptr = EEPROM.read(i+2);
  }
}

void SaveSettings(void)
{
  unsigned int i;
  unsigned char *ptr;
  
  // Signature
  EEPROM.write(0, 'D');
  EEPROM.write(1, 'A');

  // Settings
  ptr = (unsigned char *)(&Settings);
  for (i=0; i<sizeof(Settings); i++, ptr++)
  {
    EEPROM.write(i+2, *ptr);
  }
}

void CheckHost(void)
{
  static char Line[COMMAND_BUFFER_LENGTH];
  static unsigned int Length=0;
  static unsigned int BinaryMode=0;
  char Character;

  while (Serial.available())
  { 
    Character = Serial.read();

    if (BinaryMode)
    {
      Line[Length++] = Character;
      if (--BinaryMode == 0)
      {
        ProcessCommand(Line+1);
        Length = 0;
      }
    }  
    else if (Character == '~')
    {
      Line[0] = Character;
      Length = 1;
    }
    else if (Character == '\r')
    {
      Line[Length] = '\0';
      ProcessCommand(Line+1);
      Length = 0;
    }
    else if (Length >= sizeof(Line))
    {
      Length = 0;
    }
    else if (Length > 0)
    {
      Line[Length++] = Character;
      if (Length == 3)
      {
        if ((Line[1] == 'S') && (Line[2] == 'B'))
        {
          BinaryMode = 32;
        }
      }
    }
  }
}

void ProcessCommand(char *Line)
{
  int OK = 0;

  if (Line[0] == 'G')
  {
    OK = ProcessGPSCommand(Line+1);
  }
  else if (Line[0] == 'C')
  {
    OK = ProcessCommonCommand(Line+1);
  }
  else if (Line[0] == 'L')
  {
    OK = ProcessLORACommand(Line+1);
  }
  else if (Line[0] == 'A')
  {
    OK = ProcessAPRSCommand(Line+1);
  }
  else if (Line[0] == 'S')
  {
    OK = ProcessSSDVCommand(Line+1);
  }
  else if (Line[0] == 'F')
  {
    OK = ProcessFieldCommand(Line+1);
  }

  if (OK)
  {
    Serial.println("*");
  }
  else
  {
    Serial.println("?");
  }
}

int ProcessGPSCommand(char *Line)
{
  int OK = 0;
  
  if (Line[0] == 'P')
  {
    ShowGPS = atoi(Line+1);
    OK = 1;
  }
  else if (Line[0] == 'F')
  {
    // Flight mode altitude
    unsigned int Altitude;
    
    Altitude = atoi(Line+1);
    
    if (Altitude < 8000)
    {
      Settings.FlightModeAltitude = Altitude;
      OK = 1;
    }
  }

  return OK;
}

int ProcessCommonCommand(char *Line)
{
  int OK = 0;

  if (Line[0] == 'H')
  {
    // HostPriority mode
    HostPriority = Line[1] == '1';
    OK = 1;
  }
  else if (Line[0] == 'P')
  {
    // Store payload ID
    if (strlen(Line+1) < PAYLOAD_LENGTH)
    {
      strcpy(Settings.PayloadID, Line+1);
      OK = 1;
    }
  }
  else if (Line[0] == 'F')
  {
    // Store Field List
    if (strlen(Line+1) < FIELDLIST_LENGTH)
    {
      strcpy(Settings.FieldList, Line+1);
      OK = 1;
    }
  }
  else if (Line[0] == 'R')
  {
    // Reset to default settings
    SetDefaults();
    OK = 1;
  }
  else if (Line[0] == 'S')
  {
    // Save settings to flash
    SaveSettings();
    OK = 1;
  }
  else if (Line[0] == 'V')
  {
    // Version number
    ShowVersion();
    OK = 1;
  }

  return OK;
}

int ProcessLORACommand(char *Line)
{
  int OK = 0;

  if (Line[0] == 'F')
  {
    // New frequency
    double Frequency;
    
    Frequency = atof(Line+1);
    if ((Frequency >= 400) && (Frequency < 1000))
    {
      Settings.LORA_Frequency = Frequency;
      OK = 1;
    }
  }
  else if (Line[0] == 'S')
  {
    // Spreading Factor
    int SpreadingFactor = atoi(Line+1);
    
    if ((SpreadingFactor >= 6) && (SpreadingFactor <= 12))
    {
      Settings.SpreadingFactor = SpreadingFactor << 4;
      OK = 1;
    }
  }
  else if (Line[0] == 'I')
  {
    int ImplicitOrExplicit = atoi(Line+1);
    
    Settings.ImplicitOrExplicit = ImplicitOrExplicit ? 1 : 0;
    OK = 1;
  }
  else if (Line[0] == 'E')
  {
    // Error Coding
    int ErrorCoding = atoi(Line+1);
    
    if ((ErrorCoding >= 5) && (ErrorCoding <= 8))
    {
      Settings.ErrorCoding = (ErrorCoding - 4) << 1;
      OK = 1;
    }
  }
  else if (Line[0] == 'B')
  {
    // Bandwidth
    int Bandwidth = atoi(Line+1);
    
    if ((Bandwidth >= 0) && (Bandwidth <= 9))
    {
      Settings.Bandwidth = Bandwidth << 4;
      OK = 1;
    }
  }
  else if (Line[0] == 'C')
  {
    // LORA Count
    int Count = atoi(Line+1);
    
    if ((Count >= 0) && (Count < 20))
    {
      Settings.LoRaCount = Count;
      OK = 1;
    }
  }
  else if (Line[0] == 'L')
  {
    int LowDataRateOptimize = atoi(Line+1);
    
    Settings.LowDataRateOptimize = LowDataRateOptimize ? 1 : 0;
    OK = 1;
  }
  else if (Line[0] == 'T')
  {
    int LoRaCycleTime = atoi(Line+1);
    
    if ((LoRaCycleTime >= 0) && (LoRaCycleTime <= 60))
    {
      Settings.LoRaCycleTime = LoRaCycleTime;
      OK = 1;
    }
  }
  else if (Line[0] == 'O')
  {
    int LoRaSlot = atoi(Line+1);
    
    if ((LoRaSlot >= -1) && (LoRaSlot < 60))
    {
      Settings.LoRaSlot = LoRaSlot;
      OK = 1;
    }
  }

  return OK;
}

    
int ProcessAPRSCommand(char *Line)
{
  int OK = 0;

  if (Line[0] == 'P')
  {
    // Store payload ID
    if (strlen(Line+1) <= 6)
    {
      strcpy(Settings.APRS_Callsign, Line+1);
      OK = 1;
    }
  }
  else if (Line[0] == 'F')
  {
    // New frequency
    double Frequency;
    
    Frequency = atof(Line+1);
    if ((Frequency >= 134) && (Frequency <= 174))
    {
      Settings.APRS_Frequency = Frequency;
      // SetAPRSFrequency();
      OK = 1;
    }
  }
  else if (Line[0] == 'S')
  {
    // SSID
    int SSID = atoi(Line+1);
    
    if ((SSID >= 0) && (SSID <= 14))
    {
      Settings.APRS_SSID = SSID;
      OK = 1;
    }
  }
  else if (Line[0] == 'A')
  {
    // Path altitude
    Settings.APRS_PathAltitude = atoi(Line+1);

    OK = 1;
  }
  else if (Line[0] == 'W')
  {
    // Use Wide2
    Settings.APRS_HighUseWide2 = atoi(Line+1);
    OK = 1;
  }
  else if (Line[0] == 'I')
  {
    // Tx Interval in seconds
    Settings.APRS_TxInterval = atoi(Line+1);
    OK = 1;
  }
  else if (Line[0] == 'R')
  {
    // Random period
    Settings.APRS_Random = atoi(Line+1);
    OK = 1;
  }
  else if (Line[0] == 'P')
  {
    Settings.APRS_PreEmphasis = atoi(Line+1);
    // SetAPRSPreEmphasis();
    OK = 1;
  }
  else if (Line[0] == 'T')
  {
    Settings.APRS_TelemInterval = atoi(Line+1);
    OK = 1;
  }

  return OK;
}


int ProcessSSDVCommand(char *Line)
{
  int OK = 0;

  if (Line[0] == 'C')
  {
    // Clear SSDV buffer
    SSDVBufferLength = 0;
    OK = 1;
  }
  else if (Line[0] == 'P')
  {
    // Hex SSDV Packet
    unsigned char Upper=1;
    unsigned char Value;
    
    while (*(++Line))
    {
      if (Upper)
      {
        Value = HexToByte(*Line) << 4;
      }
      else
      {
        Value += HexToByte(*Line);
        SSDVBuffer[SSDVBufferLength++] = Value;
      }
      Upper = 1 - Upper;
    }
    OK = 1;
    Serial.print(F("SSDV="));
    Serial.println(SSDVBufferLength);
  }
  else if (Line[0] == 'B')
  {
    // Binary SSDV Packet
    int i;

    for (i=0; i<32; i++)
    {
      SSDVBuffer[SSDVBufferLength++] = *(++Line);
    }
    OK = 1;
    Serial.print(F("SSDV="));
    Serial.println(SSDVBufferLength);
  }
  else if (Line[0] == 'S')
  {
    // SSDV Status
    Serial.print(F("SSDV="));
    Serial.println(SSDVBufferLength);
    OK = 1;
  }
  else if (Line[0] == 'I')
  {
    // SSDV Image Count
    sscanf(Line+1, "%d,%d,%d", &Settings.LowImageCount, Settings.HighImageCount, &Settings.High);
    // Settings.ImageCount = atoi(Line+1);
    OK = 1;
  }
  
  return OK;
}


int ProcessFieldCommand(char *Line)
{
  int OK = 0;

  if (Line[0] == 'A')
  {
    GPS.PredictedLatitude = atof(Line+1);
    OK = 1;
  }
  else if (Line[0] == 'O')
  {
    GPS.PredictedLongitude = atof(Line+1);
    OK = 1;
  }
  else if (Line[0] == 'T')
  {
    GPS.Latitude = atof(Line+1);
    OK = 1;
  }
  else if (Line[0] == 'G')
  {
    GPS.Longitude = atof(Line+1);
    OK = 1;
  }
  else if (Line[0] == 'U')
  {
    GPS.Altitude = atoi(Line+1);
    GPS.UseHostPosition = 5;
    OK = 1;
  }
  
  return OK;
}

unsigned char HexToByte(char ch)
{
    int num=0;
    
    if(ch>='0' && ch<='9')
    {
        num=ch-0x30;
    }
    else
    {
        switch(ch)
        {
            case 'A': case 'a': num=10; break;
            case 'B': case 'b': num=11; break;
            case 'C': case 'c': num=12; break;
            case 'D': case 'd': num=13; break;
            case 'E': case 'e': num=14; break;
            case 'F': case 'f': num=15; break;
            default: num=0;
        }
    }
    return num;
}
