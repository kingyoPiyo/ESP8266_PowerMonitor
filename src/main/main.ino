 /********************************************************
 * Title    : ESP8266 Power Monitor
 * Date     : 2020/05/03
 * Design   : kingyo
 ********************************************************/
#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <WiFiUDP.h>
#include "Ambient.h"

//=====================================================================================
// Define
//=====================================================================================
// 計測周期[ms]
#define DEF_MEAS_CYCLE_MS   (1000)
// Ambient投稿間隔[sec]
#define DEF_PERIOD          (1 * 60)
// LED
#define DEF_LED_PIN         (16)
// 計量パルス入力ピン
#define DEF_PULSE_IN_PIN    (14)
// 計量パルススケーリング値
#define DEF_PULSE_SCALE     (12)

// Wifi, Ambient, UDP接続設定
const char* ssid                    = "hogehoge";           // WiFi SSID
const char* password                = "*************";      // WiFi パスワード
unsigned int channelId              =  12345;               // Ambient チャネルID
const char* writeKey                = "****************";   // Ambient ライトキー
static const char* UdpIpadr         = "192.168.37.255";     // UDP送信先アドレス（この例では192.168.37.0/24範囲内にブロードキャスト）
static const int UdpPortTx          =  1024;                // 送信先ポート
static const int UdpPortRx          =  1025;                // ESP8266のリッスンポート

//=====================================================================================
// Global
//=====================================================================================
static WiFiUDP wifiUdp; 
static WiFiClient client;
static Ambient ambient;
static Ticker ticker_measTrig;
static volatile bool meas_trig_en = false;
static volatile uint32_t pulse_count = 0;
static uint32_t pulse_count_sum = 0;
static uint32_t pulse_count_ambient = 0;
static int led_state = 0;

//=====================================================================================
// タイマ割り込み  計測トリガ生成用
//=====================================================================================
void meas_trig_irq()
{
    meas_trig_en = true;
}

//=====================================================================================
// 外部割り込み  計量パルス立ち上がり検出
//=====================================================================================
void ICACHE_RAM_ATTR pcount_irq()
{
    pulse_count++;
}

//=====================================================================================
// 初期設定
//=====================================================================================
void setup()
{
    // I/Oピン設定
    pinMode(DEF_LED_PIN, OUTPUT);
    pinMode(DEF_PULSE_IN_PIN, INPUT);

    // シリアル通信を開始
    Serial.begin(115200);
    Serial.println("Serial start.");

    // wifi初期化
    wifi_set_sleep_type(LIGHT_SLEEP_T);
    WiFi.begin(ssid, password);
    Serial.println("WiFi start.");

    // Wait for connection
    while (WiFi.status() != WL_CONNECTED) {
        digitalWrite(DEF_LED_PIN, led_state);
        led_state = 1 - led_state;
        Serial.print(".");
        delay(200);
    }

    // WiFi接続状態表示（デバッグ用）
    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // チャネルIDとライトキーを指定してAmbientの初期化
    ambient.begin(channelId, writeKey, &client);
    Serial.println("Ambient start.");

    // UDP受信設定
    wifiUdp.begin(UdpPortRx);

    // 割り込み設定
    ticker_measTrig.attach_ms(DEF_MEAS_CYCLE_MS, meas_trig_irq);
    attachInterrupt(DEF_PULSE_IN_PIN, pcount_irq, RISING);
}

//=====================================================================================
// 計測処理
//=====================================================================================
void meas_main()
{
    static int meas_count = 0;
    uint32_t pulse_count_tmp = pulse_count;

    pulse_count = 0;
    pulse_count_sum += pulse_count_tmp;

    // 動作状態監視用Lチカ
    digitalWrite(DEF_LED_PIN, led_state);
    led_state = 1 - led_state;

    // UDP送信(ASCII)
    wifiUdp.beginPacket(UdpIpadr, UdpPortTx);
    wifiUdp.print(String(pulse_count_tmp * DEF_PULSE_SCALE)+"[W]  " + String((float)pulse_count_sum * DEF_PULSE_SCALE / 3600) + "[Wh]");
    wifiUdp.endPacket();

    // Ambient連携
    pulse_count_ambient += pulse_count_tmp;
    if (++meas_count >= DEF_PERIOD)
    {
        // Ambientに投稿
        ambient.set(1, (float)pulse_count_ambient * DEF_PULSE_SCALE / DEF_PERIOD);
        ambient.send();
        pulse_count_ambient = 0;
        meas_count = 0;
    }
}

//=====================================================================================
// UDP受信処理
//=====================================================================================
void processUdpReceive()
{
    char receivedBuffer[255];
    
    int rcvdSize = wifiUdp.parsePacket();
    if (rcvdSize == 0) {
        return;
    }

    int len = wifiUdp.read(receivedBuffer, 255);
    if (len == 0) {
        return;
    }

    // 'C'受信で積算値をクリアする
    if (receivedBuffer[0] == 'C') {
        pulse_count_sum = 0;
    }
}

//=====================================================================================
// メインループ
//=====================================================================================
void loop()
{
    if (meas_trig_en)
    {
        meas_trig_en = false;
        meas_main(); 
    }
    processUdpReceive();
}
