/*
  The UI of this simple DRO has been derived from an example
  of the TFT_eSPI library. 
*/

/*
  The TFT_eSPI library incorporates an Adafruit_GFX compatible
  button handling class, this sketch is based on the Arduin-o-phone
  example.

  This example diplays a keypad where numbers can be entered and
  send to the Serial Monitor window.

  The sketch has been tested on the ESP8266 (which supports SPIFFS)

  The minimum screen size is 320 x 240 as that is the keypad size.

  TOUCH_CS and SPI_TOUCH_FREQUENCY must be defined in the User_Setup.h file
  for the touch functions to do anything.
*/

// The SPIFFS (FLASH filing system) is used to hold touch screen
// calibration data

#include "FS.h"

#include <SPI.h>
#include <TFT_eSPI.h>      // Hardware-specific library

TFT_eSPI tft = TFT_eSPI(); // Invoke custom library

// This is the file name used to store the calibration data
// You can change this to create new calibration files.
// The SPIFFS file name must start with "/".
#define CALIBRATION_FILE "/TouchCalData2"

// Set REPEAT_CAL to true instead of false to run calibration
// again, otherwise it will only be done once.
// Repeat calibration if you change the screen rotation.
#define REPEAT_CAL false

// code can be used with a caliper or a digital dial indicator
// calipers usually have 2 decimal places for mm and 4 for inch, including the half thousands.
// some dial indicators have 3 decimal places for mm and 5 for inch. Set FINE_RESOLUTION to true for these devices.
#define FINE_RESOLUTION false

// Keypad start position, key sizes and spacing
#define KEY_X 48 // Centre of key
#define KEY_Y 141
#define KEY_W 93 // Width and height
#define KEY_H 45
#define KEY_SPACING_X 18 // X and Y gap
#define KEY_SPACING_Y 30
#define KEY_TEXTSIZE 1.5   // Font size multiplier

// Using two fonts since numbers are nice when bold
#define LABEL1_FONT &FreeSansOblique12pt7b // Key label font 1
#define LABEL2_FONT &FreeSansBold12pt7b    // Key label font 2

// Numeric display box size and location
#define DISP_X 1
#define DISP_Y 1
#define DISP_W 318
#define DISP_H 80
#define DISP_TSIZE 20
#define DISP_TCOLOR INV_WHITE

// We have a status line for messages
#define STATUS_X 160 // Centred on this
#define STATUS_Y 90

// inverted colors, calculated by 0xFFFF - (color code)
// must be done this way for Pico-ResTouch display
#define INV_BLACK 0xFFFF
#define INV_NAVY 0xFFF0
#define INV_DARKGREEN 0xFC1F
#define INV_DARKCYAN 0xFC10
#define INV_MAROON 0x87FF
#define INV_PURPLE 0x87F0
#define INV_OLIVE 0x841F
#define INV_LIGHTGREY 0x2965
#define INV_DARKGREY 0x8410
#define INV_BLUE 0xFFE0
#define INV_GREEN 0xF81F
#define INV_CYAN 0xF800
#define INV_RED 0x07FF
#define INV_MAGENTA 0x07E0
#define INV_YELLOW 0x001F
#define INV_WHITE 0x0000
#define INV_ORANGE 0x025F
#define INV_GREENYELLOW 0x481F
#define INV_PINK 0x01E6
#define INV_BROWN 0x659F
#define INV_GOLD 0x015F
#define INV_SILVER 0x39E7
#define INV_SKYBLUE 0x7982
#define INV_VIOLET 0x6EA3
#define INV_LIGHTBLUE 0xBC22
#define INV_LIGHTGREEN 0x286A
#define INV_TEXTGREY 0x738E
#define INV_LIGHTRED 0x03F1

#define GATE_INTERVAL_MS 20
#define GPIO_CLOCK 26
#define GPIO_DATA 27

// Number length, buffer for storing it and character index
#define NUM_LEN 7
char numberBuffer[NUM_LEN + 1] = "";
uint8_t numberIndex = 0;
uint8_t decimal_places;

volatile int16_t xwidth, xwidth_float, xwidth_status, xwidth_inv;
volatile int16_t xwidth_dia = 10;


// Caliper protocol: 24 Bits with weighting (16 bit value), last bit: inch/mm, bit 20: +/-
unsigned int data[24]={1,2,4,8,16,32,64,128,256,512,1024,2048,4096,8192,16384,32768,0,0,0,1,0,0,0,1};
unsigned int datapulse[24]; // For 24 pulses inside the frame
volatile unsigned int gate, gi; // Gate variables
volatile unsigned int datavalue; // The measured value
volatile float meas_value, meas_value_old, set_value, disp_value; 
float reference_value = 0;
volatile bool NewValueFlag = false;
bool DiameterMode = false;
bool InchFlag = false;
bool SetFlag = false;
bool InvertMode = false;    // InvertMode means that if the reading increaes, the displayed value decreases. 
                            // This is useful e.g. if a dial indicator is mounted in the rear of the lathe

// Create 15 keys for the keypad
char keyLabel[15][5] = {"CRL", "INV", "SET", "1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0", "DIA" };
uint16_t keyColor[15] = {INV_LIGHTBLUE, INV_LIGHTBLUE, INV_LIGHTBLUE,
                         INV_BLUE, INV_BLUE, INV_BLUE,
                         INV_BLUE, INV_BLUE, INV_BLUE,
                         INV_BLUE, INV_BLUE, INV_BLUE,
                         INV_BLUE, INV_BLUE, INV_LIGHTRED
                        };

// Invoke the TFT_eSPI button class and create all the button objects
TFT_eSPI_Button key[15];

int64_t alarm_callback(alarm_id_t id, void *user_data) {
    gate = 0; // close gate to enable fresh start if reading starts in the middle of the pulse train
    gi = 0;
    return 0;
}

void isr_for_clock() {  // interrupt service routine for the clock input. Named this "isr_clock" first, but this resulted in blocking the Pico. 
    if (gi == 0){
        gate = 1;
        add_alarm_in_ms(GATE_INTERVAL_MS, alarm_callback, NULL, false); // pulse train duration is approx. 8 ms
    }

    if (gate == 1) {// Gate is set (start point)
        if (gpio_get(GPIO_DATA) != 0){ // Check if data input if high
            datapulse[gi] = 0; // Set datapulse to 0
        }
        else {
            datapulse[gi] = 1; // Set datapulse to 1
        }

        if (gi <24) {      // Just do this 24 times (for 24 bits, 0-23)
            datavalue = datavalue + (data[gi]*datapulse[gi]); // Sum of all single bits
            gi ++;
        }

        if (gi == 24) { // Reset to 0, no more pulses
            gate = 0;
            gi = 0;

            if (datapulse[23] == 1){    // inch
                meas_value = ((datavalue - 1) / 2)/1000.0;
                if (datapulse[0] == 1) {
                    meas_value = meas_value + 0.0005;
                }
            }
            else {                      // mm
                meas_value = datavalue / 100.0;
            }

            if (FINE_RESOLUTION) { meas_value = meas_value / 10.0;}

            if (datapulse[20] == 1){  // negative value
                meas_value = - meas_value;
            }

            if ((meas_value != meas_value_old) && (NewValueFlag == false)) {
                NewValueFlag = true;
                if (datapulse[23] == 1){    // inch
                    InchFlag = true;
                }
                else{                       // mm
                    InchFlag = false;
                }
                meas_value_old = meas_value;
            }
            datavalue = 0;
        }
    }   
    else { // Gate = 0, no gate connected
        // Maybe "error gate" message,if gate signal is not there ...
    }
}

void setup() {  // do everything except measurement on core 0
    Serial.begin(115200);       // Use serial port
    tft.init();                 // Initialise the TFT screen
    tft.setRotation(2);         // Set the rotation before we calibrate
    touch_calibrate();          // Calibrate the touch screen and retrieve the scaling factors
    tft.fillScreen(INV_BLACK);  // Clear the screen
    tft.fillRect(0, 0, 320, 480, INV_DARKGREY);                 // Draw keypad background
    tft.fillRect(DISP_X, DISP_Y, DISP_W, DISP_H, INV_BLACK);    // Draw number display area 
    tft.drawRect(DISP_X, DISP_Y, DISP_W, DISP_H, INV_WHITE);    // Draw number display frame
    drawKeypad();               // Draw keypad
}

void loop(void) {
    uint16_t t_x = 0, t_y = 0; // To store the touch coordinates 
    bool pressed = tft.getTouch(&t_x, &t_y); // Pressed will be set true is there is a valid touch on the screen

    // Check if any key coordinate boxes contain the touch coordinates
    for (uint8_t b = 0; b < 15; b++) {
        if (pressed && key[b].contains(t_x, t_y)) {
            key[b].press(true);  // tell the button it is pressed
        } 
        else {
            key[b].press(false);  // tell the button it is NOT pressed
        }
    }

    // Check if any key has changed state
    for (uint8_t b = 0; b < 15; b++) {
        if (b < 3) tft.setFreeFont(LABEL1_FONT);
        else tft.setFreeFont(LABEL2_FONT);

        if ((b != 1) && (b != 2)){
            if (key[b].justReleased()) key[b].drawButton();     // draw normal
        }

        if (key[b].justPressed()) {
            key[b].drawButton(true);  // draw inverted

             if (b == 1) {  // Invert button pressed, toggle InvertMode flag
                if (InvertMode == false){
                    InvertMode = true;
                    reference_value = meas_value;
                    NewValueFlag = true;
                }
                else {
                    InvertMode = false;
                    key[1].drawButton();
                    reference_value = meas_value;
                    NewValueFlag = true;
                }
            }

            if (b == 2) {  //SET button pressed, toggle SetFlag
                if (SetFlag == false){
                    SetFlag = true;
                    numberIndex = 0; // Reset index to 0
                    numberBuffer[numberIndex] = 0; // Place null in buffer
                }
                else {
                    key[2].drawButton(); // draw normal
                    SetFlag = false;
                    reference_value = meas_value;
                    NewValueFlag = true;
                }
            }

            if (SetFlag){
                if ((b >= 3) && (b < 14)) { // if a numberpad button, append the relevant # to the numberBuffer
                    if (numberIndex < NUM_LEN) {
                        numberBuffer[numberIndex] = keyLabel[b][0];
                        numberIndex++;
                        numberBuffer[numberIndex] = 0; // zero terminate
                    }
                }

                if (b == 0) {               // CLR  clear all chars
                    numberIndex = 0; // Reset index to 0
                    numberBuffer[numberIndex] = 0; // Place null in buffer
                }

                // Update the number display field
                tft.setTextDatum(TL_DATUM);        // Use top left corner as text coord datum
                tft.setFreeFont(&FreeSans24pt7b);  // Choose a nicefont that fits box
                tft.setTextColor(INV_WHITE, INV_ORANGE, true);     // Set the font colour

                set_value = atof(numberBuffer);

                if (FINE_RESOLUTION){
                    decimal_places = 3;
                }
                else {
                    decimal_places = 2;
                }
        
                if (InchFlag){ decimal_places = decimal_places + 2 ;}
                
                xwidth = tft.drawFloat(set_value, decimal_places, DISP_X + 100, DISP_Y + 20);
                // Now cover up the rest of the line up by drawing a black rectangle.  No flicker this way
                // but it will not work with italic or oblique fonts due to character overlap.
                tft.fillRect(DISP_X + 100 + xwidth, DISP_Y + 1, DISP_W - xwidth - 101, DISP_H - 2, INV_BLACK);
            }
              
            // Toggle diameter or linear measurement
            if (b == 14) {
                if (DiameterMode == false){
                    DiameterMode = true;
                   
                    tft.setTextDatum(TL_DATUM);        // Use top left corner as text coord datum
                    tft.setFreeFont(&FreeSans24pt7b);  // Choose a nicefont that fits box
                    tft.setTextColor(DISP_TCOLOR);     // Set the font colour

                    // Draw the diameter sign by combining "/" and "o", the value returned is the width in pixels
                    xwidth_dia = tft.drawString("o", DISP_X + 10, DISP_Y + 16);
                    xwidth_dia = tft.drawString("/", DISP_X + 16, DISP_Y + 20);
                    //status("Diameter mode set");
                }
                else {
                    DiameterMode = false;
                    tft.fillRect(DISP_X + 10 , DISP_Y + 1, xwidth_dia + 15, DISP_H - 2, INV_BLACK);
                    //status("Diameter mode cleared");
                }
            }
        } 
            delay(10); // UI debouncing
    }



    if ((NewValueFlag == true) && (SetFlag == false)) {
        tft.setTextDatum(TL_DATUM);        // Use top left corner as text coord datum
        tft.setFreeFont(&FreeSans24pt7b);  // Choose a nicefont that fits box
        tft.setTextColor(INV_WHITE, INV_BLACK, true);     // Set the font colour
        
        if (DiameterMode) {
            if (InvertMode) {            
                disp_value = set_value + (reference_value - meas_value) * 2.0;
            }
            else {
                disp_value = set_value - (reference_value - meas_value) * 2.0;
            }            
        }
        else {
            if (InvertMode) {
                disp_value = set_value + (reference_value - meas_value);
            }
            else {
                disp_value = set_value - (reference_value - meas_value);
            }
        }

        if (FINE_RESOLUTION){
            decimal_places = 3;
        }
        else {
            decimal_places = 2;
        }
        
        if (InchFlag){ decimal_places = decimal_places + 2 ;}

        xwidth_float = tft.drawFloat(disp_value, decimal_places, DISP_X + 100, DISP_Y + 20);
        // Now cover up the rest of the line up by drawing a black rectangle.  No flicker this way
        // but it will not work with italic or oblique fonts due to character overlap.
        tft.fillRect(DISP_X + 100 + xwidth_float, DISP_Y + 1, DISP_W - xwidth_float - 101, DISP_H - 2, INV_BLACK);
        NewValueFlag = false;
    }
}

void setup1() {  // do caliper readout on core 1
    pinMode(GPIO_CLOCK, INPUT);
    pinMode(GPIO_DATA, INPUT);
    
    delay(3000); // delay the reading of the caliper after startup to ensure smooth uploading of program
    attachInterrupt(digitalPinToInterrupt(GPIO_CLOCK), isr_for_clock, FALLING);  //attach interrupt to clock input
    gate = 0;
    gi = 0;
}

void loop1() {
    // nothing to do here, everything is done in the interrupt service routine
}

void drawKeypad()
{
  // Draw the keys
  for (uint8_t row = 0; row < 5; row++) {
    for (uint8_t col = 0; col < 3; col++) {
      uint8_t b = col + row * 3;

      if (b < 3) tft.setFreeFont(LABEL1_FONT);
      else tft.setFreeFont(LABEL2_FONT);

      key[b].initButton(&tft, KEY_X + col * (KEY_W + KEY_SPACING_X),
                        KEY_Y + row * (KEY_H + KEY_SPACING_Y), // x, y, w, h, outline, fill, text
                        KEY_W, KEY_H, INV_WHITE, keyColor[b], INV_WHITE,
                        keyLabel[b], KEY_TEXTSIZE);
      key[b].drawButton();
    }
  }
}

void touch_calibrate()  // calibrate touch pad
{
  uint16_t calData[5];
  uint8_t calDataOK = 0;

  // check file system exists
  if (!SPIFFS.begin()) {
    Serial.println("Formating file system");
    SPIFFS.format();
    SPIFFS.begin();
  }

  // check if calibration file exists and size is correct
  if (SPIFFS.exists(CALIBRATION_FILE)) {
    if (REPEAT_CAL)
    {
      // Delete if we want to re-calibrate
      SPIFFS.remove(CALIBRATION_FILE);
    }
    else
    {
      File f = SPIFFS.open(CALIBRATION_FILE, "r");
      if (f) {
        if (f.readBytes((char *)calData, 14) == 14)
          calDataOK = 1;
        f.close();
      }
    }
  }

  if (calDataOK && !REPEAT_CAL) {
    // calibration data valid
    tft.setTouch(calData);
  } else {
    // data not valid so recalibrate
    tft.fillScreen(INV_BLACK);
    tft.setCursor(20, 0);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(INV_WHITE, INV_BLACK);

    tft.println("Touch corners as indicated");

    tft.setTextFont(1);
    tft.println();

    if (REPEAT_CAL) {
      tft.setTextColor(INV_RED, INV_BLACK);
      tft.println("Set REPEAT_CAL to false to stop this running again!");
    }

    tft.calibrateTouch(calData, INV_MAGENTA, INV_BLACK, 15);

    tft.setTextColor(INV_GREEN, INV_BLACK);
    tft.println("Calibration complete!");

    // store data
    File f = SPIFFS.open(CALIBRATION_FILE, "w");
    if (f) {
      f.write((const unsigned char *)calData, 14);
      f.close();
    }
  }
}

// Print text in the mini status bar
void status(const char *msg) {
    //tft.setTextPadding(240);
    tft.fillRect(STATUS_X -120, STATUS_Y, 240 , 12, INV_YELLOW);    // Draw number display area 
    //tft.setCursor(STATUS_X, STATUS_Y);
    tft.setTextColor(INV_WHITE, INV_DARKGREY);
    tft.setTextFont(0);
    tft.setTextDatum(TC_DATUM);
    tft.setTextSize(1);
    xwidth_status = tft.drawString(msg, STATUS_X, STATUS_Y);
}

