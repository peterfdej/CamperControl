
#include "FS.h"
#include <TFT_eSPI.h>  // Ili9488 Touch Setup21
#include <SPI.h>
#include <esp_now.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "SPIFFS.h"
#include "Settingswhite.h"

/* User_Setup.h
 * #define ILI9488_DRIVER
 * #define TFT_MISO 19
 * #define TFT_MOSI 23
 * #define TFT_SCLK 18
 * #define TFT_CS   25 
 * #define TFT_DC    2
 * #define TFT_RST   4 
 * #define TOUCH_CS 27
 */

TFT_eSPI tft = TFT_eSPI();

#define CALIBRATION_FILE "/TouchCalData"
#define TFT_GREY      0x5AEB
#define M_SIZE 0.66
#define TFT_BL   33 //LED poort tft screen
#define REPEAT_CAL false
//#define BLACK_SPOT // Comment out to stop drawing black spots on touch screen

// Setting button
#define SETTINGBUTTON_X 265
#define SETTINGBUTTON_Y 0
#define SETTINGBUTTON_W 55
#define SETTINGBUTTON_H 55

// + button
#define PLUSBUTTON_X SETTINGBUTTON_X
#define PLUSBUTTON_Y (SETTINGBUTTON_Y + SETTINGBUTTON_H)
#define PLUSBUTTON_W SETTINGBUTTON_W
#define PLUSBUTTON_H SETTINGBUTTON_H

// - button
#define MINBUTTON_X SETTINGBUTTON_X
#define MINBUTTON_Y (SETTINGBUTTON_Y + (SETTINGBUTTON_H*2))
#define MINBUTTON_W SETTINGBUTTON_W
#define MINBUTTON_H SETTINGBUTTON_H

AsyncWebServer server(80);
AsyncEventSource events("/events");

const char* ssidPath = "/ssid.txt";
const char* passPath = "/pass.txt";
const char* SSID_INPUT = "ssid";  //html id name
const char* PASS_INPUT = "pass";  //html id name

String ssid;
String pass;
bool networkConnected = false;
bool setupmode = false;
bool deletewifi = false;

float ltx = 0;    // Saved x coord of bottom of needle
uint32_t updateTime = 0;       // time for next update
uint32_t reconnectTime = 0;    // delay for reconnect

int X_POS[] = {0,163,180,250,30,30,5,5,0,0}; //10 devices V-meter, V-meter, gastank, gastank, watertank, watertank, temp, temp, x, x
int Y_POS[] = {360,360,190,190,190,270,0,90,0,0};
float new_VALUE[] = {-10,-10,0,0,0,0,0,0,0,0}; // receive values
float old_VALUE[] = {0,0,0,0,0,0,0,0,0,0};
String rec_desc[] = {"","","","","","","","","",""}; 
int old_PERC[] = {0,0,0,0,0,0,0,0,0,0}; // value 0 -100 %
float ltx_POS[] = {0,0}; // 2 analog meters
uint16_t osx_POS[] = {M_SIZE*120, M_SIZE*120};
uint16_t osy_POS[] = {M_SIZE*120, M_SIZE*120};
int activescreen = 1;

const int pwmFreq = 5000;
const int pwmResolution = 8;
const int pwmLedChannelTFT = 0;
int ledBacklight = 255; // 0 - 255

int value1[6] = {0, 0, 0, 0, 0, 0}; //for testdata
int d = 0;

float gasfrontmin = 6394; //weight empty tank
float gasfrontmax = 11589; //weight full tank
float gasbackmin = 6394; //weight empty tank
float gasbackmax = 11589; //weight full tank
float gasfrontperc = 0;
float gasbackperc = 0;
float watermin = 1950;
float watermax = 600;
float waterperc = 0;
float water2min = 1950;
float water2max = 600;
float water2perc = 0;
uint32_t valuetimer[] = {0,0,0,0,0,0,0,0,0,0};
uint32_t MAXTIMEOUT = 600*1000; //10 minutes
uint32_t eventtimer;
//********************************************
typedef struct message_struct {
  int message_id;
  float message_value;
  String message_description;
} message_struct;


void setup(void) {
  Serial.begin(115200); // For debug
  initSPIFFS();
  ssid = readFile(SPIFFS, ssidPath);
  pass = readFile(SPIFFS, passPath);

  tft.init();
  tft.setRotation(2);
  ledcSetup(pwmLedChannelTFT, pwmFreq, pwmResolution);
  ledcAttachPin(TFT_BL, pwmLedChannelTFT);
  ledcWrite(pwmLedChannelTFT, ledBacklight); 
  touch_calibrate();
  tft.fillScreen(TFT_BLACK);
  updateTime = millis(); // Next update time
  reconnectTime = millis();
  connectToNetwork();
  //webserversettings();
  InitESPNow();
  esp_now_register_recv_cb(OnDataRecv);

  gasfrontmin = readFile(SPIFFS, "/weightfrontmin.txt").toFloat(); //weight empty tank
  gasfrontmax = readFile(SPIFFS, "/weightfrontmax.txt").toFloat(); //weight full tank
  gasbackmin = readFile(SPIFFS, "/weightbackmin.txt").toFloat(); //weight empty tank
  gasbackmax = readFile(SPIFFS, "/weightbackmax.txt").toFloat(); //weight full tank
  watermin = readFile(SPIFFS, "/watermin.txt").toFloat();
  watermax = readFile(SPIFFS, "/watermax.txt").toFloat();
  water2min = readFile(SPIFFS, "/water2min.txt").toFloat();
  water2max = readFile(SPIFFS, "/water2max.txt").toFloat();
  valuetimer[2] = millis();
  valuetimer[3] = millis();
  valuetimer[4] = millis();
  valuetimer[5] = millis();
  valuetimer[6] = millis();
  valuetimer[7] = millis();
  eventtimer = millis();
  startscreen();
}

void loop() {
  if (!networkConnected and !setupmode){
    if (reconnectTime < millis()) {
      reconnectTime = millis() + 30000;
      WiFi.disconnect(true);
      Serial.println("start reconnect delay");
    } 
    else if (reconnectTime < millis()+ 10000) { // try reconnect for 10 sec
      Serial.println("reconnect to network");
      connectToNetwork();
    }
    delay(500);
  }
   uint16_t x, y;
  // See if there's any touch data for us
  if (tft.getTouch(&x, &y))
  {
    // Draw a block spot to show where touch was calculated to be
    #ifdef BLACK_SPOT
      tft.fillCircle(x, y, 2, TFT_GEEN);
    #endif
    Serial.print(x);
    Serial.print(":");
    Serial.println(y);
    if ((x > SETTINGBUTTON_X) && (x < (SETTINGBUTTON_X + SETTINGBUTTON_W))) {
      if ((y > SETTINGBUTTON_Y) && (y <= (SETTINGBUTTON_Y + SETTINGBUTTON_H))) {
        Serial.println("Setting btn hit");
        if (activescreen == 1)
        {
          settingscreen();
        } else
        {
            deletewifi = false;
            old_VALUE[0] = 0;   //reset needle for redraw
            old_VALUE[1] = 0;
            startscreen();
        }
      }
    }
    if ((x > PLUSBUTTON_X) && (x < (PLUSBUTTON_X + PLUSBUTTON_W))) {
      if ((y > PLUSBUTTON_Y) && (y <= (PLUSBUTTON_Y + PLUSBUTTON_H))) {
        Serial.println("Plus btn hit");
        if (ledBacklight < 255)
        {
          ledBacklight += 5;
          ledcWrite(pwmLedChannelTFT, ledBacklight);
        }
      }
    }
    if ((x > MINBUTTON_X) && (x < (MINBUTTON_X + MINBUTTON_W))) {
      if ((y > MINBUTTON_Y) && (y <= (MINBUTTON_Y + MINBUTTON_H))) {
        Serial.println("Minus btn hit");
        if (ledBacklight > 10) //not complete dark
        {
          ledBacklight -= 5;
          ledcWrite(pwmLedChannelTFT, ledBacklight);
        }
      }
    }
    if ((x > 0) && (x < 180) && networkConnected && (activescreen == 2)) {
      if ((y > 405) && (y <= 445)) {
        if (!deletewifi) {
          Serial.println("Delete WiFi btn hit 1st time");
          tft.setTextColor(TFT_RED, TFT_WHITE);
          tft.drawString("Verwijder WiFi verbinding", 10, 420, 1);
          deletewifi = true;
          delay(500);
          tft.setTextColor(TFT_WHITE, TFT_BLACK);
        } else
        {
          Serial.println("Delete WiFi btn hit 2nd time");
          delay(500);
          //empty SSID.txt and restart
          writeFile(SPIFFS, ssidPath, "");
          ESP.restart();
        }
      }
    }
  }
  if (activescreen == 1)
  {
    updatedata();
    sendeventsinfo();
  }
}

// #########################################################################
//  Process received data
// #########################################################################
void updatedata()
{
  //update waste water
  if ((millis() - valuetimer[5]) > (MAXTIMEOUT * 2)) // too long no new data
  {
    new_VALUE[5] = 0;
    old_VALUE[5] = 0;
    water2perc = 0;
    Serial.println("connection timeout 5");
    valuetimer[5]=millis();
  }
  if (new_VALUE[5] != old_VALUE[5])
  {
    valuetimer[5]=millis();
    //water2perc = 5*(map(new_VALUE[5], water2max, water2min, 20 , 0)); // map to percentage in steps of 5%
    water2perc = new_VALUE[5];
    if (water2perc < 0)
    {
      water2perc = 0;
    }else if (water2perc >100)
    {
      water2perc = 100;
    }
    old_VALUE[5] = new_VALUE[5];
  }
  fillwatertank(5, water2perc, X_POS[5], Y_POS[5],TFT_GREY);

  //update drinkwater
  if ((millis() - valuetimer[4]) > (MAXTIMEOUT * 2)) // too long no new data
  {
    new_VALUE[4] = 0;
    old_VALUE[4] = 0;
    waterperc = 0;
    Serial.println("connection timeout 4");
    valuetimer[4]=millis();
  }
  if (new_VALUE[4] != old_VALUE[4])
  {
    valuetimer[4]=millis();
    //waterperc = 5*(map(new_VALUE[4], watermax, watermin, 20 , 0)); // map to percentage in steps of 5%
    waterperc = new_VALUE[4];
    if (waterperc < 0)
    {
      waterperc = 0;
    }else if (waterperc >100)
    {
      waterperc = 100;
    }
    old_VALUE[4] = new_VALUE[4];
  }
  fillwatertank(4, waterperc, X_POS[4], Y_POS[4],TFT_BLUE);
  
  //update gastank front
  if ((millis() - valuetimer[2]) > MAXTIMEOUT) // too long no new data
  {
    new_VALUE[2] = 0;
    old_VALUE[2] = 0;
    gasfrontperc = 0;
    //rec_desc[2] = "X";
    Serial.println("connection timeout 2");
    valuetimer[2]=millis();
  }
  if (new_VALUE[2] != old_VALUE[2])
  {
    valuetimer[2]=millis();
    gasfrontperc = map(new_VALUE[2], gasfrontmin, gasfrontmax, 0 , 100); // map to percentage
    if (gasfrontperc < 0)
    {
      gasfrontperc = 0;
    }else if (gasfrontperc >100)
    {
      gasfrontperc = 100;
    }
    old_VALUE[2] = new_VALUE[2];
  }
  fillgastank(2, gasfrontperc, X_POS[2], Y_POS[2],rec_desc[2]);
  
  //update gastank back
  if ((millis() - valuetimer[3]) > MAXTIMEOUT) // too long no new data
  {
    new_VALUE[3] = 0;
    old_VALUE[3] = 0;
    gasbackperc = 0;
    //rec_desc[3] = "X";
    Serial.println("connection timeout 3");
    valuetimer[3]=millis();
  }
  if (new_VALUE[3] != old_VALUE[3])
  {
    valuetimer[3]=millis();
    gasbackperc = map(new_VALUE[3], gasbackmin, gasbackmax, 0 , 100); // map to percentage
    if (gasbackperc < 0)
    {
      gasbackperc = 0;
    }else if (gasbackperc >100)
    {
      gasbackperc = 100;
    }
    old_VALUE[3] = new_VALUE[3];
  }
  fillgastank(3, gasbackperc, X_POS[3], Y_POS[3],rec_desc[3]);
  
  //update temp out and hum out
  if ((millis() - valuetimer[6]) > MAXTIMEOUT) // too long no new data
  {
    filltemphum(6, 0, 0, X_POS[6], Y_POS[6]);
    new_VALUE[6] = 0;
    new_VALUE[8] = 0;
    old_VALUE[6] = 0;
    old_VALUE[8] = 0;
    Serial.println("connection timeout 6");
    valuetimer[6]=millis();
  }
  if ((new_VALUE[6] != old_VALUE[6])or (new_VALUE[8] != old_VALUE[8]))
  {
    valuetimer[6]=millis();
    filltemphum(6, new_VALUE[6], new_VALUE[8], X_POS[6], Y_POS[6]);
    old_VALUE[6] = new_VALUE[6];
    old_VALUE[8] = new_VALUE[8];
  }
  
  //update temp in and hum in
  if ((millis() - valuetimer[7]) > MAXTIMEOUT) // too long no new data
  {
    filltemphum(7, 0, 0, X_POS[7], Y_POS[7]);
    new_VALUE[7] = 0;
    new_VALUE[9] = 0;
    old_VALUE[7] = 0;
    old_VALUE[9] = 0;
    Serial.println("connection timeout 7");
    valuetimer[7]=millis();
  }
  if ((new_VALUE[7] != old_VALUE[7])or (new_VALUE[9] != old_VALUE[9]))
  {
    valuetimer[7]=millis();
    filltemphum(7, new_VALUE[7], new_VALUE[9], X_POS[7], Y_POS[7]);
    old_VALUE[7] = new_VALUE[7];
    old_VALUE[9] = new_VALUE[9];
  }

  //update voltage_1
  if ((millis() - valuetimer[0]) > MAXTIMEOUT) // too long no new data
  {
    plotNeedle(0, -9, 0, X_POS[0], Y_POS[0]);
    Serial.println("connection timeout 0");
    valuetimer[0]=millis();
    new_VALUE[0] = -9;
    old_VALUE[0] = -9;
  }
  if (new_VALUE[0] != old_VALUE[0])
  {
    valuetimer[0]=millis();
    plotNeedle(0, new_VALUE[0], 0, X_POS[0], Y_POS[0]);
    old_VALUE[0] = new_VALUE[0];
    delay(500);
    plotNeedle(0, new_VALUE[0], 0, X_POS[0], Y_POS[0]);
  }

  //update voltage_2
  if ((millis() - valuetimer[1]) > MAXTIMEOUT) // too long no new data
  {
    plotNeedle(1, -9, 0, X_POS[1], Y_POS[1]);
    Serial.println("connection timeout 1");
    valuetimer[1]=millis();
    new_VALUE[1] = -9;
    old_VALUE[1] = -9;
  }
  if (new_VALUE[1] != old_VALUE[1])
  {
    valuetimer[1]=millis();
    plotNeedle(1, new_VALUE[1], 0, X_POS[1], Y_POS[1]);
    old_VALUE[1] = new_VALUE[1];
    delay(500);
    plotNeedle(1, new_VALUE[1], 0, X_POS[1], Y_POS[1]);
  }
}

// #########################################################################
//  Main screen
// #########################################################################
void startscreen()
{
  activescreen = 1;
  setupmode = false;
  webserverinfo(); //information screen on webserver
  tft.fillScreen(TFT_BLACK);
  touchbuttons();
  temphum(6, X_POS[6], Y_POS[6], "Buiten");
  temphum(7, X_POS[7], Y_POS[7], "Binnen");
  analogMeter(X_POS[0], Y_POS[0], "motor");
  analogMeter(X_POS[1], Y_POS[1], "huishoud");
  gastank(2, X_POS[2], Y_POS[2], "voor");
  gastank(3, X_POS[3], Y_POS[3], "achter");
  watertank(4, X_POS[4], Y_POS[4], "schoon");
  watertank(5, X_POS[5], Y_POS[5], "vuil");
  
  //fill values
  plotNeedle(0, new_VALUE[0] + 1, 0, X_POS[0], Y_POS[0]); //little change to refresh
  plotNeedle(1, new_VALUE[1] + 1, 0, X_POS[1], Y_POS[1]);
  fillgastank(2, gasfrontperc, X_POS[2], Y_POS[2],rec_desc[2]);
  fillgastank(3, gasbackperc, X_POS[3], Y_POS[3],rec_desc[3]);
  filltemphum(6, new_VALUE[6], new_VALUE[8], X_POS[6], Y_POS[6]);
  filltemphum(7, new_VALUE[7], new_VALUE[9], X_POS[7], Y_POS[7]);
  fillwatertank(4, waterperc, X_POS[4], Y_POS[4],TFT_BLUE);
  fillwatertank(5, water2perc, X_POS[5], Y_POS[5],TFT_GREY);
}

// #########################################################################
//  Setting screen
// #########################################################################
void settingscreen()
{
  activescreen = 2;
  tft.fillScreen(TFT_BLACK);
  touchbuttons();
  setupmode = true; //when no connection to network, it stops trying to reconnect.
  if (!networkConnected){
    setupAP();
    tft.setTextSize(1);
    tft.drawString("Niet verbonden met WiFi netwerk", 0, 0, 2);
    tft.drawString("SSID CC-WIFI-MANAGER", 0, 20, 2);
    tft.drawString("http://192.168.4.1/", 0, 40, 2);
    tft.drawString("http://192.168.4.1/settings/", 0, 60, 2);
    tft.drawString("Configureer SSID", 0, 80, 2);
  } else {
    tft.setTextSize(1);
    tft.drawString("Verbonden met WiFi netwerk", 0, 0, 2);
    tft.drawString(WiFi.SSID(), 0, 20, 2);
    tft.setCursor(0, 40);
    tft.print(WiFi.localIP());
    tft.print("(/settings/)");
    //Delete WiFi connection button
    tft.setTextSize(1);
    tft.drawString("Verwijder WiFi verbinding", 10, 420, 1);
    tft.drawRoundRect(0,405,180,40, 10, TFT_WHITE);
  }
  webserversettings();
}

// #########################################################################
//  Information webserver and handler
// #########################################################################
void webserverinfo(){
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", "text/html", false, processorinfo);
  });

  // Handle Web Server Events
  events.onConnect([](AsyncEventSourceClient *client){
    Serial.println("client connected.");
    if(client->lastId()){
      Serial.printf("Client reconnected! Last message ID that it got is: %u\n", client->lastId());
    }
    // send event with message "hello!", id current millis
    // and set reconnect delay to 1 second
    client->send("hello!", NULL, millis(), 10000);
  });
  server.addHandler(&events);
  server.onNotFound(notFound);
  server.begin();
}

String processorinfo(const String& var){
  //Serial.println(var);
  char buf[15];
  float voltage;
  if(var == "TEMPIN"){
    return String(new_VALUE[7]);
  }
  else if(var == "TEMPOUT"){
    return String(new_VALUE[6]);
  }
  else if(var == "HUMIN"){
    return String(new_VALUE[9]);
  }
  else if(var == "HUMOUT"){
    return String(new_VALUE[8]);
  }
  else if(var == "WEIGHTFRONT"){
    return String(gasfrontperc);
  }
  else if(var == "WEIGHTBACK"){
    return String(gasbackperc);
  }
  else if(var == "WATER"){
    return String(waterperc);
  }
  else if(var == "WATER2"){
    return String(water2perc);
  }
  else if(var == "VOLTAGE1"){
    voltage = map(new_VALUE[0], 1, 99, 110 , 150); // convert percentage to scale 11 to 15
    dtostrf(voltage/10, 4, 1, buf);
    return String(buf);
  }
  else if(var == "VOLTAGE2"){
    voltage = map(new_VALUE[1], 1, 99, 110 , 150); // convert percentage to scale 11 to 15
    dtostrf(voltage/10, 4, 1, buf);
    return String(buf);
  }
}

void sendeventsinfo() {
  // Send Events to the Web Server with the Sensor Readings
  if (eventtimer < millis()){
    Serial.println("Update webpage.");
    char buf[15];
    float voltage;
    events.send("ping",NULL,millis());
    events.send(String(new_VALUE[7]).c_str(),"tempin",millis());
    events.send(String(new_VALUE[6]).c_str(),"tempout",millis());
    events.send(String(new_VALUE[9]).c_str(),"humin",millis());
    events.send(String(new_VALUE[8]).c_str(),"humout",millis());
    events.send(String(gasfrontperc).c_str(),"weightfront",millis());
    events.send(String(gasbackperc).c_str(),"weightback",millis());
    events.send(String(waterperc).c_str(),"water",millis());
    events.send(String(water2perc).c_str(),"water2",millis());
    voltage = map(new_VALUE[0], 1, 99, 110 , 150); // convert percentage to scale 11 to 15
    dtostrf(voltage/10, 4, 1, buf);
    //Serial.println(buf);
    events.send(String(buf).c_str(),"voltage1",millis());
    voltage = map(new_VALUE[1], 1, 99, 110 , 150); // convert percentage to scale 11 to 15
    dtostrf(voltage/10, 4, 1, buf);
    //Serial.println(buf);
    events.send(String(buf).c_str(),"voltage2",millis());
    eventtimer = millis() + 60000; // 1 minute delay tp prefent overload CPU.
  }
}

// #########################################################################
//  Settings webserver and handler
// #########################################################################
void webserversettings()
{
  // Send web page with input fields to client
  Serial.println("webserversettings");
  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/APindex.html", "text/html", false, processor);
  });

  // Send a GET request to <ESP_IP>/get?inputString=<inputMessage>
  server.on("/get", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String inputMessage;
    boolean re_start = false;
    int params = request->params();
    for(int i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if (p->name() == SSID_INPUT) {
        ssid = p->value().c_str();
        writeFile(SPIFFS, ssidPath, ssid.c_str());
      }
      if (p->name() == PASS_INPUT) {
        pass = p->value().c_str();
        writeFile(SPIFFS, passPath, pass.c_str());
      }
      re_start = true;
    }
    if (request->hasParam("watermin")) {
      inputMessage = request->getParam("watermin")->value();
      writeFile(SPIFFS, "/watermin.txt", inputMessage.c_str());
      re_start = true;
    }
    else if (request->hasParam("watermax")) {
      inputMessage = request->getParam("watermax")->value();
      writeFile(SPIFFS, "/watermax.txt", inputMessage.c_str());
      re_start = true;
    }
    else if (request->hasParam("water2min")) {
      inputMessage = request->getParam("water2min")->value();
      writeFile(SPIFFS, "/water2min.txt", inputMessage.c_str());
      re_start = true;
    }
    else if (request->hasParam("water2max")) {
      inputMessage = request->getParam("water2max")->value();
      writeFile(SPIFFS, "/water2max.txt", inputMessage.c_str());
      re_start = true;
    }
    else if (request->hasParam("weightfrontmin")) {
      inputMessage = request->getParam("weightfrontmin")->value();
      writeFile(SPIFFS, "/weightfrontmin.txt", inputMessage.c_str());
      re_start = true;
    }
    else if (request->hasParam("weightfrontmax")) {
      inputMessage = request->getParam("weightfrontmax")->value();
      writeFile(SPIFFS, "/weightfrontmax.txt", inputMessage.c_str());
      re_start = true;
    }
    else if (request->hasParam("weightbackmin")) {
      inputMessage = request->getParam("weightbackmin")->value();
      writeFile(SPIFFS, "/weightbackmin.txt", inputMessage.c_str());
      re_start = true;
    }
    else if (request->hasParam("weightbackmax")) {
      inputMessage = request->getParam("weightbackmax")->value();
      writeFile(SPIFFS, "/weightbackmax.txt", inputMessage.c_str());
      re_start = true;
    }
    else {
      inputMessage = "No message sent";
    }
    Serial.print("input from webpage: ");
    Serial.println(inputMessage.c_str());
    if (re_start){
      request->send(200, "text/plain", "Saved. Module will restart.");
      delay(3000);
      ESP.restart();
    }
  });
  server.onNotFound(notFound);
  server.begin();
}

void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}

// Replaces placeholder with stored values
String processor(const String& var){
  if(var == "water"){
    return String(new_VALUE[4]);
  }
  else if(var=="watermin"){
    return readFile(SPIFFS, "/watermin.txt");
  }
  else if(var=="watermax"){
    return readFile(SPIFFS, "/watermax.txt");
  }
  else if(var == "water2"){
    return String(new_VALUE[5]);
  }
  else if(var=="water2min"){
    return readFile(SPIFFS, "/water2min.txt");
  }
  else if(var=="water2max"){
    return readFile(SPIFFS, "/water2max.txt");
  }
  else if(var == "weightfront"){
    return String(new_VALUE[2]);
  }
  else if(var=="weightfrontmin"){
    return readFile(SPIFFS, "/weightfrontmin.txt");
  }
  else if(var=="weightfrontmax"){
    return readFile(SPIFFS, "/weightfrontmax.txt");
  }
  else if(var == "weightback"){
    return String(new_VALUE[3]);
  }
  else if(var=="weightbackmin"){
    return readFile(SPIFFS, "/weightbackmin.txt");
  }
  else if(var == "weightbackmax"){
    return readFile(SPIFFS, "/weightbackmax.txt");
  }
  return String();
}

void setupAP()
{
  Serial.println("Setting AP (Access Point)");
  WiFi.softAP("CC-WIFI-MANAGER", NULL);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
  String localip = String() + WiFi.softAPIP()[0] + "." + WiFi.softAPIP()[1] + "." + WiFi.softAPIP()[2] + "." + WiFi.softAPIP()[3];
}

// #########################################################################
//  Initialize SPIFFS
// #########################################################################
void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("An error has occurred while mounting SPIFFS");
  }
  else{
    Serial.println("SPIFFS mounted successfully");
  }
}

// #########################################################################
//  SPIFFS read file
// #########################################################################
String readFile(fs::FS &fs, const char * path){
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if(!file || file.isDirectory()){
    Serial.println("- failed to open file for reading");
    return String();
  }
  
  String fileContent;
  while(file.available()){
    fileContent = file.readStringUntil('\n');
    break;     
  }
  Serial.println(fileContent);
  return fileContent;
}

// #########################################################################
//  SPIFFS write file
// #########################################################################
void writeFile(fs::FS &fs, const char * path, const char * message){
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if(!file){
    Serial.println("- failed to open file for writing");
    return;
  }
  if(file.print(message)){
    Serial.println("- file written");
  } else {
    Serial.println("- frite failed");
  }
}

// #########################################################################
//  Connect to network
// #########################################################################
void connectToNetwork() {
  WiFi.disconnect(true);
  WiFi.onEvent(WiFiEvent);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid.c_str(), pass.c_str());
  WiFi.softAP("IoT_camper", NULL); //The sensor devices will connect to this AP name.
  delay(2000);
}

// #########################################################################
//  WiFi events
// #########################################################################
void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case SYSTEM_EVENT_STA_GOT_IP:
      networkConnected = true;
      Serial.println("connected in event");
      Serial.println(WiFi.localIP());
      Serial.print("Wi-Fi Channel: ");
      Serial.println(WiFi.channel());
      break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
      networkConnected = false;
      Serial.println("reconnecting in event");
      delay(10);
      connectToNetwork();
      break;
  }
}

// #########################################################################
//  Init ESP
// #########################################################################
void InitESPNow() {
  if (esp_now_init() == ESP_OK) {
    Serial.println("ESPNow Init Success");
  }
  else {
    Serial.println("ESPNow Init Failed");
    ESP.restart();
  }
}

// #########################################################################
//  Receive data
// #########################################################################
void OnDataRecv(const uint8_t * mac, const uint8_t *data, int len) {
  message_struct* sensormessage =(message_struct*) data;
  Serial.print(sensormessage->message_id);
  Serial.print(" : ");
  Serial.println(sensormessage->message_value);
  //Serial.print(sensormessage->message_description);
  //Serial.println("");
  int id = sensormessage->message_id;
  float value = sensormessage->message_value;
  String desc = sensormessage->message_description;
  switch(id) {  //10 devices V-meter1 [0], V-meter2 [1], gastank front [2], gastank back [3],
                //drink watertank [4], wasted watertank [5], temp out [6], temp in [7], hum out [8], hum in [9]
    case(11): //Voltage 1
      new_VALUE[0] = value;
      rec_desc[0] = desc;
      break;
    case(12): //Voltage 2
      new_VALUE[1] = value;
      rec_desc[1] = desc;
      break;
    case(21): //temp out
      new_VALUE[6] = value;
      rec_desc[6] = desc;
      break;
    case(22): //temp in
      new_VALUE[7] = value;
      rec_desc[7] = desc;
      break;
    case(31): //hum out
      new_VALUE[8] = value;
      rec_desc[8] = desc;
      break;
    case(32): //hum in
      new_VALUE[9] = value;
      rec_desc[9] = desc;
      break;
    case(41): //gasweight front
      new_VALUE[2] = value;
      rec_desc[2] = desc;
      break;
    case(42): //gasweight back
      new_VALUE[3] = value;
      rec_desc[3] = desc;
      break;
    case(51): //Drink water
      new_VALUE[4] = value;
      rec_desc[4] = desc;
      break;
    case(52): //Wasted water
      new_VALUE[5] = value;
      rec_desc[5] = desc;
      break;
  }
}
// #########################################################################
//  Draw functions
//
//  touchbuttons()
//  temphum(int DEVICE, int X_TOP, int Y_TOP, String desc)
//  filltemphum(int DEVICE, float temp, float hum, int X_TOP, int Y_TOP)
//  watertank(int DEVICE, int X_TOP, int Y_TOP, String desc)
//  fillwatertank(int DEVICE, int PERC, int X_TOP, int Y_TOP, int color)
//  gastank(int DEVICE, int X_TOP, int Y_TOP, String desc)
//  fillgastank(int DEVICE, int PERC, int X_TOP, int Y_TOP, String desc)
//  analogMeter(int X_TOP, int Y_TOP, String desc)
//  plotNeedle(int DEVICE, int value, byte ms_delay, int X_TOP, int Y_TOP)
// #########################################################################

// #########################################################################
//  Setings button and Screen led dim buttons
// #########################################################################
void touchbuttons()
{
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  //tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.pushImage(SETTINGBUTTON_X + 3, SETTINGBUTTON_Y + 3,  50, 50, Settingswhite);
  tft.drawRoundRect(SETTINGBUTTON_X,SETTINGBUTTON_Y,SETTINGBUTTON_W,SETTINGBUTTON_H, 10, TFT_WHITE); //Setings button
  tft.drawRoundRect(PLUSBUTTON_X,PLUSBUTTON_Y,PLUSBUTTON_W,PLUSBUTTON_H, 10, TFT_WHITE); // + button
  tft.setTextSize(6);
  tft.drawString("+", 278, 61, 1);
  tft.drawRoundRect(MINBUTTON_X,MINBUTTON_Y,MINBUTTON_W,MINBUTTON_H, 10, TFT_WHITE); // - button
  tft.drawString("-", 278, 116, 1);
  //tft.setTextColor(TFT_WHITE, TFT_BLACK);
}

// #########################################################################
//  Temperature and Humidity positions
// #########################################################################
void temphum(int DEVICE, int X_TOP, int Y_TOP, String desc)
{
  tft.setTextSize(2);
  tft.setCursor(X_TOP + 100, Y_TOP + 15);
  tft.print("o");
  tft.setCursor(X_TOP + 115, Y_TOP + 20);
  tft.setTextSize(3);
  tft.print("C");
  tft.setCursor(X_TOP + 230, Y_TOP + 20);
  tft.print("%");
  tft.setTextSize(1);
  tft.drawCentreString(desc, X_TOP + 140, Y_TOP, 2);
}

// #########################################################################
//  Temperature and Humidity values
// #########################################################################
void filltemphum(int DEVICE, float temp, float hum, int X_TOP, int Y_TOP)
{
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  int offset = 84; // drawRightString offset

  if (abs(temp) >= 100){temp = 99.9;}
  if (abs(hum) >= 100){hum = 99.9;}
  if (temp < 0)
  {
    temp = temp * (-1);
    tft.setTextSize(2);
    tft.drawString("-", X_TOP, Y_TOP + 25, 4);
  } else
  {
    tft.setTextSize(2);
    tft.drawString("  ", X_TOP, Y_TOP + 25, 4); //clear
  }
  String strtemp = String(temp, 1);
  String strhum = String(hum, 1);

  tft.setTextSize(3);
  if (strtemp.length() <= 3){tft.drawString("   ",X_TOP + 15 ,Y_TOP + 15 , 4);} //clear
  if (strhum.length() <= 3){tft.drawString("   ",X_TOP + 140 ,Y_TOP + 15 , 4);} //clear
  tft.drawRightString(strtemp.substring(0, strtemp.length() -2),X_TOP + offset + 15 ,Y_TOP + 15 , 4);
  tft.drawRightString(strhum.substring(0, strhum.length() -2),X_TOP + offset + 140,Y_TOP + 15 , 4);
  tft.setTextSize(1);
  tft.drawString(strtemp.substring(strtemp.length() -2),X_TOP + 100,Y_TOP + 37 + 15, 4);
  tft.drawString(strhum.substring(strhum.length() -2),X_TOP + 130 + 95,Y_TOP + 37 + 15, 4);
}

// #########################################################################
//  Draw the watertank on the screen
// #########################################################################
void watertank(int DEVICE, int X_TOP, int Y_TOP, String desc)
{
  tft.drawRect(X_TOP, Y_TOP, 10, 12, TFT_WHITE);
  tft.drawRect(X_TOP + 1, Y_TOP + 1, 8, 10, TFT_WHITE);
  tft.drawRect(X_TOP, Y_TOP + 10, 100, 54, TFT_WHITE);
  tft.drawRect(X_TOP + 1, Y_TOP + 11, 98, 52, TFT_WHITE);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.drawCentreString(desc, X_TOP + 50, Y_TOP + 65, 2);
}

// #########################################################################
//  Fill the water tank 0 (empty), 100 (full)
// #########################################################################
void fillwatertank(int DEVICE, int PERC, int X_TOP, int Y_TOP, int color)
{
  if (old_PERC[DEVICE] > PERC) //redraw in black
  {
    int x = old_PERC[DEVICE]/2 - PERC/2;
    tft.fillRect(X_TOP + 2, Y_TOP + 12 + 50 - old_PERC[DEVICE]/2, 96, x, TFT_BLACK);
  }
  tft.fillRect(X_TOP + 2, Y_TOP + 12 + 50 - PERC/2, 96, PERC/2, color);
  old_PERC[DEVICE] = PERC;
}

// #########################################################################
//  Draw the gastank on the screen
// #########################################################################
void gastank(int DEVICE, int X_TOP, int Y_TOP, String desc)
{
  tft.drawRoundRect(X_TOP, Y_TOP + 10, 50, 104, 10, TFT_WHITE);
  tft.drawRoundRect(X_TOP + 1, Y_TOP + 11, 48, 102, 10, TFT_GREEN);
  tft.drawRect(X_TOP + 20, Y_TOP, 10, 12, TFT_WHITE);
  tft.drawRect(X_TOP + 21, Y_TOP + 1, 8, 10, TFT_GREEN);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.drawCentreString(desc, X_TOP + 25, Y_TOP + 115, 2);
}

// #########################################################################
//  Fill the gas tank 0 (empty), 100 (full)
// #########################################################################
void fillgastank(int DEVICE, int PERC, int X_TOP, int Y_TOP, String desc)
{
  if (old_PERC[DEVICE] > PERC) //redraw in black
  {
    int x = old_PERC[DEVICE] - PERC;
    tft.fillRect(X_TOP + 2, Y_TOP + 12 + 100 - old_PERC[DEVICE], 46, x, TFT_BLACK);
  }
  tft.fillRect(X_TOP + 2, Y_TOP + 12 + 100 - PERC, 46, PERC, TFT_LIGHTGREY);
  old_PERC[DEVICE] = PERC;
  //redraw rounded corners gastank
  tft.drawRoundRect(X_TOP, Y_TOP + 10, 50, 104, 10, TFT_WHITE);
  tft.drawRoundRect(X_TOP + 1, Y_TOP + 11, 48, 102, 10, TFT_GREEN);

  if (desc == "X")
  {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawCentreString(desc, X_TOP + 27, Y_TOP + 50, 2);
  }
}


// #########################################################################
//  Draw the analogue Voltage meter on the screen. scale 11V - 15V
// #########################################################################
void analogMeter(int X_TOP, int Y_TOP, String desc)
{

  // Meter outline, x,y,w,h,color , x,y = left top
  tft.fillRect(X_TOP, Y_TOP, M_SIZE*239, M_SIZE*131, TFT_GREY);
  tft.fillRect(1 + X_TOP, M_SIZE*3 + Y_TOP, M_SIZE*234, M_SIZE*125, TFT_WHITE);

  tft.setTextColor(TFT_BLACK);  // Text colour
  tft.setTextSize(1);

  // Draw ticks every 5 degrees from -50 to +50 degrees (100 deg. FSD swing)
  for (int i = -50; i < 51; i += 5) {
    // Long scale tick length
    int tl = 15;

    // Coodinates of tick to draw
    float sx = cos((i - 90) * 0.0174532925);
    float sy = sin((i - 90) * 0.0174532925);
    uint16_t x0 = sx * (M_SIZE*100 + tl) + M_SIZE*120 + X_TOP;
    uint16_t y0 = sy * (M_SIZE*100 + tl) + M_SIZE*150 + Y_TOP;
    uint16_t x1 = sx * M_SIZE*100 + M_SIZE*120 + X_TOP;
    uint16_t y1 = sy * M_SIZE*100 + M_SIZE*150 + Y_TOP;

    // Coordinates of next tick for zone fill
    float sx2 = cos((i + 5 - 90) * 0.0174532925);
    float sy2 = sin((i + 5 - 90) * 0.0174532925);
    int x2 = sx2 * (M_SIZE*100 + tl) + M_SIZE*120 + X_TOP;
    int y2 = sy2 * (M_SIZE*100 + tl) + M_SIZE*150 + Y_TOP;
    int x3 = sx2 * M_SIZE*100 + M_SIZE*120 + X_TOP;
    int y3 = sy2 * M_SIZE*100 + M_SIZE*150 + Y_TOP;

    // Yellow zone limits
    if (i >= -50 && i < -35) {
      tft.fillTriangle(x0, y0, x1, y1, x2, y2, TFT_RED);
      tft.fillTriangle(x1, y1, x2, y2, x3, y3, TFT_RED);
    }

    // Green zone limits
    if (i >= -35 && i < 35) {
      tft.fillTriangle(x0, y0, x1, y1, x2, y2, TFT_GREEN);
      tft.fillTriangle(x1, y1, x2, y2, x3, y3, TFT_GREEN);
    }

    // Orange zone limits
    if (i >= 35 && i < 50) {
      tft.fillTriangle(x0, y0, x1, y1, x2, y2, TFT_RED);
      tft.fillTriangle(x1, y1, x2, y2, x3, y3, TFT_RED);
    }

    // Short scale tick length
    if (i % 25 != 0) tl = 8;

    // Recalculate coords incase tick lenght changed
    x0 = sx * (M_SIZE*100 + tl) + M_SIZE*120;
    y0 = sy * (M_SIZE*100 + tl) + M_SIZE*150;
    x1 = sx * M_SIZE*100 + M_SIZE*120;
    y1 = sy * M_SIZE*100 + M_SIZE*150;

    // Draw tick
    tft.drawLine(x0 + X_TOP, y0 + Y_TOP, x1 + X_TOP, y1 + Y_TOP, TFT_BLACK);

    // Check if labels should be drawn, with position tweaks
    if (i % 25 == 0) {
      // Calculate label positions
      x0 = (sx * (M_SIZE*100 + tl + 10) + M_SIZE*120) + X_TOP;
      y0 = (sy * (M_SIZE*100 + tl + 10) + M_SIZE*150) + Y_TOP;
      switch (i / 25) {
        case -2: tft.drawCentreString("11", x0+4, y0-4, 1); break;
        case -1: tft.drawCentreString("12", x0+2, y0, 1); break;
        case 0: tft.drawCentreString("13", x0, y0, 1); break;
        case 1: tft.drawCentreString("14", x0, y0, 1); break;
        case 2: tft.drawCentreString("15", x0-2, y0-4, 1); break;
      }
    }

    // Now draw the arc of the scale
    sx = cos((i + 5 - 90) * 0.0174532925);
    sy = sin((i + 5 - 90) * 0.0174532925);
    x0 = sx * M_SIZE*100 + M_SIZE*120;
    y0 = sy * M_SIZE*100 + M_SIZE*150;
    // Draw scale arc, don't draw the last part
    if (i < 50) tft.drawLine(x0 + X_TOP, y0 + Y_TOP, x1 + X_TOP, y1 + Y_TOP, TFT_BLACK);
  }

  tft.drawString("V", M_SIZE*(3 + 230 - 40) + X_TOP, M_SIZE*(119 - 20) + Y_TOP, 2); // Units at bottom right
  tft.drawCentreString("V", M_SIZE*120 + X_TOP, M_SIZE*75 + Y_TOP, 4); // Comment out to avoid font 4
  tft.drawRect(1 + X_TOP, M_SIZE*3 + Y_TOP, M_SIZE*236 + X_TOP, M_SIZE*126 + Y_TOP, TFT_BLACK); // Draw bezel line

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawCentreString(desc, X_TOP + (M_SIZE*239/2), Y_TOP + M_SIZE*131 + 5, 2);
  //plotNeedle(DEVICE, 0, 0, X_TOP, Y_TOP); // Put meter needle at 0
}

// #########################################################################
// Update needle position
// This function is blocking while needle moves, time depends on ms_delay
// 10ms minimises needle flicker if text is drawn within needle sweep area
// Smaller values OK if text not in sweep area, zero for instant movement but
// does not look realistic... (note: 100 increments for full scale deflection)
// value = 0 to 100 %
// #########################################################################
void plotNeedle(int DEVICE, int value, byte ms_delay, int X_TOP, int Y_TOP)
{
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setTextSize(1);
  //char buf[8];
  char buf[15];
  float voltage = map(value, 1, 99, 110 , 150); // convert percentage to scale 11 to 15
  //dtostrf(value, 4, 0, buf);
  dtostrf(voltage/10, 4, 1, buf);
  tft.drawRightString(buf, 33 + X_TOP, M_SIZE*(119 - 20) + Y_TOP, 2);

  if (value < -10) value = -10; // Limit value to emulate needle end stops
  if (value > 110) value = 110;
  
  // Move the needle until new value reached
  int i = old_PERC[DEVICE];
  while (!(value == i)) {
    if (i < value) i++;
    else i--;
    old_PERC[DEVICE] = i;

    if (ms_delay == 0) old_PERC[DEVICE] = value; // Update immediately if delay is 0

    float sdeg = map(old_PERC[DEVICE], -10, 110, -150, -30); // Map value to angle
    // Calculate tip of needle coords
    float sx = cos(sdeg * 0.0174532925);
    float sy = sin(sdeg * 0.0174532925);

    // Calculate x delta of needle start (does not start at pivot point)
    float tx = tan((sdeg + 90) * 0.0174532925);

    // Erase old needle image
    tft.drawLine((M_SIZE*(120 + 24 * ltx_POS[DEVICE])) + X_TOP - 1, (M_SIZE*(150 - 24)) + Y_TOP, osx_POS[DEVICE] - 1 + X_TOP, osy_POS[DEVICE] + Y_TOP, TFT_WHITE);
    tft.drawLine((M_SIZE*(120 + 24 * ltx_POS[DEVICE])) + X_TOP, (M_SIZE*(150 - 24)) + Y_TOP, osx_POS[DEVICE] + X_TOP, osy_POS[DEVICE] + Y_TOP, TFT_WHITE);
    tft.drawLine((M_SIZE*(120 + 24 * ltx_POS[DEVICE])) + X_TOP + 1, (M_SIZE*(150 - 24)) + Y_TOP, osx_POS[DEVICE] + 1 + X_TOP, osy_POS[DEVICE] + Y_TOP, TFT_WHITE);

    // Re-plot text under needle
    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.drawCentreString("V", (M_SIZE*(120)) + X_TOP, (M_SIZE*(75)) + Y_TOP, 4); // // Comment out to avoid font 4

    // Store new needle end coords for next erase
    ltx_POS[DEVICE] = tx;
    osx_POS[DEVICE] = M_SIZE*(sx * 98 + 120);
    osy_POS[DEVICE] = M_SIZE*(sy * 98 + 150);

    // Draw the needle in the new postion, magenta makes needle a bit bolder
    // draws 3 lines to thicken needle
    tft.drawLine((M_SIZE*(120 + 24 * ltx_POS[DEVICE])) + X_TOP - 1, (M_SIZE*(150 - 24)) + Y_TOP, osx_POS[DEVICE] - 1 + X_TOP, osy_POS[DEVICE] + Y_TOP, TFT_RED);
    tft.drawLine((M_SIZE*(120 + 24 * ltx_POS[DEVICE])) + X_TOP, (M_SIZE*(150 - 24)) + Y_TOP, osx_POS[DEVICE] + X_TOP, osy_POS[DEVICE] + Y_TOP, TFT_MAGENTA);
    tft.drawLine((M_SIZE*(120 + 24 * ltx_POS[DEVICE])) + X_TOP + 1, (M_SIZE*(150 - 24)) + Y_TOP, osx_POS[DEVICE] + 1 + X_TOP, osy_POS[DEVICE] + Y_TOP, TFT_RED);

    // Slow needle down slightly as it approaches new postion
    if (abs(old_PERC[DEVICE] - value) < 10) ms_delay += ms_delay / 5;

    // Wait before next update
    delay(ms_delay);
  }
}

// #########################################################################
//  Touchscreen calibration routine
// #########################################################################
void touch_calibrate()
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
