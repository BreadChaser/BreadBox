/* S-3511 cartridge RTC BCD fields. GPL-2.0-or-later. */
#ifndef GPSP_RTC_CODEC_H
#define GPSP_RTC_CODEC_H
static unsigned rtc_decode_bcd(unsigned b) { return (b >> 4) * 10 + (b & 15); }
static unsigned rtc_decode_hour(unsigned b,unsigned status) {
  unsigned hour=rtc_decode_bcd(b & 0x7f);
  return status & 0x40 ? hour : hour % 12 + ((b & 0x80) ? 12 : 0);
}
static unsigned rtc_encode_hour(unsigned hour,unsigned status) {
  unsigned pm=hour>=12;
  if(!(status & 0x40)) { hour%=12; if(!hour)hour=12; }
  return ((hour/10)*16 + hour%10) | (!(status&0x40) && pm ? 0x80 : 0);
}
#endif
