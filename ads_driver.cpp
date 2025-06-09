#include "ads_driver.h"
#include <Wire.h>

static Adafruit_ADS1115 ads;
static constexpr uint8_t N = 8;                     // oversample   
static constexpr float   LSB = 0.1875f;             // mV/bit @ 6.144 V
static constexpr float   kDiv[4] = {1.042f,2.109f,3.023f,4.033f};
static constexpr uint8_t MAX_RETRY = 3;
static constexpr uint32_t I2C_CLK = 50000;          // 50 kHz

bool ADS_init(){
    Wire.begin(42,41,I2C_CLK);
    if(!ads.begin(0x48)) return false;
    ads.setGain(GAIN_TWOTHIRDS);
    ads.setDataRate(RATE_ADS1115_475SPS);
    return true;
}

static bool readSafe(uint8_t ch, int16_t &val){
    for(uint8_t r=0;r<MAX_RETRY;r++){
        val = ads.readADC_SingleEnded(ch);
        if(val!=0xFFFF) return true;        // ok
        delay(2);                           // NACK -> aguarda
    }
    Serial.printf("[I2C] falha canal %u\n",ch);
    return false;
}

static uint16_t cellMv(const int16_t *raw, int i){
    return uint16_t(raw[i]*LSB*kDiv[i]);
}

CellSample ADS_read(){
    int32_t acc[4] = {0};
    int16_t raw[4]  = {0};

    for(uint8_t s=0;s<N;s++){
        for(uint8_t ch=0;ch<4;ch++){
            int16_t v;
            if(readSafe(ch,v)) acc[ch]+=v;
            delayMicroseconds(125);
        }
    }
    for(int i=0;i<4;i++) raw[i]=acc[i]/N;

    CellSample sm{};
    sm.epochMs=millis();
    uint16_t absMv[4];
    for(int i=0;i<4;i++) absMv[i]=cellMv(raw,i);
    sm.mv[0]=absMv[0];
    sm.mv[1]=absMv[1]-absMv[0];
    sm.mv[2]=absMv[2]-absMv[1];
    sm.mv[3]=absMv[3]-absMv[2];
    sm.total = absMv[3];
    return sm;
}
