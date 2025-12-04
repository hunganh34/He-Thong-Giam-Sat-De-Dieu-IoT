#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LoRa.h>
#include <SPI.h>

// ===== CẤU HÌNH WIFI & MQTT =====
const char* ssid = "Tuan Anh ";
const char* password = "12345689";
const char* mqtt_server = "192.168.100.248";
const int mqtt_port = 1883;

// Topics MQTT
const char* topic_sensor_longde = "longde/sensor";
const char* topic_control_longde = "longde/control";
const char* topic_nguong_ai = "ai/nguong";
const char* topic_ai_status = "ai/status";

WiFiClient espClient;
PubSubClient client(espClient);

// ===== CẤU HÌNH PHẦN CỨNG =====
#define SCK   14
#define MISO  12
#define MOSI  13
#define CS    15
#define RST   33
#define DIO0  32

#define TRIG_PIN 16
#define ECHO_PIN 17

const int chan_cam_bien_mua_D0 = 34;
const int chan_cam_bien_mua_ADC = 36;
const int chan_cam_bien_luu_luong = 5;

// Thông số hiệu chuẩn
const int gia_tri_mua_kho = 4095;
const int gia_tri_mua_uot = 1500;

// Biến toàn cục
volatile unsigned long xung_dem = 0;
unsigned long thoigian_demxung_truoc = 0;
unsigned long lastReconnectAttempt = 0;

// ===== NGUỒNG TỪ AI =====
float MUC_NUOC_AN_TOAN = 200.0;
float MUC_NUOC_CANH_BAO = 250.0;
float MUC_NUOC_NGUY_HIEM = 300.0;

float LUU_LUONG_AN_TOAN = 5.0;
float LUU_LUONG_CANH_BAO = 15.0;
float LUU_LUONG_NGUY_HIEM = 25.0;

float DO_MUA_AN_TOAN = 30.0;
float DO_MUA_CANH_BAO = 60.0;
float DO_MUA_NGUY_HIEM = 80.0;

// Biến AI status
String ai_status = "KHOI_TAO";
String ai_mua = "CHUA_XAC_DINH";
String ai_xu_huong = "CHUA_PHAN_TICH";

// Biến dữ liệu
float muc_nuoc = 0;
float luu_luong = 0;
int trang_thai_mua_digital = 0;
float do_mua_analog = 0;

// LoRa
const long tan_so_loRa = 433E6;
const int dia_chi_node_long_de = 2;

// Biến thời gian
unsigned long lastMqttSend = 0;
unsigned long lastLoraSend = 0;
unsigned long lastAiDisplay = 0;
unsigned long lastSensorRead = 0;

// ===== WIFI & MQTT ĐÃ TỐI ƯU =====
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  // CẤU HÌNH WIFI ỔN ĐỊNH
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_STA);
  
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);

  WiFi.begin(ssid, password);

  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - startTime > 20000) {
      Serial.println("\nWiFi connection failed!");
      break;
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
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
    if (doc.containsKey("muc_nuoc_an_toan")) {
      MUC_NUOC_AN_TOAN = doc["muc_nuoc_an_toan"];
      MUC_NUOC_CANH_BAO = doc["muc_nuoc_canh_bao"];
      MUC_NUOC_NGUY_HIEM = doc["muc_nuoc_nguy_hiem"];
      
      LUU_LUONG_AN_TOAN = doc["luu_luong_an_toan"];
      LUU_LUONG_CANH_BAO = doc["luu_luong_canh_bao"];
      LUU_LUONG_NGUY_HIEM = doc["luu_luong_nguy_hiem"];
      
      DO_MUA_AN_TOAN = doc["do_mua_an_toan"];
      DO_MUA_CANH_BAO = doc["do_mua_canh_bao"];
      DO_MUA_NGUY_HIEM = doc["do_mua_nguy_hiem"];
      
      Serial.println("=== NGUONG MOI TU AI ===");
      Serial.print("Muc nuoc: "); Serial.print(MUC_NUOC_CANH_BAO); Serial.println("cm");
      Serial.print("Luu luong: "); Serial.print(LUU_LUONG_CANH_BAO); Serial.println("L/ph");
      Serial.print("Do mua: "); Serial.print(DO_MUA_CANH_BAO); Serial.println("%");
    }
  } else {
    Serial.println("Error parsing threshold JSON");
  }
}

// HÀM HIỂN THỊ AI STATUS TRÊN SERIAL
void hien_thi_ai_status() {
  Serial.println("=== AI STATUS ===");
  Serial.print("Trang thai: "); Serial.println(ai_status);
  Serial.print("Mua: "); Serial.println(ai_mua);
  Serial.print("Xu huong: "); Serial.println(ai_xu_huong);
  Serial.print("Nguong muc nuoc: "); Serial.print(MUC_NUOC_CANH_BAO); Serial.println("cm");
  Serial.println("=================");
}

void gui_du_lieu_mqtt() {
  StaticJsonDocument<256> doc;
  doc["node_id"] = dia_chi_node_long_de;
  doc["muc_nuoc"] = muc_nuoc;
  doc["luu_luong"] = luu_luong;
  doc["mua_digital"] = trang_thai_mua_digital;
  doc["do_mua_analog"] = do_mua_analog;
  doc["timestamp"] = millis();
  
  // Thêm AI status vào dữ liệu gửi đi
  doc["ai_status"] = ai_status;
  doc["ai_mua"] = ai_mua;
  doc["ai_xu_huong"] = ai_xu_huong;
  
  String output;
  serializeJson(doc, output);
  
  if (client.connected()) {
    client.publish(topic_sensor_longde, output.c_str());
    Serial.print("MQTT sent: ");
    Serial.println(output);
  } else {
    Serial.println("MQTT not connected, cannot send data");
  }
}

// ===== CẢM BIẾN =====
void doc_cam_bien() {
  muc_nuoc = doc_muc_nuoc();
  luu_luong = doc_luu_luong();
  trang_thai_mua_digital = doc_cam_bien_mua_digital();
  int gia_tri_mua_analog = doc_cam_bien_mua_analog();
  do_mua_analog = tinh_phan_tram_mua_analog(gia_tri_mua_analog);
}

float doc_muc_nuoc() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long thoi_gian = pulseIn(ECHO_PIN, HIGH, 30000);
  if (thoi_gian == 0) return -1;

  float khoang_cach = thoi_gian * 0.0343 / 2;
  return khoang_cach;
}

int doc_cam_bien_mua_digital() {
  return !digitalRead(chan_cam_bien_mua_D0);
}

int doc_cam_bien_mua_analog() {
  return analogRead(chan_cam_bien_mua_ADC);
}

float tinh_phan_tram_mua_analog(int gia_tri_analog) {
  float phan_tram = map(gia_tri_analog, gia_tri_mua_kho, gia_tri_mua_uot, 0, 100);
  return constrain(phan_tram, 0, 100);
}

float doc_luu_luong() {
  noInterrupts();
  unsigned long xung_tam = xung_dem;
  xung_dem = 0;
  interrupts();

  unsigned long khoang_thoi_gian = millis() - thoigian_demxung_truoc;
  thoigian_demxung_truoc = millis();
  
  if (khoang_thoi_gian == 0) return 0;
  
  float tan_so = (xung_tam * 1000.0) / khoang_thoi_gian;
  return tan_so / 7.5;
}

void dem_xung() {
  xung_dem++;
}

// ===== LORA =====
void gui_du_lieu_loRa() {
  String du_lieu = String(dia_chi_node_long_de) + "|" +
                   (muc_nuoc < 0 ? "ERR" : String(muc_nuoc, 1)) + "|" +
                   String(luu_luong, 2) + "|" +
                   String(trang_thai_mua_digital) + "|" +
                   String((int)do_mua_analog) + "|" +
                   ai_status + "|" +
                   ai_mua;
  
  LoRa.beginPacket();
  LoRa.print(du_lieu);
  LoRa.endPacket();
  
  Serial.print("LoRa sent: ");
  Serial.println(du_lieu);
}

void nhan_du_lieu_loRa() {
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

// ===== SETUP & LOOP =====
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("=== NODE LONGDE STARTING ===");
  
  // Cảm biến
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(chan_cam_bien_mua_D0, INPUT);
  pinMode(chan_cam_bien_luu_luong, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(chan_cam_bien_luu_luong), dem_xung, FALLING);
  
  // LoRa
  SPI.begin(SCK, MISO, MOSI, CS);
  LoRa.setPins(CS, RST, DIO0);
  if (!LoRa.begin(tan_so_loRa)) {
    Serial.println("LoRa initialization failed!");
    while (1);
  }
  LoRa.setTxPower(20);
  Serial.println("LoRa initialized successfully!");
  
  // WiFi & MQTT
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqtt_callback);

  Serial.println("Node Longde started with AI support!");
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
