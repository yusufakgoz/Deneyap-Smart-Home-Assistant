#include "arduino_secrets.h"
#include "thingProperties.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <DHT.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <time.h>

// --- FONT DOSYALARI ---
#include <Fonts/FreeSansBold9pt7b.h>  
#include <Fonts/FreeSans9pt7b.h>       
#include <Fonts/FreeSansBold12pt7b.h>  
#include <Fonts/FreeSansBold18pt7b.h> 

// --- PIN TANIMLAMALARI ---
#define TFT_CS     D1
#define TFT_RST    D0
#define TFT_DC     D2
#define TFT_MOSI   D3  
#define TFT_SCLK   D4 

#define DHTPIN     D12
#define DHTTYPE    DHT11

#define BUZZER_PIN D13
#define BUTTON_PIN D5 

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
DHT dht(DHTPIN, DHTTYPE);

// --- AYARLAR ---
String city = "Istanbul"; 
String countryCode = "TR";
String apiKey = "   "; //BURAYA OPENWEATHERMAP DEN ALDIĞINIZ APİ KEYİ GİRİNİZ

String currentPath = "http://api.openweathermap.org/data/2.5/weather?q=" + city + "," + countryCode + "&units=metric&APPID=" + apiKey;
String forecastPath = "http://api.openweathermap.org/data/2.5/forecast?q=" + city + "," + countryCode + "&units=metric&APPID=" + apiKey;

// Renkler
#define RENK_ARKA       0x0000 
#define RENK_MAVI_KUTU  0x0339 
#define RENK_TURUNCU    0xE400 
#define RENK_YESIL      0x05E0 
#define RENK_KIRMIZI    0xF800 
#define RENK_KOYU_GRI   0x2104 
#define RENK_BEYAZ      0xFFFF
#define RENK_GRI        0xBDF7
#define RENK_SECIM      0xFFFF 

// Değişkenler
int aktifSayfa = 1; 
unsigned long sonHavaGuncelleme = 0;
unsigned long sonSensorOkuma = 0;
const long havaGecikme = 600000; 
const long sensorGecikme = 2000; 

// Veriler
float t_ic = 0; int h_ic = 0; float t_dis = 0;
String saatVerisi = "--:--";

// Tahmin
int tahmin_sicaklik[3] = {0,0,0}; 
String tahmin_aciklama[3] = {"--", "--", "--"}; 
String gun_isimleri[3] = {"GUN 1", "GUN 2", "GUN 3"}; 

// IoT Sayfası
int seciliSatir = 1; 
// Cloud Kontrol Değişkenleri
bool yusufOdaState = false; 
bool erenOdaState = false;
bool yatakOdasiState = false;
bool periLedState = false;

// YENİ: Cloud Sensör Değişkenleri
// (Not: Bunlar ThingProperties.h içinde CloudTemperatureSensor ve CloudRelativeHumidity olarak tanımlanmalı)
// Burada yerel simülasyonunu yapıyoruz ama asıl işi Cloud kütüphanesi yapacak.
float odaSicaklikValue = 0.0;
float odaNemValue = 0.0;

// Titreme Önleme
bool e_yusuf = false;
bool e_eren = false;
bool e_yatak = false;
bool e_peri = false;
int e_seciliSatir = -1;

// Kontrol
float eski_t_ic = -999; int eski_h_ic = -999; float eski_t_dis = -999;
String eski_saat = "";
bool sayfaDegisti = true;
bool eski_wifi_durumu = false;

// Buton
int lastButtonState = HIGH;
unsigned long buttonPressTime = 0;
bool buttonIslemYapildi = false; 

void setup() {
  Serial.begin(9600);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP); 

  tft.initR(INITR_BLACKTAB); 
  tft.setRotation(0); 
  tft.fillScreen(RENK_ARKA);
  
  tft.setTextColor(RENK_BEYAZ);
  tft.setTextSize(1);
  tft.setCursor(10, 50);
  tft.print("Baslatiliyor...");

  dht.begin();
  initProperties(); 
  ArduinoCloud.begin(ArduinoIoTPreferredConnection);
  setDebugMessageLevel(2);
  ArduinoCloud.printDebugInfo();

  configTime(0, 0, "pool.ntp.org", "time.google.com");
}

void loop() {
  ArduinoCloud.update();
  unsigned long simdi = millis();
  
  // --- BUTON MANTIĞI ---
  int reading = digitalRead(BUTTON_PIN);
  
  if (reading == LOW && lastButtonState == HIGH) {
    buttonPressTime = simdi;
    buttonIslemYapildi = false; 
  }
  
  if (reading == LOW && !buttonIslemYapildi) {
    if (simdi - buttonPressTime > 600) { 
      buttonIslemYapildi = true; 
      if (aktifSayfa == 3) {
        iotToggle(); 
        buzzerBip(100); 
      }
    }
  }
  
  if (reading == HIGH && lastButtonState == LOW) {
    if (!buttonIslemYapildi) {
      if (aktifSayfa == 3) {
        seciliSatir++;
        if (seciliSatir > 4) {
          seciliSatir = 1; 
          aktifSayfa = 1; 
          sayfaDegisti = true;
        } else {
           ekraniGuncelleSayfa3(); 
        }
      } else {
        aktifSayfa++;
        if (aktifSayfa > 3) aktifSayfa = 1; 
        if (aktifSayfa == 3) seciliSatir = 1; 
        sayfaDegisti = true; 
      }
    }
  }
  lastButtonState = reading;

  // --- SENKRONİZASYON (Işıklar) ---
  bool degisiklikVar = false;
  if(yusufOda != yusufOdaState) { yusufOdaState = yusufOda; degisiklikVar = true; }
  if(erenOda != erenOdaState) { erenOdaState = erenOda; degisiklikVar = true; }
  if(yatakOdasi != yatakOdasiState) { yatakOdasiState = yatakOdasi; degisiklikVar = true; }
  if(periLed != periLedState) { periLedState = periLed; degisiklikVar = true; }
  
  if(degisiklikVar && aktifSayfa == 3) ekraniGuncelleSayfa3();

  // --- SENSÖR OKUMA VE CLOUD GÖNDERME ---
  if (simdi - sonSensorOkuma > sensorGecikme) {
    float yeniT = dht.readTemperature();
    int yeniH = dht.readHumidity();
    if (!isnan(yeniT) && !isnan(yeniH)) {
      t_ic = yeniT; h_ic = yeniH;
      ic_sicaklik = t_ic; ic_nem = h_ic; 
      
      // >>> YENİ KISIM: Google Home'a Gönder <<<
      // Cloud Dashboard'da oluşturduğun değişken isimleri "odaSicaklik" ve "odaNem" olmalı.
      odaSicaklik = t_ic; 
      odaNem = h_ic;
    }
    sonSensorOkuma = simdi;
    if(aktifSayfa == 1) ekraniGuncelleSayfa1(); 
  }

  if (simdi - sonHavaGuncelleme > havaGecikme || sonHavaGuncelleme == 0) {
    if (ArduinoCloud.connected()) {
       anlikHavaGetir();  
       tahminHavaGetir(); 
       gunIsimleriniBul(); 
       sonHavaGuncelleme = simdi;
    }
  }
  
  saatiGetir();

  if (sayfaDegisti) {
    eski_t_ic = -999; eski_h_ic = -999; eski_t_dis = -999; 
    eski_saat = "";
    eski_wifi_durumu = !eski_wifi_durumu;
    e_yusuf = !yusufOdaState; e_seciliSatir = -1; 

    if (aktifSayfa == 1) {
      arayuzSablonuCizSayfa1(); 
      ekraniGuncelleSayfa1();
    }
    else if (aktifSayfa == 2) {
      tft.fillRect(0, 0, 128, 160, RENK_ARKA); 
      arayuzSablonuCizSayfa2();
    }
    else if (aktifSayfa == 3) {
      arayuzSablonuCizSayfa3();
    }
    
    sayfaDegisti = false;
  }
}

// ==========================================
//          SAYFA 1 (EV EKRANI)
// ==========================================
void arayuzSablonuCizSayfa1() {
  tft.fillRect(0, 0, 128, 35, RENK_ARKA);
  tft.fillRect(0, 35, 128, 55, RENK_ARKA); 
  tft.fillRect(0, 90, 128, 8, RENK_ARKA);
  tft.fillRect(0, 98, 128, 55, RENK_ARKA);
  tft.fillRect(0, 153, 128, 7, RENK_ARKA);

  tft.fillRoundRect(4, 35, 120, 55, 8, RENK_MAVI_KUTU);
  tft.fillRoundRect(4, 98, 120, 55, 8, RENK_TURUNCU);
  
  tft.setTextColor(RENK_BEYAZ);
  tft.setFont(&FreeSansBold9pt7b);
  tft.setCursor(10, 52); tft.print("EV");
  tft.setCursor(10, 115); tft.print("DIS");
}

void ekraniGuncelleSayfa1() {
  if (aktifSayfa != 1) return;
  
  if (saatVerisi != eski_saat) {
    tft.fillRect(30, 0, 70, 30, RENK_ARKA); 
    tft.setFont(&FreeSansBold12pt7b); tft.setTextColor(RENK_BEYAZ);
    tft.setTextSize(1); tft.setCursor(30, 25); tft.print(saatVerisi);
    eski_saat = saatVerisi;
  }
  
  bool wifiAktif = (WiFi.status() == WL_CONNECTED);
  if (wifiAktif != eski_wifi_durumu) { wifiLogosuCiz(wifiAktif); eski_wifi_durumu = wifiAktif; }
  
  if (t_ic != eski_t_ic || h_ic != eski_h_ic) {
    tft.fillRoundRect(4, 35, 120, 55, 8, RENK_MAVI_KUTU);
    tft.setTextColor(RENK_BEYAZ);
    tft.setFont(&FreeSansBold9pt7b); tft.setCursor(10, 52); tft.print("EV");
    
    tft.setFont(&FreeSansBold18pt7b); tft.setCursor(15, 82); tft.print((int)t_ic);
    int cx = tft.getCursorX(); tft.drawCircle(cx + 4, 62, 3, RENK_BEYAZ); tft.drawCircle(cx + 4, 62, 4, RENK_BEYAZ);
    tft.setCursor(75, 82); tft.print(h_ic);
    int nemBitisX = tft.getCursorX(); tft.setFont(); tft.setCursor(nemBitisX + 2, 58); tft.print("%");
    eski_t_ic = t_ic; eski_h_ic = h_ic;
  }

  if (t_dis != eski_t_dis) {
    tft.fillRoundRect(4, 98, 120, 55, 8, RENK_TURUNCU);
    tft.setTextColor(RENK_BEYAZ);
    tft.setFont(&FreeSansBold9pt7b); tft.setCursor(10, 115); tft.print("DIS");
    
    tft.setFont(&FreeSansBold18pt7b); tft.setCursor(40, 145); tft.print((int)t_dis);
    int cx = tft.getCursorX(); tft.drawCircle(cx + 4, 125, 3, RENK_BEYAZ); tft.drawCircle(cx + 4, 125, 4, RENK_BEYAZ);
    eski_t_dis = t_dis;
  }
}

// ==========================================
//          SAYFA 3 (IOT EKRANI)
// ==========================================
void iotKutuCiz(int index, String isim, bool durum, bool secili) {
  int yPos = 35 + ((index-1) * 32); 
  tft.fillRect(0, yPos-2, 128, 34, RENK_ARKA); 

  uint16_t kutuRengi = (index % 2 != 0) ? RENK_MAVI_KUTU : RENK_TURUNCU;
  
  if (secili) {
    tft.drawRoundRect(0, yPos-2, 128, 30, 10, RENK_SECIM);
    tft.drawRoundRect(1, yPos-1, 126, 28, 10, RENK_SECIM);
  }

  tft.fillRoundRect(2, yPos, 124, 26, 12, kutuRengi);

  tft.setFont(&FreeSansBold9pt7b);
  tft.setTextColor(RENK_BEYAZ);
  tft.setTextSize(1);
  tft.setCursor(6, yPos + 18);
  tft.print(isim);

  int toggleX = 95; 
  int toggleY = yPos + 5; 
  tft.fillRoundRect(toggleX, toggleY, 24, 16, 8, RENK_KOYU_GRI);

  if (durum) {
    tft.fillCircle(toggleX + 24 - 8, toggleY + 8, 6, RENK_YESIL);
  } else {
    tft.fillCircle(toggleX + 8, toggleY + 8, 6, RENK_KIRMIZI);
  }
}

void arayuzSablonuCizSayfa3() {
  if (aktifSayfa != 3) return;
  tft.fillRect(0, 0, 128, 35, RENK_ARKA); 
  
  tft.setFont();
  tft.setTextColor(RENK_GRI);
  tft.setCursor(10, 10); 
  tft.print("IOT KONTROL EKRANI");

  iotKutuCiz(1, "YUSUF", yusufOdaState, (seciliSatir == 1));
  iotKutuCiz(2, "EREN", erenOdaState, (seciliSatir == 2));
  iotKutuCiz(3, "YATAK", yatakOdasiState, (seciliSatir == 3));
  iotKutuCiz(4, "PERI", periLedState, (seciliSatir == 4));
  
  e_yusuf = yusufOdaState; e_eren = erenOdaState; e_yatak = yatakOdasiState; e_peri = periLedState; e_seciliSatir = seciliSatir;
}

void ekraniGuncelleSayfa3() {
  if (aktifSayfa != 3) return;
  if (yusufOdaState != e_yusuf || seciliSatir != e_seciliSatir) { iotKutuCiz(1, "YUSUF", yusufOdaState, (seciliSatir == 1)); e_yusuf = yusufOdaState; }
  if (erenOdaState != e_eren || seciliSatir != e_seciliSatir) { iotKutuCiz(2, "EREN", erenOdaState, (seciliSatir == 2)); e_eren = erenOdaState; }
  if (yatakOdasiState != e_yatak || seciliSatir != e_seciliSatir) { iotKutuCiz(3, "YATAK", yatakOdasiState, (seciliSatir == 3)); e_yatak = yatakOdasiState; }
  if (periLedState != e_peri || seciliSatir != e_seciliSatir) { iotKutuCiz(4, "PERI", periLedState, (seciliSatir == 4)); e_peri = periLedState; }
  e_seciliSatir = seciliSatir; 
}

void iotToggle() {
  if (seciliSatir == 1) yusufOda = !yusufOda;
  if (seciliSatir == 2) erenOda = !erenOda;
  if (seciliSatir == 3) yatakOdasi = !yatakOdasi;
  if (seciliSatir == 4) periLed = !periLed;
}

void onYusufOdaChange() { } void onErenOdaChange() { } void onYatakOdasiChange() { } void onPeriLedChange() { }

void buzzerBip(int sure) { digitalWrite(BUZZER_PIN, HIGH); delay(sure); digitalWrite(BUZZER_PIN, LOW); }

String havaDurumuCevir(String description) {
  description.toLowerCase();
  if(description.indexOf("clear") >= 0) return "ACIK";
  if(description.indexOf("clouds") >= 0) { if(description.indexOf("few") >= 0 || description.indexOf("scattered") >= 0) return "PARCALI"; return "BULUTLU"; }
  if(description.indexOf("rain") >= 0) return "YAGMUR"; if(description.indexOf("drizzle") >= 0) return "CISELEME";
  if(description.indexOf("snow") >= 0) return "KARLI"; if(description.indexOf("thunder") >= 0) return "FIRTINA"; return "BULUTLU"; 
}
void arayuzSablonuCizSayfa2() {
  tft.setFont(); tft.setTextColor(RENK_GRI); tft.setTextSize(1);
  tft.setCursor(15, 8); tft.print("3 GUNLUK TAHMIN");
  tft.fillRoundRect(2, 20, 124, 45, 6, RENK_MAVI_KUTU); tft.fillRoundRect(2, 68, 124, 45, 6, RENK_MAVI_KUTU); tft.fillRoundRect(2, 116, 124, 45, 6, RENK_TURUNCU);
  for(int i=0; i<3; i++) {
    int boxY = 20 + (i*48); String trDurum = havaDurumuCevir(tahmin_aciklama[i]);
    tft.setFont(&FreeSansBold9pt7b); tft.setTextColor(RENK_BEYAZ); tft.setCursor(6, boxY + 16); tft.print(gun_isimleri[i].substring(0, 3)); 
    tft.setFont(&FreeSansBold12pt7b); tft.setCursor(95, boxY + 20); tft.print(tahmin_sicaklik[i]);
    int cx = tft.getCursorX(); tft.drawCircle(cx+3, boxY + 10, 2, RENK_BEYAZ); tft.drawCircle(cx+3, boxY + 10, 3, RENK_BEYAZ); 
    tft.setFont(&FreeSans9pt7b); tft.setCursor(6, boxY + 36); tft.print(trDurum);
  }
}
void ekraniGuncelleSayfa2() { if (aktifSayfa != 2) return; }
void anlikHavaGetir() { HTTPClient http; http.begin(currentPath); int kod = http.GET(); if (kod > 0) { DynamicJsonDocument doc(1024); deserializeJson(doc, http.getString()); t_dis = doc["main"]["temp"]; dis_sicaklik = t_dis; } http.end(); }
void tahminHavaGetir() {
  HTTPClient http; http.begin(forecastPath); int kod = http.GET();
  if (kod > 0) { DynamicJsonDocument doc(16384); DeserializationError error = deserializeJson(doc, http.getStream());
    if (!error) {
      tahmin_sicaklik[0] = doc["list"][8]["main"]["temp"]; tahmin_aciklama[0] = doc["list"][8]["weather"][0]["main"].as<String>();
      tahmin_sicaklik[1] = doc["list"][16]["main"]["temp"]; tahmin_aciklama[1] = doc["list"][16]["weather"][0]["main"].as<String>();
      tahmin_sicaklik[2] = doc["list"][24]["main"]["temp"]; tahmin_aciklama[2] = doc["list"][24]["weather"][0]["main"].as<String>();
    }
  } http.end();
}
void gunIsimleriniBul() {
  struct tm timeinfo; time_t now = time(nullptr); now += 10800; struct tm * ti = gmtime(&now);
  int bugun = ti->tm_wday; String gunlerTR[] = {"PAZ", "PZT", "SAL", "CRS", "PER", "CUM", "CTS"};
  gun_isimleri[0] = gunlerTR[(bugun + 1) % 7]; gun_isimleri[1] = gunlerTR[(bugun + 2) % 7]; gun_isimleri[2] = gunlerTR[(bugun + 3) % 7];
}
void saatiGetir() {
  time_t now = time(nullptr); if (now > 100000) { now += 10800; 
    struct tm * timeinfo = gmtime(&now); char timeStringBuff[6]; strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M", timeinfo);
    saatVerisi = String(timeStringBuff);
    if(aktifSayfa == 1) ekraniGuncelleSayfa1(); if(aktifSayfa == 3) ekraniGuncelleSayfa3(); 
  }
}
void wifiLogosuCiz(bool bagli) {
  int x = 110; int y = 10; uint16_t renk = bagli ? RENK_YESIL : RENK_KIRMIZI;
  tft.fillRect(x, y, 18, 15, RENK_ARKA); tft.fillRect(x, y+10, 3, 5, renk); tft.fillRect(x+5, y+5, 3, 10, renk); tft.fillRect(x+10, y, 3, 15, renk);
}