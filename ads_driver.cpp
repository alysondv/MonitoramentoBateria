#include "ads_driver.h"

static Adafruit_ADS1115 ads;
static float kDiv[4];
constexpr uint8_t N_OV = 8;
constexpr float LSB   = 0.1875f; // mV/bit @ PGA ±6,144 V

bool ADS_begin(uint8_t sda, uint8_t scl, uint32_t hz){
    Wire.begin(sda, scl, hz);
    if(!ads.begin(0x48)) return false;
    ads.setGain(GAIN_TWOTHIRDS);
    ads.setDataRate(RATE_ADS1115_475SPS);
    return true;
}
void ADS_setDividers(const float k[4]){ for(int i=0;i<4;i++) kDiv[i]=k[i]; }

static int16_t readAvg(uint8_t ch){
    int32_t acc=0; int16_t v;
    for(uint8_t i=0;i<N_OV;i++){
        v = ads.readADC_SingleEnded(ch);
        acc += v;
        delayMicroseconds(1000/N_OV);
    }
    return acc/N_OV;
}

bool ADS_getSample(CellSample &s){
    int16_t raw[4];
    for(uint8_t i=0;i<4;i++) raw[i]=readAvg(i);
    float absV[4];
    for(uint8_t i=0;i<4;i++) absV[i]=raw[i]*LSB*kDiv[i];
    s.epochMs = millis();
    s.mv[0]=absV[0];
    s.mv[1]=absV[1]-absV[0];
    s.mv[2]=absV[2]-absV[1];
    s.mv[3]=absV[3]-absV[2];
    s.total = absV[3];
    return true;
}