#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>

/*
 * Version <22.10.27>			@Rifat
 * Dropsensor status code added
 * motor 8 is freed and can be used now
 * Version info:
	  1 -> Jyoti Lite
	  2 -> Jyoti Pro
  	3 -> Jyoti Standard
  	4 -> Jyoti Mega
 */

const char* Version = "1.2";  //

//Firmware url: Current Source- Github
#define URL_fw_Bin "https://raw.githubusercontent.com/RnD-VIL/OTA_Firmware_Jyoti/main/motor_control_firmware.bin"

//File server info
const char* host = "https://raw.githubusercontent.com";
const int httpsPort = 443;

//WiFi info

// const char* ssid = "VerticalInv";   //ssid of your wifi
// const char* password = "@VIL@123";  //wifi password

const char* ssid = "VERTICAL";        //ssid of your wifi
const char* password = "viljyotiru";  //wifi password

//Collect the RU tag
String ru_tag = "Kapasia_HPGHS_01";                 //Unique RU tag. Source: Dash.vertical-innovations.com
const char* ping_topic = "28jyotipingtran";  //Ping topic: || 23jyotipingtran for dhaka || 19jyotipingtran for CTG || 52jyotipingtran for Narayanganj

#define drop_sensor_ideal_status 1   //for new dropsensor, tx and rx module housed in same body
#define drop_sensor_active_status 0  //for new dropsensor, tx and rx module housed in same body

//#define drop_sensor_ideal_status 0   //for old dropsensor, tx and rx module facing eachother
//#define drop_sensor_active_status 1  //for old dropsensor, tx and rx module facing eachother

float mag_spring_conf[9] = {
  0.0,  // 0 index is ignored
  1.0,  // Number of springs in magazine 1
  1.0,  // Number of springs in magazine 2
  1.0,  // Number of springs in magazine 3
  1.0,  // Number of springs in magazine 4
  1.0,  // Number of springs in magazine 5
  1.0,  // Number of springs in magazine 6
  1.0,  // Number of springs in magazine 7
  1.0   // Number of springs in magazine 8
};
// **********************************************************
// *************** Motor Auto Shutdown ************
// **********************************************************

unsigned long max_motor_run_period = 15000;  //Unit is in milli seconds
unsigned long min_motor_run_period = 100;    //Unit is in milli seconds

// **********************************************************
// *************** Error Status Codes ************
// **********************************************************

#define OK 200                 // Working as it should be
#define spring_error 101       // Spring rotating for more than define period
#define drop_sensor_error 103  // constantly getting feedback

//flags
bool spring_error_flag = false;
bool OK_flag = true;
bool drop_sensor_flag = false;  //automatically receiving feedback from drop sensor

// **********************************************************
// *************** Transection ID maximum retry count ************
// **********************************************************

#define tran_id_max_retry 5  //

// **********************************************************
// *************** Motor Auto Shutdown ************
// **********************************************************
int ds_count;  // = 0; // Drop Sensor active status counter. retries after every 200 milli seconds

// **********************************************************
// *************** DO NOT EDIT AFTER THIS POINT ************
// **********************************************************

String newHostname = "M_" + ru_tag;

// **********************************************************
// *************** Watch Dog Timer ************
// **********************************************************

volatile int tick_ping = 0;   //counter for error ping count
volatile int tick_motor = 0;  //counter for long motor run
volatile int tick_modem = 0;  // counter for error in modem connectivty
volatile int tick_sntp = 0;   // counter for sntp error (indicates no internet data)

// Define the pins
//the relays connect to
#define enable_pin D0
#define selector_a D1
#define selector_b D2
#define selector_c D3


#define motor_feedback_pin D6
#define buzzer_pin D7
#define indicator D5
#define modem_pin D4

//
#define ON 0
#define OFF 1
int motor_feedback;
//int barrel;
int motor_no_init;
//int running_motor_num;

// **********************************************************
// *************** MQTT Credentials. ************
// **********************************************************

const char* mqtt_server = "prohorii.vertical-innovations.com";
String content = "";
char msg[300];
char msgo[300];

/*--------- Newly added@Rifat ----------*/
char dmsg[300];
char rmsg[300];
/*--------- Newly added@Rifat ----------*/

//MQTT info
String dispenseTopic = ru_tag + "res";
String pingTranTopic = ru_tag + "ptres";
String clientId = ru_tag + "motor";

//OTA Topics
String pingOtaTopic = "testjyotipingtran";                    //Need suggestion on where the OTA should ping, given that device already ping at jyotipingtran
String OtaVerCheckTopic = ru_tag + "motor" + "update";        //Subscribed to Ota version check. If given version doesn't match with current version, an update will occur
String OtaUpdateInfoTopic = ru_tag + "motor" + "updateinfo";  //Publish Ota Update info

BearSSL::WiFiClientSecure net;

//Vertical innovations website certificate
const char* x509CA PROGMEM = R"EOF("
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----

")EOF";
BearSSL::X509List x509(x509CA);


// DigiCert High Assurance EV Root CA certificate (github)
const char trustRoot[] PROGMEM = R"EOF("
-----BEGIN CERTIFICATE-----
MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdWrJWZHHSjANBgkqhkiG9w0BAQUFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBD
QTAeFw0wNjExMTAwMDAwMDBaFw0zMTExMTAwMDAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IENBMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEA4jvhEXLeqKTTo1eqUKKPC3eQyaKl7hLOllsB
CSDMAZOnTjC3U/dDxGkAV53ijSLdhwZAAIEJzs4bg7/fzTtxRuLWZscFs3YnFo97
nh6Vfe63SKMI2tavegw5BmV/Sl0fvBf4q77uKNd0f3p4mVmFaG5cIzJLv07A6Fpt
43C/dxC//AH2hdmoRBBYMql1GNXRor5H4idq9Joz+EkIYIvUX7Q6hL+hqkpMfT7P
T19sdl6gSzeRntwi5m3OFBqOasv+zbMUZBfHWymeMr/y7vrTC0LUq7dBMtoM1O/4
gdW7jVg/tRvoSSiicNoxBN33shbyTApOB6jtSj1etX+jkMOvJwIDAQABo2MwYTAO
BgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUA95QNVbR
TLtm8KPiGxvDl7I90VUwHwYDVR0jBBgwFoAUA95QNVbRTLtm8KPiGxvDl7I90VUw
DQYJKoZIhvcNAQEFBQADggEBAMucN6pIExIK+t1EnE9SsPTfrgT1eXkIoyQY/Esr
hMAtudXH/vTBH1jLuG2cenTnmCmrEbXjcKChzUyImZOMkXDiqw8cvpOp/2PV5Adg
06O/nVsJ8dWO41P0jmP6P6fbtGbfYmbW0W5BjfIttep3Sp+dWOIrWcBAI+0tKIJF
PnlUkiaY4IBIqDfv8NZ5YBberOgOzW6sRBc4L0na4UU+Krk2U886UAb3LujEV0ls
YSEY1QSteDwsOoBrp+uvFRTp2InBuThs4pFsiv9kuXclVzDAGySj4dzp30d8tbQk
CAUw7C29C79Fv1C5qfPrmAESrciIxpg0X40KPMbp1ZWVbd4=
-----END CERTIFICATE-----

")EOF";
BearSSL::X509List trustRoot_ca(trustRoot);

PubSubClient client(net);

unsigned long lastMsg = 0;
unsigned long lastPing = 0;
unsigned long lastOtaMsg = 0;
unsigned long lastOtaPing = 0;

//JSON object handing

int magazine;     // = doc["mag"]; // 1
const char* res;  // = doc["res"]; // 1

int qty;            // = doc["qty"]; // 1
int Info;           // = doc["info"]; // 1
const char* id;     // = doc["id"]; // "135"
int disabled;       // = doc["disabled"]; // 0
int capacity;       //Maximum Capacity of the magazine. Configured at server side
int remaining_qty;  //Current inventory in magazine
int res_flag = 0;   // doc["flag"]; // 0,1,2

// **********************************************************
// *************** DO NOT EDIT THIS FILE ************
// **********************************************************

void buzzer() {
  digitalWrite(buzzer_pin, HIGH);
  delay(2000);
  digitalWrite(buzzer_pin, LOW);
}

void indicator_on() {
  digitalWrite(indicator, LOW);  //GREEN LED ON
}

void indicator_off() {
  digitalWrite(indicator, HIGH);  //RED LED ON
}

void black_out() {
  digitalWrite(indicator, LOW);

  digitalWrite(buzzer_pin, LOW);
}

void modem_toggle() {
  indicator_off();
  //Normally Connected pin of relay is connected to modem
  digitalWrite(modem_pin, ON);
  delay(60000);
  digitalWrite(modem_pin, OFF);
}

void relay_init(void) {  //initialize the relay
  //set all the relays OUTPUT
  pinMode(enable_pin, OUTPUT);
  pinMode(selector_c, OUTPUT);
  pinMode(selector_b, OUTPUT);
  pinMode(selector_a, OUTPUT);
  digitalWrite(enable_pin, HIGH);

  relay_SetStatus(OFF, OFF, OFF, OFF);  //turn off all the relay
}

void relay_SetStatus(unsigned char status_1, unsigned char status_2, unsigned char status_3, unsigned char status_4) {
  digitalWrite(selector_a, status_1);
  digitalWrite(selector_b, status_2);
  digitalWrite(selector_c, status_3);
  digitalWrite(enable_pin, status_4);
}

void motor_run(int mag_num) {
  if (mag_num == 1)
    relay_SetStatus(ON, ON, ON, ON);  //turn on RELAY_1
  if (mag_num == 2)
    relay_SetStatus(OFF, ON, ON, ON);  //turn on RELAY_2
  if (mag_num == 3)
    relay_SetStatus(ON, OFF, ON, ON);  //turn on RELAY_3
  if (mag_num == 4)
    relay_SetStatus(OFF, OFF, ON, ON);  //turn on RELAY_4
  if (mag_num == 5)
    relay_SetStatus(ON, ON, OFF, ON);  //turn on RELAY_5
  if (mag_num == 6)
    relay_SetStatus(OFF, ON, OFF, ON);  //turn on RELAY_6
  if (mag_num == 7)
    relay_SetStatus(ON, OFF, OFF, ON);  //turn on RELAY_7
  if (mag_num == 8)
    relay_SetStatus(OFF, OFF, OFF, ON);  //turn on RELAY_8
}

void product_dispense(int mag_number, int qty, int capacity, int remaining_qty) {
  // int mag_number :: Magazine number
  // int qty :: Quantity, Number of product(s) to be dispensed
  // int capacity :: Maximum capacity of the magazine. Server side configuration
  // int remaining_qty :: Remaining quantiy in the magazine.

  for (; qty > 0; qty--) {
    float magazine_spring = ceil((remaining_qty * mag_spring_conf[mag_number]) / capacity);
    Serial.printf("magazine_spring before CEIL: %2.2f\n", magazine_spring);
    motor_no_init = int(magazine_spring);

    for (int j = 0; j < mag_number; j++) {
      motor_no_init = motor_no_init + int(mag_spring_conf[j]);
    }

    Serial.printf("motor_no_init %d\n", motor_no_init);

    unsigned long previousMillis = millis();
    motor_run(motor_no_init);
    motor_feedback = digitalRead(motor_feedback_pin);

    //Serial.print("Running Motor");
    //Serial.println(running_motor_num);
    delay(100);

    //unsigned long currentMillis = 0; //millis();
    //delay(1000);
    bool motor_off_flag = false;
    Serial.println(previousMillis);
    Serial.println(millis());
    while (motor_feedback == drop_sensor_ideal_status)  // ||  millis() - previousMillis < max_motor_run_period ) //At normal condition, dropsensor sends 1 for new feedback 6-Dec-21
    {
      Serial.print("Printing motor feedback in loop ");
      Serial.println(motor_feedback);
      motor_feedback = digitalRead(motor_feedback_pin);
      yield();
      //currentMillis = millis();
      // Serial.println(millis() - previousMillis < max_motor_run_period);
      if (millis() - previousMillis >= max_motor_run_period) {
        motor_feedback = drop_sensor_active_status;
        Serial.println("Spring Error");
        spring_error_flag = true;
        OK_flag = false;
      }
    }

    if (millis() - previousMillis < min_motor_run_period) {

      // motor_feedback = drop_sensor_active_status;
      Serial.println("Into drop sensor block");
      drop_sensor_flag = true;  //Automically receiving drop sensor feedback
      OK_flag = false;
    }
    delay(10);
    relay_SetStatus(OFF, OFF, OFF, OFF);  //All Relay OFF (Deactivate Demux)
    remaining_qty = remaining_qty - 1;
    delay(1000);
    Serial.println();
    Serial.print("Printing motor feedback after the loop ");
    Serial.println(motor_feedback);
    motor_feedback = drop_sensor_active_status;
    delay(10);
  }
  buzzer();
}

void wifi_status(int status_code) {
  switch (status_code) {
    case 0:
      {
        Serial.println("WiFi is in process of changing between statuses");
        break;
      }
    case 1:
      {
        Serial.println("SSID cannot be reached");
        break;
      }
    case 2:
      {
        Serial.println("Scan Completed");
        break;
      }
    case 3:
      {
        Serial.println("Successful connection is established");
        break;
      }
    case 4:
      {
        Serial.println("Password is incorrect");
        break;
      }
    case 5:
      {
        Serial.println("Connection Lost");
        break;
      }
    case 6:
      {
        Serial.println("Module is disconnected or not configured in station mode");
        break;
      }
  }
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("MOTOR Module :");
  Serial.println(ru_tag);

  Serial.println();
  Serial.print("Connecting to \"");
  Serial.print(ssid);

  Serial.print("\" and password \"");
  Serial.print(password);
  Serial.println("\"");

  WiFi.mode(WIFI_STA);
  //WiFi.hostname("ESP-host");
  WiFi.hostname(newHostname.c_str());  //Sets the host name to indentify the divie in network
  WiFi.begin(ssid, password);
  Serial.println();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    wifi_status(WiFi.status());
    tick_modem = tick_modem + 1;
    Serial.printf("Tick modem count %d of 120 \n", tick_modem);

    if (tick_modem == 120)  //wait for 60s to connect to modem
    {
      // Serial.print(WiFi.status()); Serial.print(' ');
      Serial.println("");
      Serial.println("Wait 60s");
      modem_toggle();
      Serial.println("ESP Resets");
      ESP.reset();
    }
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("RSSI: ");
  Serial.println(WiFi.RSSI());
}

void setup_time() {
  Serial.println("Setting time using SNTP");
  configTime(-5 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = time(nullptr);
  while (now < 1510592825) {
    delay(500);
    tick_sntp = tick_sntp + 1;
    Serial.printf("Tick sntp count %d of 120\n", tick_sntp);

    if (tick_sntp == 120)  //wait for 60s to get sntp.
    {
      Serial.println("Wait 60s");
      modem_toggle();
      Serial.println("ESP Resets");
      ESP.reset();
    }
    now = time(nullptr);
  }
  Serial.println("done!");
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.print("Current time: ");
  Serial.print(asctime(&timeinfo));
}

void FirmwareUpdate() {
  net.setTrustAnchors(&trustRoot_ca);
  if (!net.connect(host, httpsPort)) {
    Serial.println("Connection failed");
    return;
  } else {
    Serial.println("Connected to server!");
  }
  t_httpUpdate_return ret = ESPhttpUpdate.update(net, URL_fw_Bin);  //Download update from given url and flash the nodemcu

  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
      break;

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("HTTP_UPDATE_NO_UPDATES");
      break;

    case HTTP_UPDATE_OK:
      Serial.println("HTTP_UPDATE_OK");
      break;
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String dispenseString = "";
  if (strcmp(topic, dispenseTopic.c_str()) == 0) {
    for (int i = 0; i < length; i++) {
      Serial.print((char)payload[i]);
      dispenseString += (String)(char)payload[i];
    }
    Serial.println(dispenseString);
    if (dispenseString == "RFID deoesn't exist") {
      Serial.println("play song  ONE");
    }

    const size_t bufferSize = JSON_OBJECT_SIZE(5) + 40;
    DynamicJsonBuffer jsonBuffer(bufferSize);
    JsonObject& doc = jsonBuffer.parseObject(payload);

    magazine = doc["mag"];                 // 1
    qty = doc["qty"];                      // 1
    Info = doc["info"];                    // 1
    id = doc["id"];                        // "135"
    disabled = doc["disabled"];            // 0
    capacity = doc["capacity"];            // 0
    remaining_qty = doc["remaining_qty"];  // 0

    if (magazine > 0) {
      Serial.println(magazine);
      product_dispense(magazine, qty, capacity, remaining_qty);

      //buzzer_on();

      delay(950);

      const size_t capacity = JSON_OBJECT_SIZE(2) + 50;
      DynamicJsonBuffer jsonBuffer(capacity);
      JsonObject& roott = jsonBuffer.createObject();
      roott["ru"] = ru_tag;
      roott["tran_id"] = id;
      roott["status_code"] = OK;
      if (spring_error_flag) {
        roott["status_code"] = spring_error;
        Serial.println(spring_error);
        spring_error_flag = false;
        drop_sensor_flag = false;
        OK_flag = true;
        delay(2000);
      }
      if (drop_sensor_flag) {
        Serial.println(drop_sensor_error);
        roott["status_code"] = drop_sensor_error;
        drop_sensor_flag = false;
        spring_error_flag = false;
        OK_flag = true;
        delay(200);
      }

      roott.prettyPrintTo(msgo, sizeof(msgo));
      Serial.println(msgo);

      if (client.publish("jyotitranidpost", msgo)) {
        Serial.println("Tran Id Posted");
        roott["status_code"] = OK;
      } else {
        int tran_id_post_count = 0;
        Serial.println("Tran Id Post Error.");
        Serial.print("Error Count: ");
        Serial.println(tran_id_post_count);
        bool tran_id_succes_status_flag = false;
        while (!tran_id_succes_status_flag) {
          if (client.publish("jyotitranidpost", msgo)) {
            Serial.println("Tran Id Posted");
            tran_id_succes_status_flag = true;
            Serial.println("Tran Id Post Error.");
          } else {
            tran_id_post_count++;
            if (tran_id_post_count >= tran_id_max_retry) {
              tran_id_succes_status_flag = true;
              Serial.println("Tran Id Post Error.");
            }  //if (tran_id_post_count >= tran_id_max_retry)
          }
        }  //while (!tran_id_succes_status_flag)
      }
    }
  }
  if (strcmp(topic, pingTranTopic.c_str()) == 0) {
    const size_t bufferSize = JSON_OBJECT_SIZE(1) + 40;
    DynamicJsonBuffer jsonBuffer(bufferSize);
    JsonObject& doc = jsonBuffer.parseObject(payload);
    res = doc["res"];        //
    res_flag = doc["flag"];  // 0
                             //   Serial.print("Res flag ");
                             //   Serial.println(res_flag);
    /*
       res_flag = doc["flag"]; // 0 == Red indicator light on
       res_flag = doc["flag"]; // 1 == Reboot
       res_flag = doc["flag"]; // 2 == Green indicator light on

    */
    if (strcmp(res, "true") == 0) {
      switch (res_flag) {
        case 0:
          {
            Serial.print("Res flag ");
            Serial.println(res_flag);
            Serial.println("Pinging && Red indicator light on");
            indicator_off();
            lastPing = millis();
            break;
          }
        case 1:
          {
            Serial.print("Res flag ");
            Serial.println(res_flag);
            Serial.println("Pinging && Ready to reboot");
            ESP.reset();
            //lastPing = millis();
            break;
          }
        case 2:
          {
            // Serial.println("Pinging && Green indicator light on");
            indicator_on();
            lastPing = millis();
            break;
          }
        default:
          {
            Serial.println("Anarchy rips apart the mightiest empires");
            break;
          }
      }
    }
  }

  String noUpdateMessage = "No Motor control firmware update available at " + ru_tag;
  String updateMessage = "Motor control firmware Update will begin for " + ru_tag + ", please wait";

  if (strcmp(topic, OtaVerCheckTopic.c_str()) == 0) {  // If version is checked
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");

    Serial.println("Payload received: ");
    for (int i = 0; i < length; i++) {
      Serial.print((char)payload[i]);  //Print the content of Payload at OtaVerCheckTopic aka version
    }
    Serial.println();

    //Store String payload value in EEPROM
    for (int n = 0; n < length; n++)  // automatically adjust for number of digits
    {
      EEPROM.write(n + 1, payload[n]);
    }
    EEPROM.commit();
    Serial.println("Payload stored in EEPROM...");

    Serial.print("EEPROM value: ");
    for (int n = 0; n < length; n++) {
      Serial.print(EEPROM.read(n + 1));
    }
    Serial.println("");


    // Receives Version info
    if ((char)payload[0] == Version[0]) {
      if ((char)payload[2] == Version[2]) {
        Serial.println("No new update available");                            // If given version at OtaVerCheckTopic and Current version matches
        client.publish(OtaUpdateInfoTopic.c_str(), noUpdateMessage.c_str());  //notify via mqtt
      }

      else {
        Serial.println("Update available!!!");                              //If given version at OtaVerCheckTopic and Current version doesn't match
        client.publish(OtaUpdateInfoTopic.c_str(), updateMessage.c_str());  //notify via mqtt
        FirmwareUpdate();
      }
    }

    else {
      Serial.println("Update available!!!");                              //If given version at OtaVerCheckTopic and Current version doesn't match
      client.publish(OtaUpdateInfoTopic.c_str(), updateMessage.c_str());  //notify via mqtt
      FirmwareUpdate();
    }
  }
}

void reconnect() {
  // Loop until reconnected (Basic & OTA connection)
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    // Attempt to connect
    if (client.connect(clientId.c_str(), "vilmqtt", "mvqitlt")) {

      Serial.println("connected");
      client.subscribe(dispenseTopic.c_str());
      client.subscribe(pingTranTopic.c_str());
      client.subscribe(OtaVerCheckTopic.c_str());
      //  WDT_count = 0;
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      indicator_off();

      // Wait 5 seconds before retrying
      tick_ping = tick_ping + 1;
      Serial.printf("Tick ping count %d of 5\n", tick_ping);

      if (tick_ping == 5)  //wait for reconnect to MQTT Server
      {
        ESP.reset();
      }
      delay(3000);
    }
  }
}

void eepromReset() {  //Reset eeprom by placing 0 at the adresses

  for (int n = 0; n < 10; n++) {
    EEPROM.write(n + 1, 0);
  }
  EEPROM.commit();
}

// **********************************************************
// *************** DO NOT EDIT THIS FILE ************
// **********************************************************

void setup() {
  Serial.begin(9600);
  pinMode(buzzer_pin, OUTPUT);
  digitalWrite(buzzer_pin, LOW);
  pinMode(indicator, OUTPUT);
  digitalWrite(indicator, HIGH);
  pinMode(modem_pin, OUTPUT);
  digitalWrite(modem_pin, OFF);  //Normally Connected pin of relay is connected to modem
  Serial.println(ru_tag);

  relay_init();

  //red_led_on();
  setup_wifi();
  setup_time();

  net.setTrustAnchors(&x509);
  client.setServer(mqtt_server, 8883);
  client.setCallback(callback);  // Attaching callback function to MQTT client. Whenever message arrive this function will execute

  reconnect();  // Connects with MQTT

  EEPROM.begin(512);  //Initialize EEPROM
  //eepromReset();

  Serial.print("EEPROM value: ");
  for (int n = 0; n < 3; n++) {
    Serial.print(EEPROM.read(n + 1));
  }
  Serial.println("");

  Serial.print("Firmware version: ");
  Serial.println(Version);

  if (EEPROM.read(1) == Version[0]) {
    if (EEPROM.read(3) == Version[2]) {
      Serial.println("Stored value in EEPROM are same as current version: No update...");  // Checks if previously device power was cut-off during update
    } else {
      Serial.println("An update will be downloaded...Please wait");  // If device power was cut-off during update, restart the update process
      eepromReset();
      FirmwareUpdate();
    }
  } else {
    if (EEPROM.read(1) == 0) {
      Serial.println("EEPROM Payload is in initial condition. No update necessary");  //When nodemcu receives the first firmware through serial port, initial value at EEPROM be 0
    } else {
      Serial.println("An update will be downloaded...Please wait");
      eepromReset();
      FirmwareUpdate();
    }
  }
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    setup_wifi();
    setup_time();
  }

  if (!client.connected()) {
    reconnect();
  }
  unsigned long now = millis();

  if (now - lastPing > 5000) {
    indicator_off();
  }
  client.loop();

  if (now - lastMsg > 1000) {
    lastMsg = now;
    const size_t capacity = JSON_OBJECT_SIZE(1) + 50;
    DynamicJsonBuffer jsonBuffer(capacity);
    JsonObject& root = jsonBuffer.createObject();
    root["ru"] = ru_tag;

    root.prettyPrintTo(msg, sizeof(msg));
    if (client.publish(ping_topic, msg)) {
      // Serial.println("Pinging");
      //indicator_on();
      lastPing = millis();
    } else {
      Serial.println("Ping Error");
      indicator_off();
    }
  }

  if (now - lastOtaMsg > 25000) {
    lastOtaMsg = now;
    String msgOTA = ru_tag + ", Motor " + "Pinging at version: " + String(Version);  //Displays current OTA version

    if (client.publish(pingOtaTopic.c_str(), msgOTA.c_str())) {  //ping at test server with current version
      Serial.println("  OTA Pinging...");
      Serial.print("Current version: ");
      Serial.println(Version);

      lastOtaPing = millis();
    } else {
      Serial.println("OTA Ping Error");
    }
  }
  ds_count = 0;
  motor_feedback = digitalRead(motor_feedback_pin);

  /*--------- Newly added@Rifat ----------*/
  const size_t capacity = JSON_OBJECT_SIZE(2) + 50;
  DynamicJsonBuffer jsonBuffer(capacity);
  JsonObject& dstat = jsonBuffer.createObject();
  /*--------- Newly added@Rifat ----------*/

  while (motor_feedback == drop_sensor_active_status) {
    ds_count++;
    motor_feedback = digitalRead(motor_feedback_pin);

    Serial.println("Into DS Loop");
    Serial.println(ds_count);
    if (ds_count == 20) {
      ds_count = 0;

      /*--------- Newly added@Rifat ----------*/
      dstat["code"] = drop_sensor_error;
      dstat.prettyPrintTo(dmsg, sizeof(dmsg));
      Serial.println(dmsg);
      client.publish("jyotistatuscode", dmsg);

      dstat["ru_tag"] = ru_tag;
      dstat.prettyPrintTo(rmsg, sizeof(rmsg));
      Serial.println(rmsg);
      client.publish("jyotistatuscode", rmsg);

      /*--------- Newly added@Rifat ----------*/

      // motor_run(8);
      delay(3000);
      relay_SetStatus(OFF, OFF, OFF, OFF);  //All Relay OFF (Deactivate Demux)
    }
    delay(200);
  }
}