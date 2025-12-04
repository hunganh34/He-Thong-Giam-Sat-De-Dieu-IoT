#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <LoRa.h>
#include <SPI.h>

// ===== CẤU HÌNH WIFI & MQTT =====
const char* ssid = "Tuan Anh ";
const char* password = "12345689";
const char* mqtt_server = "192.168.100.248";
const int mqtt_port = 1883;

// Topics MQTT
const char* topic_sensor_thande = "thande/sensor";
const char* topic_control_thande = "thande/control";
const char* topic_nguong_ai = "ai/nguong";
const char* topic_ai_status = "ai/status";

WiFiClient espClient;
PubSubClient client(espClient);

// ===== CẤU HÌNH PHẦN CỨNG - VẪN DÙNG GPIO2 CHO LORA RST =====
#define SCK     18
#define MISO    19
#define MOSI    23
#define CS      5
#define RST     2   // VẪN DÙNG GPIO2 CHO LORA RST
#define DIO0    4

const int chan_cam_bien_dat = 36;
const int chan_dht22 = 21;
const int chan_cam_bien_nghieng = 34;

// Thông số hiệu chuẩn
const int gia_tri_kho = 4095;
const int gia_tri_uot = 1500;
const int do_nhay = 8;

// Biến toàn cục
int mang_gia_tri[8];
int chi_so = 0;
int tong_gia_tri = 0;

#define kieu_dht DHT22
DHT dht(chan_dht22, kieu_dht);

// ===== NGUỒNG TỪ AI =====
float DO_AM_DAT_AN_TOAN = 50.0;
float DO_AM_DAT_CANH_BAO = 70.0;
float DO_AM_DAT_NGUY_HIEM = 85.0;

// Biến AI status
String ai_status = "KHOI_TAO";
String ai_mua = "CHUA_XAC_DINH";
String ai_xu_huong = "CHUA_PHAN_TICH";

// Biến dữ liệu
float do_am_dat = 0;
float nhiet_do = 0;
float do_am_kk = 0;
int trang_thai_nghieng = 0;

// LoRa
const long tan_so_loRa = 433E6;
const int dia_chi_node_than_de = 1;
unsigned long lastReconnectAttempt = 0;
// Biến thời gian
unsigned long lastAiDisplay = 0;
unsigned long lastMqttSend = 0;
unsigned long lastLoraSend = 0;
unsigned long lastSensorRead = 0;

// ===== KHAI BÁO HÀM =====
void xu_ly_du_lieu_ai(String message);
void xu_ly_nguong_moi(String message);
void setup_wifi();
void reconnect_mqtt();
void mqtt_callback(char* topic, byte* payload, unsigned int length);
void hien_thi_ai_status();
void gui_du_lieu_mqtt();
float do_cam_bien_dat();
float do_dht22_nhiet_do();
float do_dht22_do_am_kk();
int do_cam_bien_nghieng();
void gui_du_lieu_loRa();
void nhan_du_lieu_loRa();
void doc_cam_bien();

// ===== CÀI ĐẶT WIFI & MQTT VỚI GPIO2 RST =====
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  // CẤU HÌNH ĐẶC BIỆT KHI DÙNG GPIO2 CHO LORA RST
  // 1. TẮT WIFI HOÀN TOÀN TRƯỚC KHI CẤU HÌNH
  WiFi.mode(WIFI_OFF);
  delay(100);
  
  // 2. CẤU HÌNH GPIO2 Ở CHẾ ĐỘ AN TOÀN CHO LORA
  pinMode(RST, OUTPUT);
  digitalWrite(RST, HIGH);  // Đảm bảo LoRa không bị reset
  delay(50);
  
  // 3. KHỞI ĐỘNG WIFI VỚI CẤU HÌNH ĐẶC BIỆT
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);    // Quan trọng: không lưu cấu hình WiFi
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);

  WiFi.begin(ssid, password);

  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - startTime > 20000) {
      Serial.println("\nWiFi connection failed!");
      
      // THỬ CÁCH TIẾP CẬN KHÁC NẾU THẤT BẠI
      Serial.println("Trying alternative WiFi approach...");
      WiFi.disconnect();
      delay(1000);
      WiFi.begin(ssid, password);
      
      startTime = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - startTime < 15000) {
        delay(500);
        Serial.print("*");
      }
      break;
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    
    // KIỂM TRA LORA SAU KHI WIFI KẾT NỐI
    Serial.println("Verifying LoRa after WiFi connection...");
    delay(100);
    gui_du_lieu_loRa(); // Test gửi LoRa
  }
}

boolean reconnect_mqtt_non_blocking() {
  if (client.connect("NodeCamBien_ID_X")) { // Đổi ID cho phù hợp từng Node
    Serial.println("MQTT connected");
    // Subscribe lại các topic cần thiết
    client.subscribe("ai/nguong"); 
    // ... subscribe các topic khác nếu có
    return true;
  }
  return false;
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);
  
  if (String(topic) == topic_nguong_ai) {
    xu_ly_nguong_moi(message);
  } else if (String(topic) == topic_ai_status) {
    xu_ly_du_lieu_ai(message);
  }
}

void xu_ly_du_lieu_ai(String message) {
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, message);
  
  if (!error) {
    if (doc.containsKey("status")) ai_status = doc["status"].as<String>();
    if (doc.containsKey("mua")) ai_mua = doc["mua"].as<String>();
    if (doc.containsKey("xu_huong")) ai_xu_huong = doc["xu_huong"].as<String>();
    
    Serial.println("=== AI STATUS UPDATE ===");
    Serial.print("Status: "); Serial.println(ai_status);
    Serial.print("Mua: "); Serial.println(ai_mua);
    Serial.print("Xu huong: "); Serial.println(ai_xu_huong);
  } else {
    Serial.println("Error parsing AI status JSON");
  }
}

void xu_ly_nguong_moi(String message) {
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, message);
  
  if (!error) {
    if (doc.containsKey("do_am_dat_an_toan")) {
      DO_AM_DAT_AN_TOAN = doc["do_am_dat_an_toan"];
      DO_AM_DAT_CANH_BAO = doc["do_am_dat_canh_bao"];
      DO_AM_DAT_NGUY_HIEM = doc["do_am_dat_nguy_hiem"];
      
      Serial.println("=== NGUONG MOI TU AI ===");
      Serial.print("Do am dat an toan: "); Serial.print(DO_AM_DAT_AN_TOAN); Serial.println("%");
      Serial.print("Do am dat canh bao: "); Serial.print(DO_AM_DAT_CANH_BAO); Serial.println("%");
      Serial.print("Do am dat nguy hiem: "); Serial.print(DO_AM_DAT_NGUY_HIEM); Serial.println("%");
    }
  } else {
    Serial.println("Error parsing threshold JSON");
  }
}

void hien_thi_ai_status() {
  Serial.println("=== AI STATUS ===");
  Serial.print("Trang thai: "); Serial.println(ai_status);
  Serial.print("Mua: "); Serial.println(ai_mua);
  Serial.print("Xu huong: "); Serial.println(ai_xu_huong);
  Serial.print("Nguong do am dat: ");
  Serial.print(DO_AM_DAT_CANH_BAO);
  Serial.println("%");
  Serial.println("=================");
}

void gui_du_lieu_mqtt() {
  StaticJsonDocument<256> doc;
  doc["node_id"] = dia_chi_node_than_de;
  doc["do_am_dat"] = do_am_dat;
  doc["nhiet_do"] = nhiet_do;
  doc["do_am_kk"] = do_am_kk;
  doc["nghieng"] = trang_thai_nghieng;
  doc["timestamp"] = millis();
  
  doc["ai_status"] = ai_status;
  doc["ai_mua"] = ai_mua;
  doc["ai_xu_huong"] = ai_xu_huong;
  
  String output;
  serializeJson(doc, output);
  
  if (client.connected()) {
    client.publish(topic_sensor_thande, output.c_str());
    Serial.print("MQTT sent: ");
    Serial.println(output);
  } else {
    Serial.println("MQTT not connected, cannot send data");
  }
}

// ===== XỬ LÝ CẢM BIẾN =====
void doc_cam_bien() {
  do_am_dat = do_cam_bien_dat();
  nhiet_do = do_dht22_nhiet_do();
  do_am_kk = do_dht22_do_am_kk();
  trang_thai_nghieng = do_cam_bien_nghieng();
}

float do_cam_bien_dat() {
  tong_gia_tri -= mang_gia_tri[chi_so];
  mang_gia_tri[chi_so] = analogRead(chan_cam_bien_dat);
  tong_gia_tri += mang_gia_tri[chi_so];
  
  chi_so = (chi_so + 1) % do_nhay;
  
  int gia_tri_tb = tong_gia_tri / do_nhay;
  float do_am = map(gia_tri_tb, gia_tri_kho, gia_tri_uot, 0, 100);
  do_am = constrain(do_am, 0, 100);
  return do_am;
}

float do_dht22_nhiet_do() {
  float temp = dht.readTemperature();
  if (isnan(temp)) {
    Serial.println("Failed to read temperature from DHT22!");
    return -999;
  }
  return temp;
}

float do_dht22_do_am_kk() {
  float humidity = dht.readHumidity();
  if (isnan(humidity)) {
    Serial.println("Failed to read humidity from DHT22!");
    return -999;
  }
  return humidity;
}

int do_cam_bien_nghieng() {
  int gia_tri = analogRead(chan_cam_bien_nghieng);
  return (gia_tri > 2000) ? 1 : 0;
}

// ===== XỬ LÝ LORA VỚI GPIO2 RST =====
void gui_du_lieu_loRa() {
  // ĐẢM BẢO GPIO2 Ở TRẠNG THÁI CAO CHO LORA
  digitalWrite(RST, HIGH);
  delay(10);
  
  String du_lieu = String(dia_chi_node_than_de) + "|" +
                   String(do_am_dat, 1) + "|" +
                   (nhiet_do <= -998 ? "ERR" : String(nhiet_do, 1)) + "|" +
                   (do_am_kk <= -998 ? "ERR" : String(do_am_kk, 1)) + "|" +
                   String(trang_thai_nghieng) + "|" +
                   ai_status + "|" +
                   ai_mua;
  
  LoRa.beginPacket();
  LoRa.print(du_lieu);
  LoRa.endPacket();
  
  Serial.print("LoRa sent: ");
  Serial.println(du_lieu);
}

void nhan_du_lieu_loRa() {
  // ĐẢM BẢO GPIO2 Ở TRẠNG THÁI CAO CHO LORA
  digitalWrite(RST, HIGH);
  delay(5);
  
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String nhan_du_lieu = "";
    while (LoRa.available()) {
      nhan_du_lieu += (char)LoRa.read();
    }
    Serial.print("LoRa received: ");
    Serial.println(nhan_du_lieu);
  }
}

// ===== SETUP & LOOP CHÍNH VỚI GPIO2 RST =====
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("=== NODE THAN DE - GPIO2 LORA RST VERSION ===");
  
  // KHỞI TẠO LORA TRƯỚC KHI WIFI
  Serial.println("Initializing LoRa first...");
  
  // CẤU HÌNH GPIO2 CHO LORA RST
  pinMode(RST, OUTPUT);
  digitalWrite(RST, LOW);
  delay(100);
  digitalWrite(RST, HIGH);
  delay(100);
  
  SPI.begin(SCK, MISO, MOSI, CS);
  LoRa.setPins(CS, RST, DIO0);
  
  if (!LoRa.begin(tan_so_loRa)) {
    Serial.println("LoRa initialization failed! Retrying...");
    // Thử reset LoRa
    digitalWrite(RST, LOW);
    delay(100);
    digitalWrite(RST, HIGH);
    delay(100);
    
    if (!LoRa.begin(tan_so_loRa)) {
      Serial.println("LoRa initialization failed again!");
    } else {
      Serial.println("LoRa initialized on second attempt!");
    }
  } else {
    Serial.println("LoRa initialized successfully!");
  }
  
  LoRa.setTxPower(20);
  
  // KHỞI TẠO CẢM BIẾN
  Serial.println("Initializing sensors...");
  analogReadResolution(12);
  for (int i = 0; i < do_nhay; i++) {
    mang_gia_tri[i] = analogRead(chan_cam_bien_dat);
    tong_gia_tri += mang_gia_tri[i];
  }
  dht.begin();
  pinMode(chan_cam_bien_nghieng, INPUT);
  
  // KẾT NỐI WIFI & MQTT (CÓ XỬ LÝ GPIO2 RST)
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqtt_callback);
  
  Serial.println("Node Thande started with GPIO2 as LoRa RST!");
}

void loop() {
  unsigned long currentMillis = millis();

  // --- 1. XỬ LÝ WIFI & MQTT (KHÔNG CHẶN) ---
  if (!client.connected()) {
    // Nếu chưa kết nối, thì cứ mỗi 5 giây mới thử lại 1 lần
    if (currentMillis - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = currentMillis;
      Serial.println("Mat ket noi MQTT. Dang thu ket noi lai...");
      
      // Thử kết nối lại (nhưng không đứng đợi)
      if (reconnect_mqtt_non_blocking()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    // Nếu đã kết nối thì duy trì
    client.loop();
    
    // Gửi dữ liệu MQTT (chỉ gửi khi có mạng)
    if (currentMillis - lastMqttSend >= 10000) {
      gui_du_lieu_mqtt();
      lastMqttSend = currentMillis;
    }
  }

  // --- 2. CÁC TÁC VỤ KHÁC (LUÔN CHẠY DÙ CÓ WIFI HAY KHÔNG) ---
  
  // Đọc cảm biến
  if (currentMillis - lastSensorRead >= 2000) {
    doc_cam_bien();
    lastSensorRead = currentMillis;
  }

  // Gửi LoRa (QUAN TRỌNG: Nằm ngoài khối if wifi)
  // Code này đảm bảo LoRa luôn được gửi mỗi 5 giây bất chấp WiFi sống hay chết
  if (currentMillis - lastLoraSend >= 5000) {
    gui_du_lieu_loRa();
    lastLoraSend = currentMillis;
  }

  // Kiểm tra nhận LoRa (nếu node này có nhận dữ liệu)
  nhan_du_lieu_loRa();
  
  // Hiển thị trạng thái AI (nếu có)
  if (currentMillis - lastAiDisplay >= 30000) {
    hien_thi_ai_status();
    lastAiDisplay = currentMillis;
  }
}
