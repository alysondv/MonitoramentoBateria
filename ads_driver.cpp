#include "ads_driver.h"
#include <Wire.h>

static Adafruit_ADS1115 ads;
static constexpr uint8_t N = 8;           // oversampling
static constexpr float   LSB = 0.1875f;   // mV/bit @ ±6,144 V
static float kDiv[4] = {1.042f,2.109f,3.023f,4.033f};

void ADS_setKDiv(const float *k){ memcpy(kDiv,k,4*sizeof(float)); }

static bool readSafe(uint8_t ch,int16_t &val){
    for(uint8_t t=0;t<3;++t){
        val = ads.readADC_SingleEnded(ch);
        if(val!=0xFFFF) return true;
        delay(2); ads.begin(0x48);
    }
    return false;
}

void ADS_init(){
    Wire.begin(42,41,50000);            // 50 kHz
    ads.begin(0x48);
    ads.setGain(GAIN_TWOTHIRDS);        // ±6,144 V
}

bool ADS_getSample(CellSample &out){
    int32_t acc[4]={0}; int16_t raw;
    for(uint8_t i=0;i<N;i++){
        for(uint8_t ch=0;ch<4;ch++) if(readSafe(ch,raw)) acc[ch]+=raw; else return false;
        delayMicroseconds(125);
    }
    out.epochMs = millis();
    uint16_t vAbs[4];
    for(uint8_t ch=0;ch<4;ch++) vAbs[ch] = (uint16_t)((acc[ch]/N)*LSB*kDiv[ch]);
    out.mv[0]=vAbs[0]; 
    out.mv[1]=vAbs[1]-vAbs[0]; 
    out.mv[2]=vAbs[2]-vAbs[1]; 
    out.mv[3]=vAbs[3]-vAbs[2];
    out.total = vAbs[3];
    return true;
}
void ADS_raw(int16_t *arr){ for(uint8_t i=0;i<4;i++) arr[i]=ads.readADC_SingleEnded(i); }