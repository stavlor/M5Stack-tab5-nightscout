#pragma once
namespace wifisignal {
// Match the Core2 thresholds; RSSI >= 0 means no usable signal measurement.
inline unsigned bars(bool connected,int rssi){return !connected||rssi>=0?0:rssi>=-55?4:rssi>=-67?3:rssi>=-75?2:1;}
inline const char *quality(unsigned value){return value>=4?"Excellent":value==3?"Good":value==2?"Fair":value==1?"Weak":"Signal unavailable";}
}
