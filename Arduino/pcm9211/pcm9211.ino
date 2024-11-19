// pcm9211
// Basic test of PCM9211 ADC / digital audio transceiver chip.
// dan.ellis@gmail.com 2024-11-15

#include "Wire.h"

// Amychip main I2C config.
static int I2C_DA_PIN = 18;
static int I2C_CK_PIN = 17;


static int PCM9211_ADDRESS = 0x40;  // strap selectable 0x40..0x43

class PCM9211 {
  public:
    bool begin(TwoWire *wireInstance = &Wire) {
      i2c = wireInstance;
    }
    int read_register(int reg) {
      Wire.beginTransmission(PCM9211_ADDRESS);
	    Wire.write(reg);
     	Wire.endTransmission();

	    Wire.requestFrom(PCM9211_ADDRESS, 1);
     	return Wire.read();
    }
    void write_register(int reg, int value) {
      Wire.beginTransmission(PCM9211_ADDRESS);
	    Wire.write(reg);
	    Wire.write(value);
	    Wire.endTransmission();
    }
  protected:
    TwoWire *i2c = NULL;
};

void init_pcm9211(PCM9211 &pcm9211) {
// From https://e2e.ti.com/support/audio-group/audio/f/audio-forum/1370260/pcm9211-pll-not-getting-a-lock-with-audio-input

// Here is a script that you can use and get the system initialize and get it  to work.


// #**************************************
// #this script is for SPDIF-->RXIN0-->DIR-->MainOutput, Record sound from SPDIF to PC through TAS1020

// #So
// #1, Chose RXIN0 to DIR
// #2, Active DIR
// #3, chose DIR output as Mainoutput's source.

// #Also HW modification
// #1, Flying to High Level(3.3V) to make sure U7's output is Hi-Z
// #or 2, TAS1020 output logic high on P1.2 I2S enable signal. 
// #**************************************


// #System RST Control
// #w 80 40 00
// w 80 40 33
pcm9211.write_register(0x40, 0x33);  // Power down ADC, power down DIR, power down DIT, power down OSC
// w 80 40 C0
pcm9211.write_register(0x40, 0xC0);  // Normal operation for all

// #XTI Source, Clock (SCK/BCK/LRCK) Frequency Setting
// # XTI 24.5760 for SCLK 12.288 and BCK 3.072, LRCK 48k = XTI/512
// w 80 31 1A
pcm9211.write_register(0x31, 0x1a);
// w 80 33 22
pcm9211.write_register(0x33, 0x22);
// w 80 20 00
pcm9211.write_register(0x20, 0x00);
// w 80 24 00
pcm9211.write_register(0x24, 0x00);
// #ADC clock source is chosen by REG42
// w 80 26 81
pcm9211.write_register(0x26, 0x81);

// #XTI Source, Secondary Bit/LR Clock (SBCK/SLRCK) Frequency Setting
// w 80 33 22
pcm9211.write_register(0x33, 0x22);

// #*********************************************************
// #-------------------------------Start DIR settings---------------------------------------
// #REG. 21h, DIR Receivable Incoming Biphase's Sampling Frequency Range Setting
// w 80 21 00
pcm9211.write_register(0x21, 0x10);   // 0x10 = normal, 28-108 kHz

// #REG. 22h, DIR CLKSTP and VOUT delay
// w 80 22 01
pcm9211.write_register(0x22, 0x01);

// #REG. 23h, DIR OCS start up wait time and Process for Parity Error Detection and ERROR Release Wait Time Setting
// w 80 23 04
pcm9211.write_register(0x23, 0x04);

// # REG 27h DIR Acceptable fs Range Setting & Mask
// w 80 27 00
//pcm9211.write_register(0x27, 0xd7);  // 0xd7: Limit to 32, 44, 48 +/- 2%
pcm9211.write_register(0x27, 0x00);  // 0xd7: Limit to 32, 44, 48 +/- 2%

// # REG 2Fh, DIR Output Data Format, 24bit I2S mode
// w 80 2F 04
pcm9211.write_register(0x2f, 0x04);

// # REG. 30h, DIR Recovered System Clock (SCK) Ratio Setting
// w 80 30 02
pcm9211.write_register(0x30, 0x02);

// #REG. 32h, DIR Source, Secondary Bit/LR Clock (SBCK/SLRCK) Frequency Setting
// w 80 32 22
pcm9211.write_register(0x32, 0x22);

// #REG 34h DIR Input Biphase Signal Source Select and RXIN01 Coaxial Amplifier
// #--PWR down amplifier, Select RXIN2
// #w 80 34 C2
// #--PWR up amplifier, select RXIN0
// w 80 34 00
//pcm9211.write_register(0x34, 0x00);
// #--PWR up amplifier, select RXIN1
// #w 80 34 01
pcm9211.write_register(0x34, 0x01);

// #REG. 37h, Port Sampling Frequency Calculator Measurement Target Setting, Cal and DIR Fs
// w 80 37 00
pcm9211.write_register(0x37, 0x00);
// #REG 38h rd DIR Fs
// r 80 38 01
pcm9211.write_register(0x38, 0x01);
// #***********************************************************
// #------------------------------------ End DIR settings------------------------------------------


// #***********************************************************
// #---------------------------------Start  MainOutput Settings--------------------------------------
// #MainOutput
// #REG. 6Ah, Main Output & AUXOUT Port Control
// w 80 6A 00
pcm9211.write_register(0x6a, 0x00);

// #REG. 6Bh, Main Output Port (SCKO/BCK/LRCK/DOUT) Source Setting
// w 80 6B 11
//pcm9211.write_register(0x6b, 0x11);  // 0x11 = locked to DIR (0x00 for ADC/DIR auto)
pcm9211.write_register(0x6b, 0x00);  // 0x11 = locked to DIR (0x00 for ADC/DIR auto)

// #REG. 6Dh, MPIO_B & Main Output Port Hi-Z Control
// w 80 6D 00
pcm9211.write_register(0x6d, 0x00);
// #***********************************************************
// #------------------------------------ End MainOutput settings------------------------------------------

// # read back all registers to ensure GUI integrity
// r 80 20 5E
}

// #************** WM8960 setup *****************************

extern "C" {
#include "wm8960.h"

static int WM8690_ADDRESS = 0x1a;

void i2c_master_write_wm8960(uint8_t *data_wr, size_t size_wr) {
  // Register is already encoded into data_wr[0]
  Wire.beginTransmission(WM8690_ADDRESS);
  for (int i = 0; i < size_wr; ++i) {
    Wire.write(data_wr[i]);
  }
  Wire.endTransmission();
}
}

// #***********************************************************


class PCM9211 pcm9211;

void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_DA_PIN, I2C_CK_PIN);
  //pcm9211.begin(&Wire);
  delay(1000);
  Serial.println("Scanning for I2C devices ...");
  int address = 0x40;
  Wire.beginTransmission(address);
  int error = Wire.endTransmission();
  if (error == 0){
    Serial.printf("I2C device found at address 0x%02X\n", address);
  } else if(error != 2){
    Serial.printf("Error %d at address 0x%02X\n", error, address);
  }

  // Default DAC setup
  setup_wm8960_i2s();

  // Configure ADC for 16 bit output
  //pcm9211.write_register(0x48, 0x00);  // ADFMT1:0 = 00 24 bit i2s
  //pcm9211.write_register(0x48, 0x03);  // ADFMT1:0 = 11 16 bit right-justified
  init_pcm9211(pcm9211);
}

void loop() {
  delay(5000);

  Serial.println("Reading 9211 ...");
  // Serial.print("reg 0x38 fscalc rslt=");
  // Serial.println(pcm9211.read_register(0x38), 16);
  // Serial.print("reg 0x39 biphsf rslt=");
  // Serial.println(pcm9211.read_register(0x39), 16);
  Serial.print("reg 0x25 errcause rslt=");
  //Serial.println(pcm9211.read_register(0x25), 16);
}
