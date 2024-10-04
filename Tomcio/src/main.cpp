#include <Arduino.h>
#include "FS.h"
#include <SPI.h>
#include <TFT_eSPI.h>    
#include "Free_Fonts.h"

TFT_eSPI tft = TFT_eSPI(); 

uint16_t prog_data[9][17];                       // Programs data
uint8_t sel_p;                                   // Selected program
uint32_t scale;                                  // Scale to display progress maker
unsigned long startTime;                         // Timestamp when step was started
unsigned long endTime;                           // Timestamp when step will end
unsigned long agitTime;                          // Timestamp of next agitation
unsigned long curr_time;                         // Curr time to calc display progress maker
uint8_t vibro = 0;                               // controlls vibration

#define CALIBRATION_FILE "/TouchCalData2"        // Calibration file
#define REPEAT_CAL false                         // Setting True will run calibration every time

//---------------------------------Stepper driver---------------------------------
#include "DRV8825.h"
// Motor steps per revolution. Most steppers are 200 steps or 1.8 degrees/step
#define MOTOR_STEPS 200
#define RPM         20
#define DIR         10
#define STEP        12
#define MODE0       48
#define MODE1       47
#define MODE2       21
#define ENABLE      11
#define MICROST     16
#define MOTOR_ACCEL 1000
#define MOTOR_DECEL 1000
DRV8825 stepper(MOTOR_STEPS, DIR, STEP, ENABLE, MODE0, MODE1, MODE2);

//=================================TIMER=================================

volatile bool tick = 0;
hw_timer_t *Timer0_Cfg = NULL;

void IRAM_ATTR Timer0_ISR(){
    tick = 1;
    if(vibro > 0){
        vibro--;
    } else   digitalWrite(8, LOW);
}

// function declarations
  void init_SPIFFS();
  void touch_calibrate();
  void load_programs();
  void read_prog();
  void edit_prog(int prog);
  void sel_prog();
  void irig(int ir_cnt);
  void tft_upd();
  void dev_stage();
  void stop_stage();
  void fix_stage();
  void rinse_stage();

//=================================SETUP=================================

void setup() {

  Serial.begin(115200);

  // define pins
    pinMode(8,  OUTPUT);       // vibration
    pinMode(18, OUTPUT);      // buzzer
    pinMode(1,  OUTPUT);       // status led

  // Init screen
    tft.init();
    tft.setRotation(1);
  
  // Init SPIFFS
  init_SPIFFS();

  // Initial functions
    //load_programs();    <<== uncomment to initially load programs
    read_prog();
    touch_calibrate();

  // Turn on status LED - all initials done
  digitalWrite(1, HIGH);

  // Welcome screen
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setFreeFont(FF32);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Tomcio", tft.width() / 2, tft.height() / 2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setFreeFont(FF29);
    tft.drawString("Universal film development helper", tft.width() / 2, tft.height() / 2 + 60);
    tft.setFreeFont(FF17);
    tft.drawString("Set tank holder in position", tft.width() / 2, tft.height() / 2 + 120);
    delay(10000);
    tft.fillScreen(TFT_BLACK);

  // Motor config
    stepper.begin(RPM);
    stepper.setMicrostep(MICROST);
    stepper.setEnableActiveState(LOW);
    stepper.enable();
    stepper.setSpeedProfile(stepper.LINEAR_SPEED, MOTOR_ACCEL, MOTOR_DECEL);

  // Timer config
    Timer0_Cfg = timerBegin(0, 8000, true);
    timerAttachInterrupt(Timer0_Cfg, &Timer0_ISR, true);
    timerAlarmWrite(Timer0_Cfg, 5000, true);
    timerAlarmEnable(Timer0_Cfg);

  // Execute program
    sel_prog();
    dev_stage();
    stop_stage();
    fix_stage();
    rinse_stage();
}

//=================================ENDLESS LOOP=================================

void loop(void) {
  tft.fillScreen(TFT_BLACK);
  tft.setFreeFont(FF32);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.drawString("DONE", tft.width() / 2, tft.height() / 2);

  while(1){}
}

//=================================INITIAL FUNCTIONS=================================

//---------------------------------Init SPIFFS---------------------------------
void init_SPIFFS(){
  if (!SPIFFS.begin()) {
    Serial.println("formatting file system");
    SPIFFS.format();
    SPIFFS.begin();
  }else Serial.println("file system formatted already");
}

//---------------------------------Touchscreen calibration---------------------------------
void touch_calibrate(){

  uint16_t calData[5];
  uint8_t calDataOK = 0;

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
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(20, 0);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    tft.println("Touch corners as indicated");

    tft.setTextFont(1);
    tft.println();

    if (REPEAT_CAL) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.println("Set REPEAT_CAL to false to stop this running again!");
    }

    tft.calibrateTouch(calData, TFT_MAGENTA, TFT_BLACK, 15);

    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.println("Calibration complete!");

    // store data
    File f = SPIFFS.open(CALIBRATION_FILE, "w");
    if (f) {
      f.write((const unsigned char *)calData, 14);
      f.close();
    }
  }
}

//---------------------------------Load initial program to SPFIFS---------------------------------
void load_programs(){

   if (!SPIFFS.exists("/Program_1")) {
    Serial.println("Writting Program_1");
    File f = SPIFFS.open("/Program_1", "w");
    uint16_t prog_data[17] = {4 , 450 , 4 , 60 , 6 , 120 , 6 , 60 , 4 , 240 , 4 , 60 , 5 , 10 , 20 , 0 , 0};
      uint8_t prog_data8[34];
      uint8_t z = 0;
      for (int i = 0; i < 17; i++) {
        prog_data8[z] = prog_data[i] & 0xff;
        prog_data8[z+1] = (prog_data[i] >> 8) & 0xff;
        z = z + 2;
      }
    if (f) {
      f.write(prog_data8, sizeof(prog_data8));
      f.close();
    }
   } else Serial.println("Program_1 exists");

  for (int p = 2; p<10; p++){
    String file_name = "/Program_"+String(p);

      if (!SPIFFS.exists(file_name)) {
      Serial.println("Writting " + file_name);
      File f = SPIFFS.open(file_name, "w");
      uint16_t prog_data[17] = {0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0 , 0};
        uint8_t prog_data8[34];
        uint8_t z = 0;
        for (int i = 0; i < 17; i++) {
          prog_data8[z] = prog_data[i] & 0xff;
          prog_data8[z+1] = (prog_data[i] >> 8) & 0xff;
          z = z + 2;
        }
      if (f) {
        f.write(prog_data8, sizeof(prog_data8));
        f.close();
      }
    } else Serial.println(file_name + " exists");
  }

}

//---------------------------------Read programs from SPFIFS---------------------------------
void read_prog(){

  uint8_t prog_data8[34];

  for (int pr = 0; pr < 9; pr = pr +1) {
    String file_name = "/Program_" + String(pr+1);
    File f = SPIFFS.open(file_name, "r");
    f.read(prog_data8, 34);
      int z = 0;
      for (int i = 0; i<17; i++){
        prog_data[pr][i] = prog_data8[z] + (prog_data8[z+1]<<8);
        z = z + 2;
      }
    f.close();
  }
}

//---------------------------------Edit selected program---------------------------------
void edit_prog(int prog){

  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE,TFT_BLACK);
  tft.setTextDatum(ML_DATUM);
  tft.drawString("Dev", 5, 60);
  tft.drawString("Stop", 5, 120);
  tft.drawString("Fix", 5, 180);
  tft.drawString("Rinse", 5, 260);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("I-A", 140,10);
  tft.drawString("Time", 240,10);
  tft.drawString("A-C", 340,10);
  tft.drawString("A-T", 440,10);

  uint8_t prog_data_cnt = 0;

  for(uint8_t d = 1; d < 4; d++){
    for(uint8_t p = 1; p < 5; p++){

      tft.setTextColor(TFT_WHITE,TFT_BLACK);
      tft.drawString(String(prog_data[prog-1][prog_data_cnt]),(p *100) + 40,d * 60);
      prog_data_cnt++;

      tft.setTextColor(TFT_RED,TFT_WHITE);
      tft.drawString("-",(p *100) + 10,d * 60);
      tft.setTextColor(TFT_BLUE,TFT_WHITE);
      tft.drawString("+",(p *100) + 70,d * 60);

    }
  }

  for(uint8_t d = 0; d < 5; d++){

    tft.setTextColor(TFT_WHITE,TFT_BLACK);
    tft.drawString(String(prog_data[prog-1][12 + d]),140 + (d * 75),260);
    tft.setTextColor(TFT_RED,TFT_WHITE);
    tft.drawString("-",115 + (d * 75),260);
    tft.setTextColor(TFT_BLUE,TFT_WHITE);
    tft.drawString("+",165 + (d * 75),260);

  }

  tft.setTextColor(TFT_BLACK,TFT_GREEN);
  tft.setTextDatum(BL_DATUM);
  tft.drawString("SAVE",10,320);
  tft.setTextColor(TFT_BLACK,TFT_YELLOW);
  tft.setTextDatum(BR_DATUM);
  tft.drawString("CANCEL",470,320);

  uint16_t x, y;
  uint8_t ret_res = 0;
  tft.setTextColor(TFT_WHITE,TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextDatum(MC_DATUM);

  do{  
    while(!tft.getTouch(&x, &y)){
      delay(5);
    }

    uint8_t prog_data_cnt = 0;

    for(uint8_t d = 1; d < 4; d++){
      for(uint8_t p = 1; p < 5; p++){

        if ((x > (p * 100)) && (x < (p * 100) + 20) && (y > (d * 60) - 10) && (y < (d * 60) + 10)) {
          if(prog_data[prog-1][prog_data_cnt]>0) prog_data[prog-1][prog_data_cnt]--;
          tft.drawString("    ", (p * 100) + 40, d * 60);
          tft.drawString(String(prog_data[prog-1][prog_data_cnt]), (p *100) + 40, d * 60);
        }

        if ((x > (p * 100) + 60) && (x < (p *100) + 80) && (y > (d * 60) - 10) && (y < (d * 60) + 10)) {
          prog_data[prog-1][prog_data_cnt]++;
          tft.drawString("    ", (p * 100) + 40, d * 60);
          tft.drawString(String(prog_data[prog-1][prog_data_cnt]), (p *100) + 40, d * 60);
        }
      prog_data_cnt++;
      }
    }

    for(uint8_t d = 0; d < 5; d++){

      if ((x > 105 + (d * 75)) && (x < 125 + (d * 75)) && (y > 250) && (y < 270)) {
        if(prog_data[prog-1][12 + d]>0) prog_data[prog-1][12 + d]--;
        tft.drawString("   ", 140 + (d * 75), 260);
        tft.drawString(String(prog_data[prog-1][12 + d]), 140 + (d * 75), 260);
      }

      if ((x > 155 + (d * 75)) && (x < 175 + (d * 75)) && (y > 250) && (y < 270)) {
        prog_data[prog-1][12 + d]++;
        tft.drawString("   ", 140 + (d * 75), 260);
        tft.drawString(String(prog_data[prog-1][12 + d]), 140 + (d * 75), 260);
      }

    }

    if ((x > 9) && (x < 55) && (y > 300) && (y < 321)) {
      ret_res =1;
    }

    if ((x > 425) && (x < 471) && (y > 300) && (y < 321)) {
      ESP.restart();
    }

    delay(30);
  } while(ret_res == 0);

  //save

  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(1);
  tft.setTextColor(TFT_RED,TFT_BLACK);
  tft.setTextSize(5);
  tft.drawString("SAVING",240,160);

    String file_name = "/Program_"+String(prog);
      SPIFFS.remove(file_name);

      if (!SPIFFS.exists(file_name)) {
      Serial.println("Opening " + file_name);
      File f = SPIFFS.open(file_name, "w");

        if (!f) {
            Serial.println("Failed to open file for writing");
        } else {
            Serial.println("File opened for writing");
        }

        uint8_t prog_data8[34];
        uint8_t z = 0;
        for (int i = 0; i < 17; i++) {
          prog_data8[z] = prog_data[prog-1][i] & 0xff;
          prog_data8[z+1] = (prog_data[prog-1][i] >> 8) & 0xff;
          z = z + 2;
        }
      if (f) {
        f.write(prog_data8, sizeof(prog_data8));
        f.close();
      }
    } 

    delay(5000);

    ESP.restart();

}

//---------------------------------Display program init screen---------------------------------
void sel_prog(){

  int prog = 1;
  bool set = 0;

  tft.setTextSize(1);
  tft.setFreeFont(FF22);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("Select program", tft.width() / 2, 15);
  tft.drawLine(0,40,480,40,TFT_WHITE);
  tft.fillTriangle(20,270,63,300,63,240,TFT_BLUE);
  tft.fillTriangle(460,270,417,300,417,240,TFT_BLUE);
  tft.fillRect(130,240,220,60,TFT_GREEN);
  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(FF5);
  tft.setTextColor(TFT_RED,TFT_BLACK);
  tft.drawString("Edit",450,55);
  tft.setTextSize(1);
  tft.setFreeFont(FF22);
  tft.setTextColor(TFT_BLACK, TFT_GREEN);
  tft.drawString("LOAD", tft.width() / 2, 270);
  tft.setTextFont(1);
  tft.setTextDatum(TL_DATUM);

  do {

    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("Program: " + String(prog),15,45);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("DEVELOPMENT",25,60);
    tft.drawString("Initial agitation: " + String(prog_data[prog-1][0]) + " rotation(s)",35,75);
    tft.drawString("Develop for " + String(prog_data[prog-1][1]) + "s with " + String(prog_data[prog-1][2]) + " rotation(s) every " + String(prog_data[prog-1][3]) +"s",35,90);
    tft.drawString("STOP BATH",25,105);
    tft.drawString("Initial agitation: " + String(prog_data[prog-1][4]) + " rotation(s)",35,120);
    tft.drawString("Bath for " + String(prog_data[prog-1][5]) + "s with " + String(prog_data[prog-1][6]) + " rotation(s) every " + String(prog_data[prog-1][7]) +"s",35,135);
    tft.drawString("FIX",25,150);
    tft.drawString("Initial agitation: " + String(prog_data[prog-1][8]) + " rotation(s)",35,165);
    tft.drawString("Bath for " + String(prog_data[prog-1][9]) + "s with " + String(prog_data[prog-1][10]) + " rotation(s) every " + String(prog_data[prog-1][11]) +"s",35,180);
    tft.drawString("RINSE",25,195);
    tft.drawString("Pattern: " + String(prog_data[prog-1][12]) + " - " + String(prog_data[prog-1][13]) + " - " + String(prog_data[prog-1][14]) + " - " + String(prog_data[prog-1][15]) + " - " + String(prog_data[prog-1][16]) + " rotation(s)",35,210);

    uint16_t x, y;
    while(!tft.getTouch(&x, &y)){
      delay(5);
    }

    if ((x > 20) && (x < 63)) {
      if ((y > 240) && (y < 300)) {
        prog = prog - 1;
        if (prog == 0) prog = 9;
        tft.fillRect(0,41,420,174,TFT_BLACK);
        delay(15);
      }
    }

    if ((x > 417) && (x < 460)) {
      if ((y > 240) && (y < 300)) {
        prog = prog + 1;
        if (prog == 10) prog = 1;
        tft.fillRect(0,41,420,174,TFT_BLACK);
        delay(15);
      }
    }

    if ((x > 130) && (x < 350)) {
      if ((y > 240) && (y < 300)) {
        set = 1;
        delay(15);
      }
    }

    if ((x > 430) && (x < 470)) {
      if ((y > 50) && (y < 60)) {
        edit_prog(prog);
      }
    }

  } while(set == 0);

  sel_p = prog - 1;
}

//=================================DEVELOP FUNCTIONS=================================

//---------------------------------Development---------------------------------
void irig(int ir_cnt){
  tft.fillRect(0,145,480,160,TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(TFT_GREEN, TFT_GREEN);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Agitation", tft.width() / 2, 225);
  
  int8_t direc = 1;
  for( uint8_t i = 0; i < ir_cnt; i++){
    stepper.rotate(90 * direc);
    delay(125);
    stepper.rotate(-90 * direc);
    delay(125);
    direc = direc * (-1);
  }

  tft.fillRect(0,145,480,160,TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_GREEN, TFT_GREEN);
  tft.drawString("WAIT", tft.width() / 2, 225);

  digitalWrite(8, HIGH);
  vibro = 6;
}

void tft_upd(){
  tft.setTextColor(TFT_GOLD, TFT_BLACK);
  tft.setTextDatum(MR_DATUM);
  tft.setFreeFont(FF6);
  tft.setTextSize(1);
  tft.fillRect(300,0,180,40,TFT_BLACK);
  curr_time = millis();
  int m = ((endTime - curr_time)/1000) / 60;
  int s = ((endTime - curr_time)/1000) % 60;
  if (s < 10) tft.drawString(String(m) + ":0" + String(s), 475, 20);
  else tft.drawString(String(m) + ":" + String(s), 475, 20);
  tft.fillRect(20,92,440,9,TFT_BLACK);
  tft.fillTriangle(((curr_time-startTime)*scale/100000L)+30,93,((curr_time-startTime)*scale/100000L)+35,100,((curr_time-startTime)*scale/100000L)+25,100,TFT_CYAN);
}

void dev_stage(){
  tft.fillScreen(TFT_BLACK);

  tft.setFreeFont(FF22);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setTextDatum(ML_DATUM);
  tft.setTextSize(1);
  tft.drawString("DEVELOPMENT", 20, 20);
  tft.setTextDatum(MR_DATUM);
  tft.setFreeFont(FF6);
  tft.setTextSize(1);
  int m = prog_data[sel_p][1] / 60;
  int s = prog_data[sel_p][1] % 60;
  if (s < 10) tft.drawString(String(m) + ":0" + String(s), 475, 20);
  else tft.drawString(String(m) + ":" + String(s), 475, 20);
  tft.drawLine(0,47,480,47,TFT_WHITE);
  tft.drawRect(29,69,422,22,TFT_WHITE);
  scale = 42000/(prog_data[sel_p][1]);
  tft.fillRect(450-(0.1*scale),70,(0.1*scale),20,TFT_RED);
  uint16_t t = prog_data[sel_p][3] + 1;
  do {
    tft.fillRect(30 + (t*scale/100),70,(prog_data[sel_p][2] * 2.5 * scale/100),20,TFT_YELLOW);
    t=t+prog_data[sel_p][3];
  } while(t < prog_data[sel_p][1]);
  tft.fillRect(30,70,((prog_data[sel_p][3] - prog_data[sel_p][0] *2.5) * scale/100),20,TFT_GREEN);
  tft.fillTriangle(29,93,34,100,24,100,TFT_CYAN);

  tft.fillSmoothRoundRect(130,150,220,150,10,TFT_GREEN,TFT_WHITE);

  tft.setTextSize(2);
  tft.setFreeFont(FF22);
  tft.setTextColor(TFT_BLACK, TFT_GREEN);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("START", tft.width() / 2, 225);

  bool click = 0;
  while(click == 0){

    uint16_t x, y;
    while(!tft.getTouch(&x, &y)){
      delay(5);
    }

    if ((x > 130) && (x < 350)) {
      if ((y > 150) && (y < 300)) {
        click = 1;
        delay(15);
      }
    }
  }

  startTime = millis();
  endTime   = startTime + prog_data[sel_p][1] * 1000;
  agitTime  = startTime + prog_data[sel_p][3] * 1000;
  curr_time = startTime;

  tft.fillSmoothRoundRect(130,150,220,150,10,TFT_LIGHTGREY,TFT_WHITE);
  tft.setTextSize(2);
  tft.setFreeFont(FF22);
  tft.setTextColor(TFT_BLACK, TFT_LIGHTGREY);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("START", tft.width() / 2, 225);

  delay(1000);

  tft.fillSmoothRoundRect(130,150,220,150,10,TFT_GREEN,TFT_WHITE);
  tft.setTextSize(1);
  tft.setTextColor(TFT_BLACK, TFT_GREEN);
  tft.drawString("Initial", tft.width() / 2, 210);
  tft.drawString("agitation", tft.width() / 2, 240);

  click = 0;
  while(click == 0){

    uint16_t x, y;
    while(!tft.getTouch(&x, &y)){
      if (tick){
        tft_upd();
        tick = 0;
      }
    }

    if ((x > 130) && (x < 350)) {
      if ((y > 150) && (y < 300)) {
        click = 1;
        delay(15);
      }
    }
  }

  irig(prog_data[sel_p][0]);


  bool drain = 1;
  while(millis() < endTime){
  
    if (millis() > agitTime){
      irig(prog_data[sel_p][2]);
      agitTime  = agitTime + prog_data[sel_p][3] * 1000;
    }
  
    if (millis() > endTime - 10000 && drain){
      digitalWrite(18, HIGH);
      tft.fillRect(0,145,480,160,TFT_BLACK);
      tft.setTextSize(2);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.setTextDatum(MC_DATUM);
      tft.drawString("DRAIN OFF", tft.width() / 2, 225);
      drain = 0;
    }

    if (tick){
      tft_upd();
      tick = 0;
    }
  }
  digitalWrite(18, LOW);
  tft.fillRect(0,145,480,160,TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("DEVELOPMENT DONE", tft.width() / 2, 225);
  delay(5000);

}

//---------------------------------Stop bath---------------------------------
void stop_stage(){
  tft.fillScreen(TFT_BLACK);

  tft.setFreeFont(FF22);
  tft.setTextColor(TFT_DARKGREEN, TFT_BLACK);
  tft.setTextDatum(ML_DATUM);
  tft.setTextSize(1);
  tft.drawString("STOP BATH", 20, 20);
  tft.setTextDatum(MR_DATUM);
  tft.setFreeFont(FF6);
  tft.setTextSize(1);
  int m = prog_data[sel_p][5] / 60;
  int s = prog_data[sel_p][5] % 60;
  if (s < 10) tft.drawString(String(m) + ":0" + String(s), 475, 20);
  else tft.drawString(String(m) + ":" + String(s), 475, 20);
  tft.drawLine(0,47,480,47,TFT_WHITE);
  tft.drawRect(29,69,422,22,TFT_WHITE);
  scale = 42000/(prog_data[sel_p][5]);
  uint16_t t = prog_data[sel_p][7] + 1;
  do {
    tft.fillRect(30 + (t*scale/100),70,(prog_data[sel_p][6] * 2.5 * scale/100),20,TFT_YELLOW);
    t=t+prog_data[sel_p][7];
  } while(t < prog_data[sel_p][5]);
  tft.fillRect(30,70,((prog_data[sel_p][7] - prog_data[sel_p][4] *2.5) * scale/100),20,TFT_GREEN);
  tft.fillTriangle(29,93,34,100,24,100,TFT_CYAN);

  tft.fillSmoothRoundRect(130,150,220,150,10,TFT_GREEN,TFT_WHITE);

  tft.setTextSize(2);
  tft.setFreeFont(FF22);
  tft.setTextColor(TFT_BLACK, TFT_GREEN);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("START", tft.width() / 2, 225);

  bool click = 0;
  while(click == 0){

    uint16_t x, y;
    while(!tft.getTouch(&x, &y)){
      delay(5);
    }

    if ((x > 130) && (x < 350)) {
      if ((y > 150) && (y < 300)) {
        click = 1;
        delay(15);
      }
    }
  }

  startTime = millis();
  endTime   = startTime + prog_data[sel_p][5] * 1000;
  agitTime  = startTime + prog_data[sel_p][7] * 1000;
  long curr_time = startTime;

  tft.fillSmoothRoundRect(130,150,220,150,10,TFT_LIGHTGREY,TFT_WHITE);
  tft.setTextSize(2);
  tft.setFreeFont(FF22);
  tft.setTextColor(TFT_BLACK, TFT_LIGHTGREY);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("START", tft.width() / 2, 225);

  delay(1000);

  tft.fillSmoothRoundRect(130,150,220,150,10,TFT_GREEN,TFT_WHITE);
  tft.setTextSize(1);
  tft.setTextColor(TFT_BLACK, TFT_GREEN);
  tft.drawString("Initial", tft.width() / 2, 210);
  tft.drawString("agitation", tft.width() / 2, 240);

  click = 0;
  while(click == 0){

    uint16_t x, y;
    while(!tft.getTouch(&x, &y)){
      if (tick){
        tft_upd();
        tick = 0;
      }
    }

    if ((x > 130) && (x < 350)) {
      if ((y > 150) && (y < 300)) {
        click = 1;
        delay(15);
      }
    }
  }

  irig(prog_data[sel_p][4]);

  while(millis() < endTime){
  
    if (millis() > agitTime){
      irig(prog_data[sel_p][6]);
      agitTime  = agitTime + prog_data[sel_p][7] * 1000;
    }
  
    if (tick){
      tft_upd();
      tick = 0;
    }
  }

  tft.fillRect(0,145,480,160,TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("STOP BATH DONE", tft.width() / 2, 225);
  digitalWrite(18, HIGH);
  delay(5000);
  digitalWrite(18, LOW);

}

//---------------------------------Fix---------------------------------
void fix_stage(){
  tft.fillScreen(TFT_BLACK);

  tft.setFreeFont(FF22);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setTextDatum(ML_DATUM);
  tft.setTextSize(1);
  tft.drawString("FIXING", 20, 20);
  tft.setTextDatum(MR_DATUM);
  tft.setFreeFont(FF6);
  tft.setTextSize(1);
  int m = prog_data[sel_p][9] / 60;
  int s = prog_data[sel_p][9] % 60;
  if (s < 10) tft.drawString(String(m) + ":0" + String(s), 475, 20);
  else tft.drawString(String(m) + ":" + String(s), 475, 20);
  tft.drawLine(0,47,480,47,TFT_WHITE);
  tft.drawRect(29,69,422,22,TFT_WHITE);
  scale = 42000/(prog_data[sel_p][9]);
  tft.fillRect(450-(0.1*scale),70,(0.1*scale),20,TFT_RED);
  uint16_t t = prog_data[sel_p][11] + 1;
  do {
    tft.fillRect(30 + (t*scale/100),70,(prog_data[sel_p][10] * 2.5 * scale/100),20,TFT_YELLOW);
    t=t+prog_data[sel_p][11];
  } while(t < prog_data[sel_p][9]);
  tft.fillRect(30,70,((prog_data[sel_p][11] - prog_data[sel_p][8] *2.5) * scale/100),20,TFT_GREEN);
  tft.fillTriangle(29,93,34,100,24,100,TFT_CYAN);

  tft.fillSmoothRoundRect(130,150,220,150,10,TFT_GREEN,TFT_WHITE);

  tft.setTextSize(2);
  tft.setFreeFont(FF22);
  tft.setTextColor(TFT_BLACK, TFT_GREEN);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("START", tft.width() / 2, 225);

  bool click = 0;
  while(click == 0){

    uint16_t x, y;
    while(!tft.getTouch(&x, &y)){
      delay(5);
    }

    if ((x > 130) && (x < 350)) {
      if ((y > 150) && (y < 300)) {
        click = 1;
        delay(15);
      }
    }
  }

  startTime = millis();
  endTime   = startTime + prog_data[sel_p][9] * 1000;
  agitTime  = startTime + prog_data[sel_p][11] * 1000;
  long curr_time = startTime;

  tft.fillSmoothRoundRect(130,150,220,150,10,TFT_LIGHTGREY,TFT_WHITE);
  tft.setTextSize(2);
  tft.setFreeFont(FF22);
  tft.setTextColor(TFT_BLACK, TFT_LIGHTGREY);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("START", tft.width() / 2, 225);

  delay(1000);

  tft.fillSmoothRoundRect(130,150,220,150,10,TFT_GREEN,TFT_WHITE);
  tft.setTextSize(1);
  tft.setTextColor(TFT_BLACK, TFT_GREEN);
  tft.drawString("Initial", tft.width() / 2, 210);
  tft.drawString("agitation", tft.width() / 2, 240);

  click = 0;
  while(click == 0){

    uint16_t x, y;
    while(!tft.getTouch(&x, &y)){
      if (tick){
        tft_upd();
        tick = 0;
      }
    }

    if ((x > 130) && (x < 350)) {
      if ((y > 150) && (y < 300)) {
        click = 1;
        delay(15);
      }
    }
  }

  irig(prog_data[sel_p][8]);


  bool drain = 1;
  while(millis() < endTime){
  
    if (millis() > agitTime){
      irig(prog_data[sel_p][10]);
      agitTime  = agitTime + prog_data[sel_p][11] * 1000;
    }
  
    if (millis() > endTime - 10000 && drain){
      tft.fillRect(0,145,480,160,TFT_BLACK);
      tft.setTextSize(2);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.setTextDatum(MC_DATUM);
      tft.drawString("DRAIN OFF", tft.width() / 2, 225);
      drain = 0;
    }

    if (tick){
      tft_upd();
      tick = 0;
    }
  }

  tft.fillRect(0,145,480,160,TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("FIXING DONE", tft.width() / 2, 225);
  digitalWrite(18, HIGH);
  delay(5000);
  digitalWrite(18, LOW);

}

//---------------------------------Rinse---------------------------------
void rinse_stage(){
  tft.fillScreen(TFT_BLACK);
  tft.setFreeFont(FF22);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setTextDatum(ML_DATUM);
  tft.setTextSize(1);
  tft.drawLine(0,47,480,47,TFT_WHITE);

  for(uint8_t p = 1; p < 6; p++){

    if (prog_data[sel_p][11 + p] > 0) {
      tft.drawString("RINSE " + String(p), 20, 20);
      tft.fillSmoothRoundRect(130,150,220,150,10,TFT_GREEN,TFT_WHITE);

      tft.setTextSize(2);
      tft.setFreeFont(FF22);
      tft.setTextColor(TFT_BLACK, TFT_GREEN);
      tft.setTextDatum(MC_DATUM);
      tft.drawString("START", tft.width() / 2, 225);

      bool click = 0;
      while(click == 0){

        uint16_t x, y;
        while(!tft.getTouch(&x, &y)){
          delay(5);
        }
        if ((x > 130) && (x < 350)) {
          if ((y > 150) && (y < 300)) {
            click = 1;
            delay(15);
          }
        }
      }
      irig(prog_data[sel_p][11 + p]);
    }
  }
}



