#include <PinChangeInterrupt.h>
#include <Adafruit_NeoPixel.h>

#ifdef __AVR__
 #include <avr/power.h> // Required for 16 MHz Adafruit Trinket
#endif

#define LED_PIN     12

// How many NeoPixels are attached to the Arduino?
#define LED_COUNT  35

// NeoPixel brightness, 0 (min) to 255 (max)
#define BRIGHTNESS 230 // Set BRIGHTNESS to about 1/5 (max = 255)

// Declare our NeoPixel strip object:
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRBW + NEO_KHZ800);


/***********************/
/*        PINS         */
/***********************/
// PIR Sensor
const int pirPin = 8;
// Piezo
const int piezoPin[2] = {A0, A1};
// Button
const int buttonPin = 10; // Pin 10 (2)
const int buttonLedPin = 7; // Pin 4 (3)

//Button 2: Pin 6   Led: 4
//Button 3: Pin 14  Led: 44
//Button 4: Pin 15  Led: 45

// Matrix
// Output Pins => Change to use Interrupt Pins 19,18,2,3 (22, 24, 49, 47) then iterate ? 
const int PO_[4] = {19,18,2,3};
// Input Pins
const int PI_[16] = {52,50,48,53,51,42,40,38,36,34,32,30,28,26,24,22}; // (26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 53, 51)

/***********************/
/*        VARIABLES    */
/***********************/
// Matrix Values
// 120 = single 20, 220 = double 20, 320 = triple 20
// 125 = single Bull, 225 = double bull
const int MATRIX_VALUES[4][16] = {
    {212, 112, 209, 109, 214, 114, 211, 111, 208, 108, 000, 312, 309, 314, 311, 308},
    {216, 116, 207, 107, 219, 119, 203, 103, 217, 117, 225, 316, 307, 319, 303, 317},
    {202, 102, 215, 115, 210, 110, 206, 106, 213, 113, 125, 302, 315, 310, 306, 313},
    {204, 104, 218, 118, 201, 101, 220, 120, 205, 105, 000, 304, 318, 301, 320, 305}};

bool bI[4][16];
bool bHitDetected = false;

// Piezo
int iValue[2] = {0, 0};
int iPiezoThreshold = 20;
bool bMissedDart = false;
// Button
int iButtonState = 0;
// PIR
long lDuration;
int iPIRState = 0;
int iMotionDetected = 0;
int iDebounceWobbleTime = 750;
// Input String from Raspberry Pi to Arduino
const byte numChars = 32;
char cInputChars[numChars];
char cTempChars[numChars];
char cCommand[numChars] = {0};
int iParam = 0;
bool bNewData = false;
// String sInputString = "";
// boolean bStringComplete = false;

// State
// 0: INIT
// 1: THROW
// 2: NEXTPLAYER
// 3: MOTION DETECTED
// 4: RESET ULTRASONIC
// 5: WON
// 9: CONNECTED
int iState = 0;

/***********************/
/*       Functions     */
/***********************/

void CheckButton()
{
  // iButtonState = digitalRead(buttonPin);
  // Wenn gedrückt Ausgabe
  if (iButtonState == 1)
  {
    Serial.println("b");
    delay(500);
    iButtonState = 0;
  }
}

void ButtonOn()
{
  digitalWrite(buttonLedPin, HIGH);
}

void ButtonOff()
{
  digitalWrite(buttonLedPin, LOW);
}

void EvalThrow()
{
  bHitDetected = false;

  for (int x = 0; x < 4; x++)
  {
    digitalWrite(PO_[0], HIGH);
    digitalWrite(PO_[1], HIGH);
    digitalWrite(PO_[2], HIGH);
    digitalWrite(PO_[3], HIGH);
    digitalWrite(PO_[x], LOW);

    for (int y = 0; y < 16; y++)
    {
      bI[x][y] = digitalRead(PI_[y]);
      if (bI[x][y] == 0)
      {
        Serial.println(MATRIX_VALUES[x][y]);
        delay(300);
        bHitDetected = true;
        // Set Bull to 0
        bI[1][10] = 1;
        bI[2][10] = 1;
      }
    }
  }
}

void CheckMissed()
{
  bMissedDart = false;

  // Read both piezos
  for (int i = 0; i < 2; i++)
  {
    iValue[i] = analogRead(piezoPin[i]);

    if (iValue[i] >= iPiezoThreshold)
    {
      bMissedDart = true;
    }
  }

  if (!bHitDetected && bMissedDart)
  {
    Serial.println("m");
    delay(300);
  }
}

void BlinkExtraFast(int times)
{
  for (int i = 0; i < times; i++)
  {
    digitalWrite(buttonLedPin, HIGH);
    delay(100);
    digitalWrite(buttonLedPin, LOW);
    delay(100);
  }
}

void Blink(int times)
{
  for (int i = 0; i < times; i++)
  {
    digitalWrite(buttonLedPin, HIGH);
    delay(250);
    digitalWrite(buttonLedPin, LOW);
    delay(250);
  }
}

void BlinkSlow(int times)
{
  for (int i = 0; i < times; i++)
  {
    digitalWrite(buttonLedPin, HIGH);
    delay(750);
    digitalWrite(buttonLedPin, LOW);
    delay(750);
  }
}

void BlinkExtraSlow(int times)
{
  for (int i = 0; i < times; i++)
  {
    digitalWrite(buttonLedPin, HIGH);
    delay(1000);
    digitalWrite(buttonLedPin, LOW);
    delay(1000);
  }
}

void checkPIR()
{
  iPIRState = digitalRead(pirPin);
  if (iPIRState)
  {
    Serial.println("u");
  }
}

void SetMotion()
{
  iMotionDetected = 1;
}

void ResetMotion()
{
  iPIRState = 0;
  iMotionDetected = 0;
}

void RecvWithStartEndMarkers()
{
  // Define things
  static bool bRecvInProgress = false;
  static byte ndx = 0;
  char cStartMarker = '<';
  char cEndMarker = '>';
  char cRC;

  while (Serial.available() > 0 && bNewData == false)
  {
    // Read char
    cRC = Serial.read();

    if (bRecvInProgress == true)
    {
      if (cRC != cEndMarker)
      {
        // Append char to char array
        cInputChars[ndx] = cRC;
        ndx++;
        // Reduce char to prevent from buffer overflow
        if (ndx >= numChars)
        {
          ndx = numChars - 1;
        }
      }
      else
      {
        // Terminate char array
        cInputChars[ndx] = '\0';
        // Set things
        bRecvInProgress = false;
        ndx = 0;
        bNewData = true;
      }
    }
    else if (cRC == cStartMarker)
    {
      // Start reading
      bRecvInProgress = true;
    }
  }
}

void ParseData()
{
  // Copy to cCommand terminating at ","
  char *strtokIndx;
  strtokIndx = strtok(cTempChars, ",");
  strcpy(cCommand, strtokIndx);

  // Read rest beginning from ","
  strtokIndx = strtok(NULL, ",");
  iParam = atoi(strtokIndx);
}

void ProcessSerial()
{
  // Only process if new data
  if (bNewData == true)
  {
    // Copy to temp chars as strtok would write to original char array
    strcpy(cTempChars, cInputChars);
    // Parse data and split into command and param
    ParseData();

    // If, else if around cCommand
    if (strcmp(cCommand, "p") == 0)
    {
      // if p set new piezo threshold
      iPiezoThreshold = iParam;
    }
    else if (strcmp(cCommand, "u") == 0) {
      // if u set new debounce wobble time
      iDebounceWobbleTime = iParam;
    }
    else if (strcmp(cCommand, "s") == 0)
    {
      // if s set new game state and switch over it to control app flow
      iState = iParam;
      switch (iState)
      {
      case 1:
        // THROW
        ButtonOff();
        // clear if button pressed after motiondetection
        iButtonState = 0;
        break;
      case 2:
        // NEXTPLAYER
        ButtonOn();
        break;
      case 3:
        // MOTION DETECTED
        SetMotion();
        break;
      case 4:
        // RESET ULTRASONIC
        ResetMotion();
        break;
      case 9:
        // SERVICE CONNECTED
        Blink(7);
        iState = 1;
        break;
      default:
        break;
      }
    }
    else if (strcmp(cCommand, "b") == 0)
    {
      // Turn button off or on from remote
      switch (iParam)
      {
      case 0:
        ButtonOff();
      case 1:
        ButtonOn();
      default:
        break;
      }
    }

    // Reset bNewData state
    bNewData = false;
  }
}


void DetectMotion()
{
  // If there is no motion process ultrasonic
  switch (iMotionDetected)
  {
  case 0:
    // Button Overwrites Motion detection
    CheckButton();
    checkPIR();
    break;
  default:
    break;
  }
}

void EvalNextPlayer()
{
  switch (digitalRead(buttonLedPin))
  {
  case 0:
    ButtonOn();
    // wait before checking PIR
    delay(iDebounceWobbleTime);
    break;
  case 1:
    DetectMotion();
    break;
  default:
    break;
  }
}

void triggerButton() 
{
  iButtonState = 1;
}

/* Setup loop */
void setup()
{
  // LED Strip
  strip.begin();           // INITIALIZE NeoPixel strip object (REQUIRED)
  strip.show();            // Turn OFF all pixels ASAP
  strip.setBrightness(BRIGHTNESS);
  strip.fill(strip.Color(0, 0, 0, 255));
  strip.show();
  // Pins PIR-Sensor
  pinMode(pirPin, INPUT);
  // Pin Button
  pinMode(buttonPin, INPUT_PULLUP);
  //attachInterrupt(digitalPinToInterrupt(buttonPin), triggerButton, CHANGE);
  attachPCINT(digitalPinToPCINT(buttonPin), triggerButton, CHANGE);
  // Pin Button LED
  pinMode(buttonLedPin, OUTPUT);
  digitalWrite(buttonLedPin, LOW);
  // Matrix
  for (int i = 0; i < 4; i++)
  {
    pinMode(PO_[i], OUTPUT);
  }

  for (int i = 0; i < 16; i++)
  {
    pinMode(PI_[i], INPUT_PULLUP);
  }

  // Button blink 5 times
  // Blink(5);

  // Start serial
  Serial.begin(9600);
}

/* Main loop */
void loop()
{

  // Read serial and process it
  RecvWithStartEndMarkers();
  ProcessSerial();

  // Switch over state loop
  switch (iState)
  {
  case 0:
    BlinkSlow(1);
    break;
  case 1:
    EvalThrow();
    CheckMissed();
    CheckButton();
    break;
  case 2:
    EvalNextPlayer();
    break;
  case 3:
    Blink(1);
    break;
  case 5:
    CheckButton();
    BlinkExtraSlow(1);
    break;
  default:
    break;
  }
}
