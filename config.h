#pragma once
struct Calib{ float kDiv[4]; };
bool CFG_load(Calib &c);
void CFG_save(const Calib &c);
