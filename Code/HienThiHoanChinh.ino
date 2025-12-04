#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <LoRa.h>
#include <TFT_eSPI.h> // Chỉ dùng thư viện gốc

// ===== CẤU HÌNH WIFI & MQTT =====
const char* ssid = "Tuan Anh ";
const char* password = "12345689";
const char* mqtt_server = "192.168.100.248";
const int mqtt_port = 1883;

// Topics
const char* topic_sensor_thande = "thande/sensor";
const char* topic_sensor_longde = "longde/sensor";
const char* topic_ai_status = "ai/status";
const char* topic_nguong_ai = "ai/nguong";

WiFiClient espClient;
PubSubClient client(espClient);

// ===== PHẦN CỨNG =====
#define TFT_LED 21
#define TFT_DC  22
#define TFT_CS  5
#define TFT_RST 4
#define LORA_RST 27
#define LORA_INT 26
#define LORA_SCK 14
#define LORA_MOSI 13
#define LORA_MISO 12
#define LORA_CS 15

TFT_eSPI tft = TFT_eSPI();

// ===== BIẾN DỮ LIỆU =====
float do_am_dat_thande = 0;
float nhiet_do_thande = 0;
float do_am_kk_thande = 0;
int trang_thai_nghieng_thande = 0;
unsigned long thoi_gian_cap_nhat_thande = 0;

float muc_nuoc_longde = 0;
float luu_luong_longde = 0;
int trang_thai_mua_digital_longde = 0;
float do_mua_analog_longde = 0;
unsigned long thoi_gian_cap_nhat_longde = 0;

bool trang_thai_ket_noi_thande = false;
bool trang_thai_ket_noi_longde = false;
const unsigned long thoi_gian_chet = 30000;
unsigned long lastReconnectAttempt = 0;
unsigned long lastScreenUpdate = 0;

String trang_thai_ai = "KHOI TAO";
String mua_hien_tai = "CHUA RO";
String xu_huong_ai = "CHUA RO";
String canh_bao_tong_hop = "";

// Ngưỡng
float MUC_NUOC_CANH_BAO = 250.0;
float MUC_NUOC_NGUY_HIEM = 300.0;
float DO_AM_DAT_CANH_BAO = 70.0;
float DO_AM_DAT_NGUY_HIEM = 85.0;
float LUU_LUONG_CANH_BAO = 15.0;

// ===== WIFI & MQTT =====
void setup_wifi() {
  WiFi.mode(WIFI_STA);
  pinMode(2, INPUT_PULLUP);
  WiFi.begin(ssid, password);
}

boolean reconnect_mqtt_non_blocking() {
  if (client.connect("HienThi_NoAccent")) {
    Serial.println("MQTT connected");
    client.subscribe(topic_sensor_thande);
    client.subscribe(topic_sensor_longde);
    client.subscribe(topic_ai_status);
    client.subscribe(topic_nguong_ai);
    return true;
  }
  return false;
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) message += (char)payload[i];
  
  if (String(topic) == topic_sensor_thande) {
    StaticJsonDocument<256> doc;
    if (!deserializeJson(doc, message)) {
      do_am_dat_thande = doc["do_am_dat"];
      nhiet_do_thande = doc["nhiet_do"];
      do_am_kk_thande = doc["do_am_kk"];
      trang_thai_nghieng_thande = doc["nghieng"];
      trang_thai_ket_noi_thande = true;
      thoi_gian_cap_nhat_thande = millis();
    }
  } 
  else if (String(topic) == topic_sensor_longde) {
    StaticJsonDocument<256> doc;
    if (!deserializeJson(doc, message)) {
      muc_nuoc_longde = doc["muc_nuoc"];
      luu_luong_longde = doc["luu_luong"];
      trang_thai_mua_digital_longde = doc["mua_digital"];
      do_mua_analog_longde = doc["do_mua_analog"];
      trang_thai_ket_noi_longde = true;
      thoi_gian_cap_nhat_longde = millis();
    }
  }
  else if (String(topic) == topic_ai_status) {
    StaticJsonDocument<512> doc;
    if (!deserializeJson(doc, message)) {
      trang_thai_ai = doc["status"].as<String>();
      mua_hien_tai = doc["mua"].as<String>();
      xu_huong_ai = doc["xuhuong"].as<String>();
      
      // Chuyển mã sang Không Dấu
      if(mua_hien_tai == "MUA_MUA") mua_hien_tai = "CO MUA";
      else if(mua_hien_tai == "MUA_NHO") mua_hien_tai = "MUA NHO";
      else if(mua_hien_tai == "MUA_RAT_TO") mua_hien_tai = "MUA TO";
      else mua_hien_tai = "KHONG";

      if(xu_huong_ai == "TANG" || xu_huong_ai == "NUOC DANG (TANG)") xu_huong_ai = "DANG DANG";
      else if(xu_huong_ai == "GIAM" || xu_huong_ai == "NUOC RUT (GIAM)") xu_huong_ai = "DANG RUT";
      else xu_huong_ai = "ON DINH";

      // Xử lý chuỗi cảnh báo dài (Bỏ dấu)
      canh_bao_tong_hop = trang_thai_ai;
      canh_bao_tong_hop.replace("NGUY_HIEM", "NGUY HIEM");
      canh_bao_tong_hop.replace("CANH_BAO", "CANH BAO");
      canh_bao_tong_hop.replace("CẤP", "CAP");
      canh_bao_tong_hop.replace("TRÀN", "TRAN");
      canh_bao_tong_hop.replace("LŨ", "LU");
      canh_bao_tong_hop.replace("SẠT", "SAT");
      canh_bao_tong_hop.replace("LỞ", "LO");
    }
  }
}

// ===== LORA =====
void khoi_tao_lora() {
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_INT);
  if (!LoRa.begin(433E6)) Serial.println("LoRa failed!");
}

void nhan_du_lieu_loRa() {
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    while (LoRa.available()) LoRa.read();
  }
}

// ===== HIỂN THỊ TFT (KHÔNG DẤU) =====
void khoi_tao_tft() {
  pinMode(TFT_LED, OUTPUT);
  digitalWrite(TFT_LED, HIGH);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
}

void ve_khung_nen() {
  tft.fillScreen(TFT_BLACK);
  
  // Header
  tft.setTextSize(2);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setCursor(60, 5); tft.println("GIAM SAT DE DIEU");
  tft.drawFastHLine(0, 25, 320, TFT_DARKGREEN);

  // Cột
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(10, 30); tft.println("THAN DE");
  tft.setCursor(170, 30); tft.println("LONG DE");
  
  // Nhãn
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  int y_start = 45;
  
  // Cột 1
  tft.setCursor(10, y_start); tft.print("Do am dat:");
  tft.setCursor(10, y_start + 15); tft.print("Nhiet do:");
  tft.setCursor(10, y_start + 30); tft.print("Do am KK:");
  tft.setCursor(10, y_start + 45); tft.print("Nghieng:");
  
  // Cột 2
  tft.setCursor(170, y_start); tft.print("Muc nuoc:");
  tft.setCursor(170, y_start + 15); tft.print("Luu luong:");
  tft.setCursor(170, y_start + 30); tft.print("Mua:");
  tft.setCursor(170, y_start + 45); tft.print("Do mua:");
  
  // Footer
  tft.drawFastHLine(0, 115, 320, 0x3333);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setCursor(10, 125); tft.print("TRANG THAI AI:");
  
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 140); tft.print("Du bao:");
  tft.setCursor(150, 140); tft.print("Xu huong:");
}

void cap_nhat_man_hinh() {
  int y_start = 45;
  tft.setTextSize(1);
  tft.setTextPadding(60);

  // --- CỘT 1 ---
  // Độ ẩm đất
  if (trang_thai_ket_noi_thande) {
    uint16_t mau = do_am_dat_thande > 70 ? TFT_RED : TFT_GREEN;
    tft.setTextColor(mau, TFT_BLACK); tft.setCursor(80, y_start); 
    tft.print(do_am_dat_thande, 1); tft.print("%");
  } else { tft.setTextColor(TFT_RED, TFT_BLACK); tft.setCursor(80, y_start); tft.print("---"); }

  // Nhiệt độ
  tft.setTextColor(TFT_GREEN, TFT_BLACK); tft.setCursor(80, y_start + 15);
  if(trang_thai_ket_noi_thande) { tft.print(nhiet_do_thande, 1); tft.print("C"); }

  // Độ ẩm KK
  tft.setCursor(80, y_start + 30);
  if(trang_thai_ket_noi_thande) { tft.print(do_am_kk_thande, 1); tft.print("%"); }

  // Nghiêng
  tft.setCursor(80, y_start + 45);
  if (trang_thai_ket_noi_thande) {
    if(trang_thai_nghieng_thande == 1) { tft.setTextColor(TFT_RED, TFT_BLACK); tft.print("NGUY HIEM"); }
    else { tft.setTextColor(TFT_GREEN, TFT_BLACK); tft.print("BINH THUONG"); }
  }

  // --- CỘT 2 ---
  // Mực nước
  tft.setCursor(240, y_start);
  if (trang_thai_ket_noi_longde) {
    uint16_t mau = muc_nuoc_longde > 250 ? TFT_RED : TFT_GREEN;
    tft.setTextColor(mau, TFT_BLACK); 
    if(muc_nuoc_longde < 0) tft.print("LOI");
    else { tft.print(muc_nuoc_longde, 1); tft.print("cm"); }
  } else { tft.setTextColor(TFT_RED, TFT_BLACK); tft.setCursor(240, y_start); tft.print("---"); }
  
  // Lưu lượng
  tft.setTextColor(TFT_GREEN, TFT_BLACK); tft.setCursor(240, y_start + 15);
  if(trang_thai_ket_noi_longde) { tft.print(luu_luong_longde, 1); tft.print("L"); }

  // Mưa & Độ mưa
  tft.setTextColor(TFT_CYAN, TFT_BLACK); tft.setCursor(240, y_start + 30);
  if(trang_thai_ket_noi_longde) tft.print(trang_thai_mua_digital_longde ? "CO MUA" : "KHONG");

  tft.setTextColor(TFT_GREEN, TFT_BLACK); tft.setCursor(240, y_start + 45);
  if(trang_thai_ket_noi_longde) { tft.print(do_mua_analog_longde, 0); tft.print("%"); }

  // --- AI INFO ---
  tft.setTextPadding(80);
  tft.setCursor(60, 140); tft.setTextColor(TFT_YELLOW, TFT_BLACK); tft.print(mua_hien_tai);
  
  tft.setCursor(220, 140); 
  if(xu_huong_ai == "DANG DANG") tft.setTextColor(TFT_RED, TFT_BLACK);
  else tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.print(xu_huong_ai);

  // Cảnh báo (Scroll)
  tft.setTextPadding(320);
  tft.setCursor(10, 160);
  if (canh_bao_tong_hop.indexOf("CAP 5") >= 0) tft.setTextColor(TFT_RED, TFT_BLACK);
  else if (canh_bao_tong_hop.indexOf("CAP 2") >= 0) tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  else tft.setTextColor(TFT_GREEN, TFT_BLACK);
  
  if(canh_bao_tong_hop.length() > 30) {
     tft.print(canh_bao_tong_hop.substring(0, 30));
     tft.setCursor(10, 175); tft.print(canh_bao_tong_hop.substring(30));
  } else {
     tft.print(canh_bao_tong_hop);
  }
  
  tft.setTextPadding(0);
}

void kiem_tra_ket_noi() {
  unsigned long now = millis();
  if (now - thoi_gian_cap_nhat_thande > thoi_gian_chet) trang_thai_ket_noi_thande = false;
  if (now - thoi_gian_cap_nhat_longde > thoi_gian_chet) trang_thai_ket_noi_longde = false;
}

void setup() {
  Serial.begin(115200);
  khoi_tao_tft();
  ve_khung_nen();
  khoi_tao_lora();
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqtt_callback);
}

void loop() {
  unsigned long currentMillis = millis();
  if (!client.connected()) {
    if (currentMillis - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = currentMillis;
      if (reconnect_mqtt_non_blocking()) lastReconnectAttempt = 0;
    }
  } else {
    client.loop();
  }
  nhan_du_lieu_loRa();
  kiem_tra_ket_noi();
  if (currentMillis - lastScreenUpdate > 1000) {
    cap_nhat_man_hinh();
    lastScreenUpdate = currentMillis;
  }
}
