#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include "PubSubClient.h"
#include <ESP32_FTPClient.h>
#include "ModbusRtu.h"
#include <SPIFFS.h>
#include <FS.h>
#include "Spiffs_rw.h"
#include <esp_task_wdt.h>
#include <iostream>
#include <string>

#define WDT_TIMEOUT 30
#define led_connection 41
#define led_published 42

#define FORMAT_SPIFFS_IF_FAILED true
Modbus slave(1, Serial1, 0);

/////////////////////////Set Up Network////////////////////////

const char* ssid = "MIC_IIOT";
const char* password = "mic@admin";
const char* mqtt_server = "192.168.0.103";

IPAddress local_IP(192, 168, 0, 10);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);


char Machine_no[] = "BM165_146";    

char ftp_server[] = "192.168.0.103";
char ftp_user[]   = "admin";
char ftp_pass[]   = "1234";

ESP32_FTPClient ftp (ftp_server, ftp_user, ftp_pass, 5000, 2);

/////////////////////////Set Up Network////////////////////////

WiFiClient espClient;
PubSubClient client(espClient);

uint16_t prevDataState = 0;
int alarmCount = 0;
File file;
String output, output_end;
int lineCount, Year;

//////////////Setup////////////////////////////
const int maxAlarms = 500;  // จำนวนสูงสุดของรายการ alarm list
bool alarmStates[maxAlarms];
String alarmNames[maxAlarms] = {
  "",
  "",
  "",
};

String status_MC[8] = {
  "1",
  "2",
  "3",
  "4",
  "5",
  "6",
  "7",
  "8",
};

int bitIndex;
const int num = 55;   // จำนวน Data ทั้งหมด
const int alarm_coil = 30;
const int mc_coil = 8; //+6+11
#define mc_setup 30
#define mc_adjust 31
#define mc_chTool 32
#define mc_warm 33
#define mc_other 34
#define mc_stop 35
#define mc_run 36
#define mc_alarm 37

#define yy Data[38]
#define mm Data[39]
#define dd Data[40]
#define hh Data[41]
#define mn Data[42]
#define sec Data[43]
uint16_t Data[num];

int8_t state1 = 0;
unsigned long previousMillis = 0;
unsigned long previousMillis2 = 0;
unsigned long previousMillis3 = 0;
unsigned long previousMillis4 = 0;
const unsigned long interval = 100; //เวลาอ่าน Modbus
const unsigned long interval2 = 5000; //เวลาในการอ่านข้อมูลใน SPIFFS
const unsigned long interval3 = 2000; // Reconnect MQTT Time
const unsigned long interval4 = 60000; // FTP Time
//////////////เพิ่มเติม////////////////////////////
int8_t state = 0;
String  rssi, mc_status, actionyear, actionmonth, actiondate, actionhour, actionmin, actionsec , TARGET_SHIFT, TOTAL_OUT_SHIFT_A,TOTAL_OUT_SHIFT_B,TOTAL_OUT_SHIFT_C,YIELD_SHIFT_A,YIELD_SHIFT_B,YIELD_SHIFT_C,TRAY_1_COUNTER,TRAY_2_COUNTER,TRAY_3_COUNTER,TOTAL_TRAY ;
//char r[16], d0[16], d1[16], d2[16], d3[16], d4[16], d5[16], d6[16], d7[16], d8[16], d9[16], d10[16], d11[16], d12[16], d13[16], d14[16], d15[16], d16[16], d17[16], d18[16], d19[16]; // Add register ,d6[16],…….n;

void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info)
{
  switch (event) {
    case SYSTEM_EVENT_STA_CONNECTED:
      Serial.println("Connected to WiFi");
      Serial.println(WiFi.localIP());
      break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
      Serial.println("Disconnected from WiFi");
      break;
    default:
      break;
  }
}

void callback(char* topic, byte* payload, unsigned int length)
{
  esp_task_wdt_reset();
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

}

void reconnect()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    esp_task_wdt_reset();
    //Serial.println("Wifi connected");
    if (!client.connected())
    {
      if (client.connect("ESP32S2_Client")) {

      } else {
        Serial.print("Failed, rc=");
        Serial.print(client.state());
        Serial.println(" try again in 5 seconds");
        digitalWrite(led_published, LOW);
        //delay(5000);
      }
    } else {
      //Serial.println("MQTT connected");
    }
    client.loop();

  } else {
    Serial.println("Not connected");
    digitalWrite(led_published, LOW);
  }
}

void setup()
{
  Serial.begin(500000);
  Serial1.begin(115200);
  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);
  esp_task_wdt_reset();
  pinMode ( led_connection , OUTPUT);
  pinMode ( led_published, OUTPUT);
  delay(1000);
  WiFi.onEvent(WiFiEvent);
  WiFi.config(local_IP, gateway, subnet);
  WiFi.begin(ssid, password);
  ArduinoOTA.setHostname(Machine_no);

  // No authentication by default
  ArduinoOTA.setPassword("1234");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA
  .onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  })
  .onEnd([]() {
    Serial.println("\nEnd");
  })
  .onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  })
  .onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {  //FORMAT_SPIFFS_IF_FAILED
    Serial.println("SPIFFS Mount Failed");
    return;
  }

  //SPIFFS.format();                   // format file
  //deleteFile(SPIFFS, "/data.txt");   // Delete data.txt      //////////////////////////////////////
  listDir(SPIFFS, "/", 0);
  appendFile(SPIFFS, "/data.txt", "topic,occurred,restored,mark");
  appendFile(SPIFFS, "/mc_status.txt", "occurred,mc_status");
  loadAlarmStatesFromFile("/state.txt", alarmStates, alarm_coil);
  for (int i = 0; i < alarm_coil; i++)
  {
    Serial.print("data["); Serial.print(i); Serial.print("] : "); Serial.println(alarmStates[i]);
  }

  slave.start();

  client.setServer(mqtt_server, 1883); // MQTT broker address and port
  client.setCallback(callback);
}

void loop()
{
  state1 = slave.poll(Data, num);
  digitalWrite(led_published, HIGH);
  digitalWrite(led_connection, HIGH);
  esp_task_wdt_reset();
  unsigned long currentMillis = millis();
  Year = actionyear.toInt();
  if (Year > 0 && currentMillis - previousMillis >= interval && state1 >= 7 )
  {
    previousMillis = currentMillis;
    deleteEmptyLines("/data.txt");
    file = SPIFFS.open("/data.txt", "a");


    for (int i = 0; i < alarm_coil; i++)
    {
      int bitValue = (Data[i / 16] >> (i % 16)) & 1;  // อ่านค่า Bit ใน Coil
      //Serial.print("data["); Serial.print(i); Serial.print("] : "); Serial.println(alarmStates[i]);
      if (bitValue == 1 && !alarmStates[i])
      {
        bitIndex = i;
        alarmStates[i] = true;

        output = alarmNames[i] + "," + String(formatTime(yy, mm, dd, hh, mn, sec)) + ",";
        alarmCount++;
        //printAlarmList();
      }
      else if (bitValue == 0 && alarmStates[i])
      {
        alarmStates[i] = false;
        if (alarmCount > 0 && i >= 0 && i < maxAlarms)
        {
          bitIndex = i;
          output_end = formatTime(yy, mm, dd, hh, mn, sec);

          insertDataAfterPrefix("/data.txt", alarmNames[bitIndex].c_str(), (output_end + ",*").c_str());
          goto loop1;

        }
      }
    }

loop1: delay(1);
    saveAlarmStatesToFile("/state.txt", alarmStates, alarm_coil);
    for (int i = alarm_coil; i <= (alarm_coil + mc_coil); i++)
    {
      int bitValue = (Data[i / 16] >> (i % 16)) & 1;  // อ่านค่า Bit ใน Coil
      //Serial.print(i);Serial.print(" : ");Serial.println(bitValue);
      if (bitValue == 1 && !alarmStates[i])
      {

        if ( i == mc_setup ) {
          alarmStates[i] = true;
          printMachineStatus(status_MC[0]);
        }
        if ( i == mc_adjust ) {
          alarmStates[i] = true;
          printMachineStatus(status_MC[1]);
        }
        if ( i == mc_chTool ) {
          alarmStates[i] = true;
          printMachineStatus(status_MC[2]);
        }
        if ( i == mc_warm ) {
          alarmStates[i] = true;
          printMachineStatus(status_MC[3]);
        }
        if ( i == mc_other ) {
          alarmStates[i] = true;
          printMachineStatus(status_MC[4]);
        }

        if ( i == mc_stop ) {
          alarmStates[i] = true;
          printMachineStatus(status_MC[5]);
        }

          if ( i == mc_run) {
          alarmStates[i] = true;
          printMachineStatus(status_MC[6]);
        }

          if ( i == mc_alarm ) {
          alarmStates[i] = true;
          printMachineStatus(status_MC[7]);
        }
      }
      if (bitValue == 0 && alarmStates[i])
      {
        alarmStates[i] = false;
      }

    }
    //////////Counter/////////////////
    esp_task_wdt_reset();

  }
  if (currentMillis - previousMillis2 >= interval2 )
  {
    previousMillis2 = currentMillis;
    esp_task_wdt_reset();
    String rssi = String(WiFi.RSSI());

    ///////////////////////////////////////////////////

    //mc_status = String("Not");  Serial.print("MC_Status : ");        Serial.println(mc_status);
    actionyear = String(yy);  Serial.print("YY : ");              Serial.println(actionyear);
    actionmonth = String(mm);  Serial.print("MM : ");             Serial.println(actionmonth);
    actiondate = String(dd);  Serial.print("DD : ");              Serial.println(actiondate);
    actionhour = String(hh);  Serial.print("HH : ");              Serial.println(actionhour);
    actionmin = String(mn);  Serial.print("mm : ");               Serial.println(actionmin);
    actionsec = String(sec);  Serial.print("ss : ");               Serial.println(actionsec);
    TARGET_SHIFT         = String(Data[44]);  Serial.print("TARGET : ");             Serial.println(TARGET_SHIFT);
    TOTAL_OUT_SHIFT_A    = String(Data[45]);  Serial.print("TL_OUT_A : ");           Serial.println(TOTAL_OUT_SHIFT_A);
    TOTAL_OUT_SHIFT_B    = String(Data[46]);  Serial.print("TL_OUT_B  : ");          Serial.println(TOTAL_OUT_SHIFT_B);
    TOTAL_OUT_SHIFT_C    = String(Data[47]);  Serial.print("TL_OUT_C  : ");          Serial.println(TOTAL_OUT_SHIFT_C);
    YIELD_SHIFT_A        = String(Data[48]);  Serial.print("YIELD_A : ");            Serial.println(YIELD_SHIFT_A);
    YIELD_SHIFT_B        = String(Data[49]);  Serial.print("YIELD_B  : ");           Serial.println(YIELD_SHIFT_B);
    YIELD_SHIFT_C     = String(Data[50]);  Serial.print("YIELD_C  : ");              Serial.println(YIELD_SHIFT_C);
    TRAY_1_COUNTER    = String(Data[51]);  Serial.print("TRAY_1_COUNT  : ");          Serial.println(TRAY_1_COUNTER);
    TRAY_2_COUNTER    = String(Data[52]);  Serial.print("TRAY_2_COUNT  : ");          Serial.println(TRAY_2_COUNTER);
    TRAY_3_COUNTER    = String(Data[53]);  Serial.print("TRAY_3_COUNT  : ");          Serial.println(TRAY_3_COUNTER);
    TOTAL_TRAY        = String(Data[54]);  Serial.print("TL_TRAY : ");                Serial.println(TOTAL_TRAY);



    ///////////////////////////////////////////////////////////////////

    esp_task_wdt_reset();
    digitalWrite(led_published, LOW);

    rssi = WiFi.RSSI(); // WiFi strength
    Serial.print("rssi : "); Serial.println(WiFi.RSSI());
    // สร้าง JSON object
    StaticJsonDocument<5000> doc;

    doc["rssi"]                     = rssi;
    doc["MC_Status"]                = mc_status;
    doc["YY"]                  = actionyear;
    doc["MM"]                  = actionmonth;
    doc["DD"]                  = actiondate;
    doc["HH"]                  = actionhour;
    doc["mm"]                  = actionmin;
    doc["ss"]                  = actionsec;
    doc["Target"]       = TARGET_SHIFT;
    doc["TL_OUT_A"]      = TOTAL_OUT_SHIFT_A;
    doc["TL_OUT_B"]      = TOTAL_OUT_SHIFT_B;
    doc["TL_OUT_C"]      = TOTAL_OUT_SHIFT_C;
    doc["YIELD_A"]       = YIELD_SHIFT_A;
    doc["YIELD_B"]       = YIELD_SHIFT_B;
    doc["YIELD_C"]       = YIELD_SHIFT_C;
    doc["TRAY_1_COUNT"]  = TRAY_1_COUNTER;
    doc["TRAY_2_COUNT"]  = TRAY_2_COUNTER;
    doc["TRAY_3_COUNT"]  = TRAY_3_COUNTER;
    doc["TL_TRAY"]       = TOTAL_TRAY;

    // แปลง JSON object เป็น string
    String jsonStr;
    serializeJson(doc, jsonStr);

    // ส่งข้อมูลผ่าน MQTT
    client.publish("mic/test/BM165_146", jsonStr.c_str());
    Serial.println(jsonStr);

    Serial.println("\n---------------finish loop------------------\n\n");
    deleteEmptyLines("/data.txt");
    Serial.println("-----------------Read data.txt-----------------");
    readFile(SPIFFS, "/data.txt");
    Serial.println("-----------------Read data.txt-----------------");
    lineCount = countLines("/data.txt");
    Serial.print("Number of lines in file: ");
    alarmCount = lineCount;
    Serial.println(lineCount);

    deleteEmptyLines("/mc_status.txt");
    Serial.println("-----------------mc_status.txt-----------------");
    readFile(SPIFFS, "/mc_status.txt");
    Serial.println("-----------------mc_status.txt-----------------");
    lineCount = countLines("/mc_status.txt");
    Serial.print("Number of lines in file: ");
    alarmCount = lineCount;
    Serial.println(lineCount);

    /*Serial.println("-----------------state.txt-----------------");
      readFile(SPIFFS, "/state.txt");
      Serial.println("-----------------state.txt-----------------");*/

    listDir(SPIFFS, "/", 0);

  }

  if (currentMillis - previousMillis4 >= interval4 )  ////////////////Sent to FTP
  {
    previousMillis4 = currentMillis;
    if (WiFi.status() == WL_CONNECTED)
    {
      esp_task_wdt_reset();
      sent_ftp_mc();
    }
  }

  if (currentMillis - previousMillis3 >= interval3 )   ///////////////////reconnect
  {
    previousMillis3 = currentMillis;
    reconnect() ;
  }
  int h = actionhour.toInt();
  int m = actionmin.toInt();
  int s = actionsec.toInt();
  if (h == 7 && m == 0 && s <= 20 )
  {
    SPIFFS.format();
    Serial.println("*****************************");
    ESP.restart();
  }
}

void printMachineStatus(String status_mc)
{
  int lineCount3 = countLines("/mc_status.txt");
  String output_nc = (formatTime(yy, mm, dd, hh, mn, sec)) + "," + status_mc;

  if (lineCount3 >= 2 )
  {
    appendToLine_MC(SPIFFS, "/mc_status.txt", lineCount3, output_nc.c_str(), status_mc.c_str() );
  }
}



void sent_ftp_mc()
{
  String mc_name_file = "gmma_tn_mcstatus_" + String(Machine_no) + ".txt";
  lineCount = countLines("/mc_status.txt");
  int Count4 = lineCount;
  if (Count4 > 2)
  {
    ftp.OpenConnection();
    File dataFile2 = SPIFFS.open("/mc_status.txt", "r");
    if (dataFile2) {
      String content2 = dataFile2.readString();
      dataFile2.close();
      if (content2.length() > 0) {
        // Upload the content to the FTP server
        ftp.InitFile("Type A");
        ftp.ChangeWorkDir("/data_mcstatus/tn/"); // Change this to your remote directory    ////data_mcstatus\tn
        ftp.NewFile(mc_name_file.c_str());
        ftp.Write(content2.c_str());
        ftp.CloseFile();
      }
    } else {
      Serial.println("Failed to open mc_status.txt for reading");
    }

    ftp.CloseConnection();
  } else
  {
    Serial.println("Failed to open gmma_tn_mcstatus_BM165_146.txt Empty");
    delay(1000);
  }
}
