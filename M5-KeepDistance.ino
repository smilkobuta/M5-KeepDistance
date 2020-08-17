#pragma mark - Depend ESP8266Audio, ESP8266_Spiram, vl53l0x-arduino, MedianFilter libraries
/* 
cd ~/Arduino/libraries
git clone https://github.com/earlephilhower/ESP8266Audio
git clone https://github.com/Gianbacchio/ESP8266_Spiram
git clone https://github.com/pololu/vl53l0x-arduino
git clone https://github.com/daPhoosa/MedianFilter
*/

#include <M5Stack.h>
#include <WiFi.h>
#include "AudioFileSourceSD.h"
#include "AudioFileSourceID3.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"
#include <Wire.h>
#include "VL53L0X.h"
#include <driver/dac.h>
#include <MedianFilter.h>

// 設定項目
char* sound_list[] = {"random", "yukkuri1", "yukkuri2", "yukkuri3", "yukkuri4"};
int distance_list[] = {30, 40, 50, 60, 70, 80};
int8_t sound_selected = 0;
uint8_t sound_list_num = 5;
int8_t distance_selected = 1;
uint8_t distance_list_num = 6;

// 設定位置
uint8_t pos = 0;

// センサー
VL53L0X sensor;
MedianFilter medianFilter(10, 0);
unsigned long last_sensor = 0;
float distance = -1;

// 距離チェック
unsigned long start_checked = 0;
unsigned long last_checked = 0;
String distance_checked;

// 音声
AudioGeneratorMP3 *mp3;
AudioFileSourceSD *file;
AudioOutputI2S *out;
AudioFileSourceID3 *id3;

// ちらつき防止
TFT_eSprite screen = TFT_eSprite(&M5.Lcd);

void setup()
{
  M5.begin();
  WiFi.mode(WIFI_OFF); 

  Serial.begin(115200);
  Serial.println("Serial start");
  
  // Power chipがgpio21, gpio22, I2Cにつながれたデバイスに接続される。
  // バッテリー動作の場合はこの関数を読んでください（バッテリーの電圧を調べるらしい）
  M5.Power.begin();
  
  Wire.begin();// I2C通信を開始する
  sensor.init();
  sensor.setTimeout(0);
  sensor.startContinuous(1000);

  // Sprite初期化
  screen.setColorDepth(8);
  screen.createSprite(320, 240);
  screen.fillSprite(getColor(50, 100, 110));
  screen.pushSprite(0, 0);
  
  // ノイズ対策
  /*
  dacWrite(25, 0);
  dac_output_disable(DAC_CHANNEL_1);
  dac_i2s_disable();
  dac_output_voltage(DAC_CHANNEL_1, 1);
  */
  
  delay(500);

  update_display();
}

void loop()
{
  M5.update();

  int updated = 0;
  int sound_check = 0;

  // ボタン操作
  if (M5.BtnA.wasPressed()) {
    // 切り替え
    pos = pos == 0 ? 1 : 0;
    updated = 1;
  } else if (M5.BtnB.wasPressed()) {
    // 左ボタン
    if (pos == 0) {
      sound_selected --;
      if (sound_selected < 0) {
        sound_selected = sound_list_num - 1;
      }
      sound_check = 1;
    } else {
      distance_selected --;
      if (distance_selected < 0) {
        distance_selected = distance_list_num - 1;
      }
    }
    updated = 1;
  } else if (M5.BtnC.wasPressed()) {
    // 右ボタン
    if (pos == 0) {
      sound_selected ++;
      if (sound_selected >= sound_list_num) {
        sound_selected = 0;
      }
      sound_check = 1;
    } else {
      distance_selected ++;
      if (distance_selected >= distance_list_num) {
        distance_selected =  0;
      }
    }
    updated = 1;
  }

  // 距離の計測
  if (distance == -1 || millis() - last_sensor > 1 * 1000) {
    last_sensor = millis();

    // MedianFilterを使ってノイズ軽減
//    float distance_new = sensor.readRangeSingleMillimeters();
    float distance_new, distance_new_read = sensor.readRangeContinuousMillimeters();
    medianFilter.in(distance_new_read);
    distance_new = medianFilter.out();
    Serial.println("distance_new=" + String(distance_new));
    
    if (distance_new > 0 && distance_new < 1000) {
      distance = distance_new;
      updated = 1;
      check_distance();
    } else {
      distance = 0;
      updated = 1;
    }
    
    if (sensor.timeoutOccurred()) {
//      M5.Lcd.setCursor(0, 0);
//      M5.Lcd.println("TIMEOUT");
    } else {
//      M5.Lcd.setCursor(0, 0);
//      M5.Lcd.println("        ");  
    }
  }

  if (updated == 1) {
    update_display();
  }
  
  if (sound_check == 1) {
    // 再生
    String sound_filename = sound_list[sound_selected];
    if (sound_filename != "random") {
      String sound_mp3 = "/" + sound_filename + ".mp3";
      play_mp3(sound_mp3.c_str());
    }
  }
}

void update_display()
{
  Serial.println("update_display()");
  
  screen.fillScreen(getColor(50, 100, 110));
  screen.setTextColor(BLACK);
  screen.setTextFont(4); // 26ピクセルASCIIフォント

  uint16_t button_border1 = getColor(40, 100, 160);
  uint16_t button_border2 = getColor(200, 220, 60);
  uint16_t button_bg1 = getColor(220, 200, 160);
  uint16_t distance_bg1 = getColor(40, 140, 160);
  uint16_t font_offset = 26 / 2 - 3;
  uint16_t font_id = 4;

  // 距離の表示
  char distance_label[64];
  snprintf(distance_label, sizeof distance_label, "%.1f", distance / 10);
  
  screen.fillRect(60, 40, 200, 40, distance_bg1);
  if (distance > 0) {
    screen.drawCentreString(String(distance_label) + "cm", 160, 60 - font_offset, font_id);
  } else {
    screen.drawCentreString("---", 160, 60 - font_offset, font_id);
  }

  // 四角
  if (pos == 0) {
    // サウンド設定・・・サウンド名
    screen.fillRect(60, 100, 200, 40, button_border2);
    screen.fillRect(62, 102, 196, 36, button_bg1);
    
    // サウンド設定・・・左ボタン
    screen.fillRect(20, 100, 30, 40, button_border2);
    screen.fillRect(22, 102, 26, 36, button_bg1);
    
    // サウンド設定・・・右ボタン
    screen.fillRect(270, 100, 30, 40, button_border2);
    screen.fillRect(272, 102, 26, 36, button_bg1);
    
    // 距離設定・・・距離
    screen.fillRect(60, 160, 200, 40, button_border1);
    screen.fillRect(61, 161, 198, 38, button_bg1);
    
    // 距離設定・・・左ボタン
    screen.fillRect(20, 160, 30, 40, button_border1);
    screen.fillRect(21, 161, 28, 38, button_bg1);
    
    // 距離設定・・・右ボタン
    screen.fillRect(270, 160, 30, 40, button_border1);
    screen.fillRect(271, 161, 28, 38, button_bg1);
  } else {
    // サウンド設定・・・サウンド名
    screen.fillRect(60, 100, 200, 40, button_border1);
    screen.fillRect(61, 101, 198, 38, button_bg1);
    
    // サウンド設定・・・左ボタン
    screen.fillRect(20, 100, 30, 40, button_border1);
    screen.fillRect(21, 101, 28, 38, button_bg1);
    
    // サウンド設定・・・右ボタン
    screen.fillRect(270, 100, 30, 40, button_border1);
    screen.fillRect(271, 101, 28, 38, button_bg1);
    
    // 距離設定・・・距離
    screen.fillRect(60, 160, 200, 40, button_border2);
    screen.fillRect(62, 162, 196, 36, button_bg1);
    
    // 距離設定・・・左ボタン
    screen.fillRect(20, 160, 30, 40, button_border2);
    screen.fillRect(22, 162, 26, 36, button_bg1);
    
    // 距離設定・・・右ボタン
    screen.fillRect(270, 160, 30, 40, button_border2);
    screen.fillRect(272, 162, 26, 36, button_bg1);
  }

  // 文字列
  // サウンド設定・・・サウンド名
  screen.drawCentreString(sound_list[sound_selected], 160, 120 - font_offset, font_id);

  // サウンド設定・・・左ボタン
  screen.drawCentreString("<", 35, 120 - font_offset, font_id);
  
  // サウンド設定・・・右ボタン
  screen.drawCentreString(">", 285, 120 - font_offset, font_id);
  
  // 距離設定・・・距離
  screen.drawCentreString(String(distance_list[distance_selected]) + "cm", 160, 180 - font_offset, font_id);
  
  // 距離設定・・・左ボタン
  screen.drawCentreString("<", 35, 180 - font_offset, font_id);
  
  // 距離設定・・・右ボタン
  screen.drawCentreString(">", 285, 180 - font_offset, font_id);

//  screen.setCursor(0, 0);
//  screen.print(distance);
//  screen.print(":");
//  screen.print(distance_list[distance_selected]);
//  screen.print(":");
//  screen.print(distance_checked);
  
  screen.pushSprite(0, 0);
}

void check_distance()
{
  if (distance > 0 && distance / 10 < distance_list[distance_selected]) {
    // 近い
    distance_checked = "too close";

    unsigned long now = millis();
    if (start_checked == 0) {
      start_checked = now;
    }
    if (now - start_checked > 30 * 1000) {
      // beep
      distance_checked = "beep";
      start_checked = 0;
    
      String sound_filename = sound_list[sound_selected];
      if (sound_filename == "random") {
        sound_filename = sound_list[random(1, sound_list_num)];
      }
      String sound_mp3 = "/" + sound_filename + ".mp3";
      play_mp3(sound_mp3.c_str());
    }
  } else {
    // OK
    distance_checked = "ok";
    start_checked = 0;
  }
}

void play_mp3(const char *sound_mp3)
{
  Serial.println(sound_mp3);
  delay(1000);
  file = new AudioFileSourceSD(sound_mp3);
  id3 = new AudioFileSourceID3(file);
  out = new AudioOutputI2S(0, 1); // Output to builtInDAC
  out->SetOutputModeMono(true);
  out->SetGain(0.5);
  mp3 = new AudioGeneratorMP3();
  mp3->begin(id3, out);
  while(mp3->isRunning()) {
    if (!mp3->loop()) mp3->stop();
  }
}

uint16_t getColor(uint8_t red, uint8_t green, uint8_t blue){
  return ((red>>3)<<11) | ((green>>2)<<5) | (blue>>3);
}
