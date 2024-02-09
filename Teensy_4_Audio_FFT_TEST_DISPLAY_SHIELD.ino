#include <Arduino.h>
#include "ILI9341Wrapper.h"
#include "MathUtil.h"
#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include <ILI9341_T4.h>
#include <tgx.h> 

#include "control_sgtl5000.h"
class mSGTL5000 : public AudioControlSGTL5000 {
  public:
    void attGAIN(uint8_t att) {
      modify(0x0020, (att & 1) << 8, 1 << 8);
    }
};


using namespace tgx;

// VIDEO
// DEFAULT WIRING USING SPI 0 ON TEENSY 4/4.1
//
#define PIN_SCK     13      // mandatory
#define PIN_MISO    12      // mandatory (if the display has no MISO line, set this to 255 but then VSync will be disabled)
#define PIN_MOSI    11      // mandatory
#define PIN_DC      10      // mandatory, can be any pin but using pin 10 (or 36 or 37 on T4.1) provides greater performance

#define PIN_CS      9       // optional (but recommended), can be any pin.  
#define PIN_RESET   6       // optional (but recommended), can be any pin. 
#define PIN_BACKLIGHT 255   // optional, set this only if the screen LED pin is connected directly to the Teensy.
#define PIN_TOUCH_IRQ 255   // optional. set this only if the touchscreen is connected on the same SPI bus
#define PIN_TOUCH_CS  255   // optional. set this only if the touchscreen is connected on the same spi bus

// Note: Pins Ordered 0, 1, 2, 3, 4, 14, 16, 17, 5 22 are free as switches
#define DIPA1 22
#define DIPA2 5
#define DIPA3 17
#define DIPA4 16
#define DIPA5 14
#define DIPA6 4
#define DIPA7 3 
#define DIPA8 2
#define DIPB1 1
#define DIPB2 0

// Define modes here. Refer to set_modes()
#define MUSIC_CIRCLES 0
#define VOICE_SPLINES 1

// 30MHz SPI. Can do much better with short wires
// #define SPI_SPEED       30000000
#define SPI_SPEED       60000000

// the screen driver object
ILI9341Wrapper tft(PIN_CS, PIN_DC, PIN_SCK, PIN_MOSI, PIN_MISO, PIN_RESET, PIN_TOUCH_CS, PIN_TOUCH_IRQ);

// 2 diff buffers with about 6K memory each
ILI9341_T4::DiffBuffStatic<6000> diff1;
ILI9341_T4::DiffBuffStatic<6000> diff2;

// screen size in portrait mode
#define LX  320
#define LY  240

// our framebuffers
uint16_t fb[LX * LY];              // the main framebuffer we draw onto.

// internal framebuffer (150K in DMAMEM) used by the ILI9431_T4 library for double buffering.
DMAMEM uint16_t internal_fb[LX * LY]  = { 0 };

// image that encapsulates fb.
Image<RGB565> im(fb, LX, LY);

// AUDIO
// GUItool: begin automatically generated code
AudioInputI2S            i2s1;           //xy=431,357
AudioAnalyzeFFT1024      fft1024_1;      //xy=577,294
AudioAnalyzeFFT1024      fft1024_2;      //xy=613,383
AudioConnection          patchCord1(i2s1, 0, fft1024_1, 0);
AudioConnection          patchCord2(i2s1, 1, fft1024_2, 0);
// AudioControlSGTL5000     sgtl5000_1;     //xy=314,302
mSGTL5000                sgtl5000_1;
// GUItool: end automatically generated code

// Counter variables
int count = 0;
int count2 = 0;
int count_circle = 0;
bool circle_trigger = false;
//bool reset_count = 1;

// Buffers
float fft_buffer1[100];
float fft_buffer2[100];
float voice_buffer[20];
float voice_buffer2[20];
float voice_peaks[4];
float voice_peaks2[4];

float water_left[45];
float water_right[45];

void set_gain() {
  int pot_read;
  int mic_gain;
  pot_read = analogRead(15);
  mic_gain = map(pot_read, 0, 1023, 0, 63);
  sgtl5000_1.micGain(mic_gain);
}

int set_mode() {
  if (!digitalRead(DIPA1)) {
    return VOICE_SPLINES;
  }
  if (!digitalRead(DIPA2)) {
    return 2;
  }
  if (!digitalRead(DIPA3)) {
    return 3;
  }
  if (!digitalRead(DIPA4)) {
    return 4;
  }
  if (!digitalRead(DIPA5)) {
    return 5;
  }
  if (!digitalRead(DIPA6)) {
    return 6;
  }
  if (!digitalRead(DIPA7)) {
    return 7;
  }
  if (!digitalRead(DIPA8)) {
    return 8;
  }
  return MUSIC_CIRCLES;
}

void set_circle(int low, int high, RGB565 color, int size, int mult_1, int mult_2, int count, int phase=0) {
  float f1 = max(fft_buffer1[low], fft_buffer1[high]);
  float f2 = max(fft_buffer2[low], fft_buffer2[high]);

  im.fillCircle(160 + cos(count) * (mult_1 * (f1 * mult_2)), 120 + sin(count) * (mult_1 * (f2 * mult_2)), size, color, color);
}

void set_cubic_bezier(int y, int peak, int rbuf, int gbuf, int bbuf) {
  int PAy = max(y - voice_peaks[peak] * 48 * 200, y - 48);
  int PBy = min(y + voice_peaks2[peak] * 48 * 200, y + 48);

  int r = 31 * max(fft_buffer2[rbuf], fft_buffer2[rbuf+5]) * 50 + 8;
  int g = 63 * max(fft_buffer2[gbuf], fft_buffer2[gbuf+5]) * 50 + 18;
  int b = 31 * max(fft_buffer2[bbuf], fft_buffer2[bbuf+5]) * 50 + 5;

  im.drawCubicBezier({0, y}, {320, y}, {100, PAy}, {200, PBy}, true, RGB565(r,g,b));
}

// Default visualizer
void music_visualizer_circles() {
   // Copy fft to buffer
  for (int i = 0; i < 45; i++) {
    fft_buffer1[i] = fft1024_1.read(i+2);
    fft_buffer2[i] = fft1024_2.read(i+2);
  }

  // Boom circle
  if (fft_buffer1[0] >= .02) {
    circle_trigger = true;
  }

  if (circle_trigger == true) {
    im.drawCircle(160, 120, count_circle, RGB565_White, .5);
    count_circle += 3;
  }

  if (count_circle > 200) {
    count_circle = 0;
    circle_trigger = false;
  }

  // Background noise
  int gt = (500 * fft_buffer1[1] * 8);
  int gr = constrain((32 * fft_buffer2[1] * 10), 1, 31);
  for (int y = 1; y < 5; y++) {
    for (int t = 0; t < gt; t++) {
      im.drawPixel(random(320), random(240), RGB565(15, 20, gr));
    }
  }

  // Circles
  set_circle(3, 25, RGB565_Magenta, 3, 80, 60, count2);
  set_circle(23, 42, RGB565_Red, 3, 80, 60, count, 30);
  set_circle(6, 28, RGB565_Blue, 3, 80, 60, count2, 90);
  set_circle(10, 32, RGB565_Teal, 3, 300, 60, count, 90);
  set_circle(13, 35, RGB565_Green, 3, 300, 60, count2, 120);
  set_circle(18, 40, RGB565_Salmon, 3, 150, 90, count, 120);

  im.drawCircle(160 + sin(count) * (80 * (fft_buffer1[0] * 60)), 120 + cos(count) * (80 * (fft_buffer2[0] * 60)), 6, RGB565_White);

  count += 2;
  count2 += 3;

  // Middle waterfall
  // im.drawFastHLine(0, 120, 320, RGB565(120,6,100));
  // for (int i = 0; i < 320; i++) {
  //   int lheight = (fft1024_1.read(i+2) * 240) * 30;
  //   im.drawFastVLine(i, 120 - (lheight / 2), lheight, RGB565_White);
  // }

  // Waterfall
  // im.drawFastHLine(0, 239, 320, RGB565(120,6,100));
  int p;
  int q;
  int r;
  p = 31 * (max(fft_buffer1[3], fft_buffer1[12])) * 50 + 12;
  q = 63 * (max(fft_buffer1[6], fft_buffer1[15])) * 50 + 18;
  r = 31 * (max(fft_buffer1[10], fft_buffer1[20])) * 50 + 12;

  // Lower
  int lheighttemp;
  int lheight;
  for (int i = 0; i < 45; i++) {
    lheighttemp = (fft_buffer1[i] * 240) * 15;
    lheight = min(lheighttemp, 240);
    im.fillRect(i*4, 240 - lheight, 4, 240, RGB565(p, q, r));
  }

  for (int i = 0; i < 45; i++) {
    lheighttemp = (fft_buffer2[i] * 240) * 15;
    lheight = min(lheighttemp, 240);
    im.fillRect(320 - i*4, 240 - lheight, 4, 240, RGB565(p, q, r));
  }

  // Upper
  for (int i = 0; i < 45; i++) {
    int amp;
    water_left[i] = fft_buffer1[i] > water_left[i] ? fft_buffer1[i] : water_left[i];
    if (i == 0) {
      amp = 8;
    }
    else {
      amp = 15;
    }
    int ulheighttemp = (water_left[i] * 240) * amp;
    int ulheight = min(ulheighttemp, 240);
    im.fillRect(i*4, 0, 4, ulheight, RGB565(5, 5, 5));
    if (water_left[i] > 0.02) {
      water_left[i] -= 0.05;
    }
    else {
      water_left[i] -= 0.0003;
    }
  }

  for (int i = 0; i < 45; i++) {
    int amp;
    water_right[i] = fft_buffer2[i] > water_right[i] ? fft_buffer2[i] : water_right[i];
    if (i == 0) {
      amp = 8;
    }
    else {
      amp = 15;
    }
    int urheighttemp = (water_right[i] * 240) * amp;
    int urheight = min(urheighttemp, 240);
    im.fillRect(320 - i*4, 0, 4, urheight, RGB565(5, 5, 5));
    if (water_right[i] > 0.02) {
      water_right[i] -= 0.05;
    }
    else {
      water_right[i] -= 0.0003;
    }
  }
}

void voice_splines() {
  // Copy fft to buffer
  for (int i = 0; i < 25; i++) {
    fft_buffer1[i] = fft1024_1.read(i+2);
    fft_buffer2[i] = fft1024_2.read(i+2);
  }

  voice_buffer[19] = fft_buffer1[5];
  voice_buffer2[19] = fft_buffer2[3];

  for (int i = 0; i < 4; i++) {
    voice_peaks[i] = fft_buffer1[8 + (2*i)] > voice_peaks[i] ? fft_buffer1[8 + (2*i)] : voice_peaks[i];
    voice_peaks2[i] = fft_buffer2[5 + (2*i)] > voice_peaks2[i] ? fft_buffer2[5 + (2*i)] : voice_peaks2[i];
  }

    // Background noise
  int gt = (500 * fft_buffer1[1] * 8);
  int gg = constrain((63 * fft_buffer2[1] * 10), 1, 31);
  for (int y = 1; y < 5; y++) {
    for (int t = 0; t < gt; t++) {
      im.drawPixel(random(320), random(240), RGB565(15, gg, 8));
    }
  }

  for (int i = 0; i < 4; i++) {
    set_cubic_bezier(48 + (48*i), i, 3 + (2*i), 2 + (2*i), 3 + (3*i));
  }

  for (int i = 0; i < 4; i++) {
    if (voice_peaks[i] > .005) {
      voice_peaks[i] -= .0008;
    }
    else {
      voice_peaks[i] -= .0002;
    }

    if (voice_peaks2[i] > .005) {
      voice_peaks2[i] -= .0008;
    }
    else {
      voice_peaks2[i] -= .0002;
    }
  }

  int lheight;
  for (int i = 0; i < 20; i++) {
    lheight = (voice_buffer[i] * 240) * 60;
    im.drawFastVLine(16 + i*16, 120 - (lheight / 2), lheight, RGB565_Yellow, 0.3);
  }

  for (int i = 0; i < 20; i++) {
    lheight = (voice_buffer2[i] * 240) * 60;
    im.drawFastVLine(16 + i*16, 120 - (lheight / 2), lheight, RGB565_Cyan, 0.3);
  }

  for (int i = 0; i < 19; i++) {
    voice_buffer[i] = voice_buffer[i+1];
    voice_buffer2[i] = voice_buffer2[i+1];
  }
}

void serial_fft_debug() {
  // each time new FFT data is available
  // print it all to the Arduino Serial Monitor
  Serial.print("FFT: ");
  for (int i=0; i<40; i++) {
    if (fft_buffer1[i] >= 0.01) {
      Serial.print(fft_buffer1[i]);
      Serial.print(" ");
    } else {
      Serial.print("  -  "); // don't print "0.00"
    }
  }
  // Serial.println(AudioMemoryUsageMax());
  Serial.println();
}

void setup() {
  // Serial.begin(9600);
  pinMode(DIPA1, INPUT_PULLUP);
  pinMode(DIPA2, INPUT_PULLUP);
  pinMode(DIPA3, INPUT_PULLUP);
  pinMode(DIPA4, INPUT_PULLUP);
  pinMode(DIPA5, INPUT_PULLUP);
  pinMode(DIPA6, INPUT_PULLUP);
  pinMode(DIPA7, INPUT_PULLUP);
  pinMode(DIPA8, INPUT_PULLUP);
  pinMode(DIPB1, INPUT_PULLUP);
  pinMode(DIPB2, INPUT_PULLUP);

  AudioMemory(18);

  sgtl5000_1.enable();
  sgtl5000_1.autoVolumeDisable();
  sgtl5000_1.surroundSoundDisable();
  sgtl5000_1.enhanceBassDisable();
  sgtl5000_1.muteHeadphone();
  // sgtl5000_1.muteLineout();
  sgtl5000_1.adcHighPassFilterDisable();

  sgtl5000_1.inputSelect(AUDIO_INPUT_MIC);
  sgtl5000_1.lineOutLevel(13); // 3.16 Volts p-p
  sgtl5000_1.volume(0.7f);     // 0.8 corresponds to the maximum undistorted output for a full scale signal
  sgtl5000_1.lineInLevel(0);   // 3.12 Volts p-p
  sgtl5000_1.micGain(20);
  sgtl5000_1.attGAIN(0);       // ADC volume range reduction down by 6.0 dB

  tft.output(&Serial);                // output debug infos to serial port.     
  while (!tft.begin(SPI_SPEED)) delay(1);      // init the display
  tft.setRotation(3);                 // portrait mode 240 x320
  tft.setFramebuffer(internal_fb);    // set the internal framebuffer (enables double buffering)
  tft.setDiffBuffers(&diff1, &diff2); // set the 2 diff buffers => activate differential updates. 
  tft.setDiffGap(4);                  // use a small gap for the diff buffers

  // Below, vsync_spacing = 2 means we want 120/2=60Hz fixed framerate with vsync enabled.
  // Here, at 30mhz spi, we could even choose vsync_spacing = 1 and refresh rate = 90hz
  // to get a solid 90fps and it would still works. We can also try setting vsync_spacing = 0 
  // to find out the maximum framerate without vsync (which will be over 100fps). 

  tft.setRefreshRate(120);            // around 120hz for the display refresh rate. 
  tft.setVSyncSpacing(2);             // set framerate = refreshrate/2 (and enable vsync at the same time). 


  if (PIN_BACKLIGHT != 255) { // make sure backlight is on
    pinMode(PIN_BACKLIGHT, OUTPUT);
    digitalWrite(PIN_BACKLIGHT, HIGH);
  }

  randomSeed(analogRead(15));
  im.fillScreen(RGB565_Black);
}

void loop() {
  im.fillScreen(RGB565_Black);
  set_gain();

  if (fft1024_1.available() && fft1024_2.available()) {
    switch (set_mode()) {
      case MUSIC_CIRCLES:
        music_visualizer_circles();
        break;
      case VOICE_SPLINES:
        voice_splines();
        break;
    }
    
    // FPS overlay via switch
    if (!digitalRead(DIPB2)) {
      tft.overlayFPS(fb);               // draw the FPS counter on the top right corner of the framebuffer          
    }

    // Serial FFT Plotter
    if (!digitalRead(DIPB1)) {
      serial_fft_debug();
    }
    tft.update(fb);
  }
}

