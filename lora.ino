#include <SPI.h>
#include <string.h>

// RFM98 registers
#define REG_FIFO                    0x00
#define REG_OPMODE                  0x01
#define REG_FIFO_ADDR_PTR           0x0D 
#define REG_FIFO_TX_BASE_AD         0x0E
#define REG_FIFO_RX_BASE_AD         0x0F
#define REG_FIFO_RX_CURRENT_ADDR    0x10
#define REG_IRQ_FLAGS_MASK          0x11
#define REG_IRQ_FLAGS               0x12
#define REG_RX_NB_BYTES             0x13
#define REG_MODEM_CONFIG            0x1D
#define REG_MODEM_CONFIG2           0x1E
#define REG_MODEM_CONFIG3           0x26
#define REG_PREAMBLE_MSB            0x20
#define REG_PREAMBLE_LSB            0x21
#define REG_PAYLOAD_LENGTH          0x22
#define REG_HOP_PERIOD              0x24
#define REG_FREQ_ERROR              0x28
#define REG_DETECT_OPT              0x31
#define  REG_DETECTION_THRESHOLD     0x37
#define REG_DIO_MAPPING_1           0x40
#define REG_DIO_MAPPING_2           0x41

// FSK stuff
#define REG_PREAMBLE_MSB_FSK        0x25
#define REG_PREAMBLE_LSB_FSK        0x26
#define REG_PACKET_CONFIG1          0x30
#define REG_PAYLOAD_LENGTH_FSK      0x32
#define REG_FIFO_THRESH             0x35
#define REG_FDEV_MSB                0x04
#define REG_FDEV_LSB                0x05
#define REG_FRF_MSB                 0x06
#define REG_FRF_MID                 0x07
#define REG_FRF_LSB                 0x08
#define REG_BITRATE_MSB             0x02
#define REG_BITRATE_LSB             0x03
#define REG_IRQ_FLAGS2              0x3F

// MODES
#define RF98_MODE_RX_CONTINUOUS     0x85
#define RF98_MODE_TX                0x83
#define RF98_MODE_SLEEP             0x80
#define RF98_MODE_STANDBY           0x81

// Modem Config 1
#define EXPLICIT_MODE               0x00
#define IMPLICIT_MODE               0x01

#define ERROR_CODING_4_5            0x02
#define ERROR_CODING_4_6            0x04
#define ERROR_CODING_4_7            0x06
#define ERROR_CODING_4_8            0x08

#define BANDWIDTH_7K8               0x00
#define BANDWIDTH_10K4              0x10
#define BANDWIDTH_15K6              0x20
#define BANDWIDTH_20K8              0x30
#define BANDWIDTH_31K25             0x40
#define BANDWIDTH_41K7              0x50
#define BANDWIDTH_62K5              0x60
#define BANDWIDTH_125K              0x70
#define BANDWIDTH_250K              0x80
#define BANDWIDTH_500K              0x90

// Modem Config 2

#define SPREADING_6                 0x60
#define SPREADING_7                 0x70
#define SPREADING_8                 0x80
#define SPREADING_9                 0x90
#define SPREADING_10                0xA0
#define SPREADING_11                0xB0
#define SPREADING_12                0xC0

#define CRC_OFF                     0x00
#define CRC_ON                      0x04


// POWER AMPLIFIER CONFIG
#define REG_PA_CONFIG               0x09
#define PA_MAX_BOOST                0x8F    // 100mW (max 869.4 - 869.65)
#define PA_LOW_BOOST                0x81
#define PA_MED_BOOST                0x8A
#define PA_MAX_UK                   0x88    // 10mW (max 434)
#define PA_OFF_BOOST                0x00
#define RFO_MIN                     0x00

// 20DBm
#define REG_PA_DAC                  0x4D
#define PA_DAC_20                   0x87

// LOW NOISE AMPLIFIER
#define REG_LNA                     0x0C
#define LNA_MAX_GAIN                0x23  // 0010 0011
#define LNA_OFF_GAIN                0x00

typedef enum {lmIdle, lmListening, lmSending} tLoRaMode;

tLoRaMode LoRaMode;
byte currentMode = 0x81;
int TargetID;
int GroundCount;
int AirCount;
int BadCRCCount;
unsigned char Telemetry[SENTENCE_LENGTH];
unsigned long LastLoRaTX=0;
unsigned long TimeToSendIfNoGPS=0;

void LoRaDefaults(void)
{
  Settings.ImplicitOrExplicit = EXPLICIT_MODE;
  Settings.ErrorCoding = ERROR_CODING_4_8;
  Settings.Bandwidth = BANDWIDTH_20K8;
  Settings.SpreadingFactor = SPREADING_11;
  Settings.LowDataRateOptimize = 0x08;
}

void SetupLoRa(void)
{
  // initialize the pins
  pinMode(LORA_NSS, OUTPUT);
  pinMode(LORA_DIO0, INPUT);

  SPI.begin();
  
  SwitchToLoRaMode();
}

void SwitchToLoRaMode(void)
{
  int PayloadLength;
  
  // LoRa mode 
  // setMode(RF98_MODE_SLEEP);
  writeRegister(REG_OPMODE,0x80);

  // Frequency
  SetLoRaFrequency();

  PayloadLength = (Settings.ImplicitOrExplicit == IMPLICIT_MODE) ? 255 : 0;

  writeRegister(REG_MODEM_CONFIG, Settings.ImplicitOrExplicit | Settings.ErrorCoding | Settings.Bandwidth);
  writeRegister(REG_MODEM_CONFIG2, Settings.SpreadingFactor | CRC_ON);
  writeRegister(REG_MODEM_CONFIG3, 0x04 | Settings.LowDataRateOptimize);									// 0x04: AGC sets LNA gain
  
  writeRegister(REG_DETECT_OPT, (readRegister(REG_DETECT_OPT) & 0xF8) | ((Settings.SpreadingFactor == SPREADING_6) ? 0x05 : 0x03));  // 0x05 For SF6; 0x03 otherwise
  
  writeRegister(REG_DETECTION_THRESHOLD, (Settings.SpreadingFactor == SPREADING_6) ? 0x0C : 0x0A);		// 0x0C for SF6, 0x0A otherwise  
  
  writeRegister(REG_PAYLOAD_LENGTH, PayloadLength);
  writeRegister(REG_RX_NB_BYTES, PayloadLength);
  
  // Change the DIO mapping to 01 so we can listen for TxDone on the interrupt
  writeRegister(REG_DIO_MAPPING_1,0x40);
  writeRegister(REG_DIO_MAPPING_2,0x00);
  
  // Go to standby mode
  setMode(RF98_MODE_STANDBY);
}

void SetLoRaFrequency(void)
{
  unsigned long FrequencyValue;
  double Frequency;
    
  // Serial.print("Frequency is ");
  // Serial.println(Frequency);

  Frequency = Settings.LORA_Frequency * 7110656 / 434;
  FrequencyValue = (unsigned long)(Frequency);

  // Serial.print("FrequencyValue is ");
  // Serial.println(FrequencyValue);

  writeRegister(0x06, (FrequencyValue >> 16) & 0xFF);    // Set frequency
  writeRegister(0x07, (FrequencyValue >> 8) & 0xFF);
  writeRegister(0x08, FrequencyValue & 0xFF);
}


/////////////////////////////////////
//    Method:   Change the mode
//////////////////////////////////////
void setMode(byte newMode)
{
  if(newMode == currentMode)
    return;  
  
  switch (newMode) 
  {
    case RF98_MODE_TX:
      writeRegister(REG_LNA, LNA_OFF_GAIN);  // TURN LNA OFF FOR TRANSMIT
      writeRegister(REG_PA_CONFIG, PA_MAX_UK);
      writeRegister(REG_OPMODE, newMode);
      currentMode = newMode; 
      break;
      
    case RF98_MODE_RX_CONTINUOUS:
      writeRegister(REG_PA_CONFIG, PA_OFF_BOOST);   // TURN PA OFF FOR RECIEVE??
      writeRegister(REG_LNA, LNA_MAX_GAIN);         // MAX GAIN FOR RECEIVE
      writeRegister(REG_OPMODE, newMode);
      currentMode = newMode; 
      break;
      
    case RF98_MODE_SLEEP:
      writeRegister(REG_OPMODE, newMode);
      currentMode = newMode; 
      break;
      
    case RF98_MODE_STANDBY:
      writeRegister(REG_OPMODE, newMode);
      currentMode = newMode; 
      break;
      
    default: return;
  } 
  
  if (newMode != RF98_MODE_SLEEP)
  {
    delay(10);
  }
}


/////////////////////////////////////
//    Method:   Read Register
//////////////////////////////////////

byte readRegister(byte addr)
{
  select();
  SPI.transfer(addr & 0x7F);
  byte regval = SPI.transfer(0);
  unselect();
  return regval;
}

/////////////////////////////////////
//    Method:   Write Register
//////////////////////////////////////

void writeRegister(byte addr, byte value)
{
  select();
  SPI.transfer(addr | 0x80); // OR address with 10000000 to indicate write enable;
  SPI.transfer(value);
  unselect();
}

/////////////////////////////////////
//    Method:   Select Transceiver
//////////////////////////////////////
void select() 
{
  digitalWrite(LORA_NSS, LOW);
}

/////////////////////////////////////
//    Method:   UNSelect Transceiver
//////////////////////////////////////
void unselect() 
{
  digitalWrite(LORA_NSS, HIGH);
}

/*
/void CheckLoRaRx(void)
{
  if (LoRaMode == lmListening)
  {
    if (digitalRead(LORA_DIO0))
    {
      unsigned int Bytes;
					
      Bytes = receiveMessage(Telemetry, sizeof(Telemetry));
      // Serial.print("Rx "); Serial.print(Bytes); Serial.println(" bytes");
      
      Bytes = min(Bytes, sizeof(Telemetry));
    }
  }
}
*/

int TimeToSend(void)
{
  int CycleSeconds;
	
  if (Settings.LoRaCycleTime == 0)
  {
    // Not using time to decide when we can send
    return 1;
  }

  if ((millis() > (LastLoRaTX + Settings.LoRaCycleTime*1000+2000)) && (TimeToSendIfNoGPS == 0))
  {
    // Timed out
    // Serial.println("Using Timeout");
    return 1;
  }
  
  if (GPS.Satellites > 0)
  {
    static int LastCycleSeconds=-1;

    // Can't Tx twice at the same time
    CycleSeconds = (GPS.SecondsInDay+Settings.LoRaCycleTime-17) % Settings.LoRaCycleTime;   // Could just use GPS time, but it's nice to see the slot agree with UTC
    
    if (CycleSeconds != LastCycleSeconds)
    {
      LastCycleSeconds = CycleSeconds;
      
      if (CycleSeconds == Settings.LoRaSlot)
      {
        // Serial.println("Using GPS Timing");
        return 1;
      }
    }
  }
  else if ((TimeToSendIfNoGPS > 0) && (millis() >= TimeToSendIfNoGPS))
  {
    // Serial.println("Using LoRa Timing");
    return 1;
  }
    
  return 0;
}


int LoRaIsFree(void)
{
  if ((LoRaMode != lmSending) || digitalRead(LORA_DIO0))
  {
    // Serial.println("LoRaIsFree");
    // Either not sending, or was but now it's sent.  Clear the flag if we need to
    if (LoRaMode == lmSending)
    {
      // Clear that IRQ flag
      writeRegister( REG_IRQ_FLAGS, 0x08); 
      LoRaMode = lmIdle;
    }
				
    // Now we test to see if we're doing TDM or not
    // For TDM, if it's not a slot that we send in, then we should be in listening mode
    // Otherwise, we just send
				
    if (TimeToSend())
    {
      // Either sending continuously, or it's our slot to send in
      // Serial.println("TimeToSend");
					
      return 1;
    }
    
//    if (Settings.LoRaCycleTime > 0)
//    {
//      // TDM system and not time to send, so we can listen
//      if (LoRaMode == lmIdle)
//      {
//        startReceiving();
//      }
//    }
  }
  
  return 0;
}

void SendLoRa(unsigned char *buffer, int Length)
{
  int i;
  
  LastLoRaTX = millis();
  TimeToSendIfNoGPS = 0;

  SwitchToLoRaMode();
  
  setMode(RF98_MODE_STANDBY);

  // writeRegister(REG_DIO_MAPPING_1, 0x40);		// 01 00 00 00 maps DIO0 to TxDone
  writeRegister(REG_FIFO_TX_BASE_AD, 0x00);  // Update the address ptr to the current tx base address
  writeRegister(REG_FIFO_ADDR_PTR, 0x00); 
  if (Settings.ImplicitOrExplicit == EXPLICIT_MODE)
  {
    writeRegister(REG_PAYLOAD_LENGTH, Length);
  }
  select();
  // tell SPI which address you want to write to
  SPI.transfer(REG_FIFO | 0x80);

  // loop over the payload and put it on the buffer 
  for (i = 0; i < Length; i++)
  {
    SPI.transfer(buffer[i]);
  }
  unselect();

  // go into transmit mode
  setMode(RF98_MODE_TX);
  
  LoRaMode = lmSending;
}

//void startReceiving(void)
//{
//  writeRegister(REG_DIO_MAPPING_1, 0x00);		// 00 00 00 00 maps DIO0 to RxDone
//	
//  writeRegister(REG_FIFO_RX_BASE_AD, 0);
//  writeRegister(REG_FIFO_ADDR_PTR, 0);
//	  
//  // Setup Receive Continuous Mode
//  setMode(RF98_MODE_RX_CONTINUOUS); 
//		
//  LoRaMode = lmListening;
//}

//int receiveMessage(unsigned char *message, int MaxLength)
//{
//  int i, Bytes, currentAddr, x;
//
//  Bytes = 0;
//	
//  x = readRegister(REG_IRQ_FLAGS);
//  
//  // clear the rxDone flag
//  writeRegister(REG_IRQ_FLAGS, 0x40); 
//    
//  // check for payload crc issues (0x20 is the bit we are looking for
//  if((x & 0x20) == 0x20)
//  {
//    // CRC Error
//    writeRegister(REG_IRQ_FLAGS, 0x20);		// reset the crc flags
//    BadCRCCount++;
//  }
//  else
//  {
//    currentAddr = readRegister(REG_FIFO_RX_CURRENT_ADDR);
//    Bytes = readRegister(REG_RX_NB_BYTES);
//    Bytes = min(Bytes, MaxLength-1);
//
//    writeRegister(REG_FIFO_ADDR_PTR, currentAddr);   
//		
//    for(i = 0; i < Bytes; i++)
//    {
//      message[i] = (unsigned char)readRegister(REG_FIFO);
//    }
//    message[Bytes] = '\0';
//
//    // Clear all flags
//    writeRegister(REG_IRQ_FLAGS, 0xFF); 
//  }
//  
//  return Bytes;
//}

int FSKPacketSent(void)
{
  return ((readRegister(REG_IRQ_FLAGS2) & 0x48) != 0);
}

int FSKBufferLow(void)
{
  return (readRegister(REG_IRQ_FLAGS2) & 0x20) == 0;
}


void CheckLoRa(void)
{
  static unsigned int ImageCount=0;
  
  if (LoRaIsFree())
  {
    int PacketLength;

    // SSDV or Telemetry?
    if (++ImageCount > Settings.ImageCount)
    {
      ImageCount = 0;
    }
    else if (SSDVBufferLength < 256)
    {
      ImageCount = 0;
    }

    if (ImageCount == 0)
    {
      // Telemetry
      PacketLength = BuildSentence((char *)Telemetry);
   
      SendLoRa(Telemetry, PacketLength);    
      
      if (ShowLoRa)
      {
        Serial.print(F("LORA="));
        Serial.print((char *)Telemetry);
        Serial.print('\r');
      }
    }
    else
    {
      // SSDV packet
      SendLoRa(SSDVBuffer+1, 255);    
      
      if (ShowLoRa)
      {
        Serial.println(F("LORA=SSDV"));
      }

      SSDVBufferLength = 0;
      Serial.print(F("SSDV="));
      Serial.println(SSDVBufferLength);
    }
  }
}
