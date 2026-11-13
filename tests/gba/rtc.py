#!/usr/bin/env python3
"""Exercise the production serial RTC with time reads and actual clock writes."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'gba-core/gba_memory.c').read_text()
def func(name):
 start=s.index('static void '+name+'(');a=s.index('{',start);depth=1;b=a+1
 while depth:
  depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[start:b]
constants=s[s.index('#define RTC_DISABLED'):s.index('static bool rtc_enabled')]
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
typedef uint8_t u8;typedef uint32_t u32;typedef uint64_t u64;typedef int64_t s64;
#include "rtc_codec.h"
static u8 gpio_regs[3];
static u32 rtc_state,rtc_command,rtc_data_bits,rtc_write_mode,rtc_bit_count,rtc_status=0x40;
static u64 rtc_data;
static int64_t rtc_clock_offset;
static bool rtc_enabled=true;
static time_t rtc_current_time(void) {return 1704067200+rtc_clock_offset;}
static u8 encode_bcd(u8 n) {n%=100;return n/10*16+n%10;}
#define GPIO_RTC_CLK 1
#define GPIO_RTC_DAT 2
#define GPIO_RTC_CSS 4
'''+s[s.index('int retro_get_rtc('):s.index('void rtc_set_time_source(')]+constants+func('write_rtc')+r'''
static unsigned pins;
static void set(unsigned n) {unsigned old=pins;pins=n;gpio_regs[0]=n;write_rtc(old,n);}
static void bit(unsigned v) {set(4|(v<<1));set(5|(v<<1));}
static void command(unsigned cmd) {set(0);set(4);for(int i=7;i>=0;i--)bit((cmd>>i)&1);}
static void write(unsigned cmd,u64 value,unsigned bits) {command(cmd);for(unsigned i=0;i<bits;i++)bit((value>>i)&1);set(0);}
static u64 read(unsigned cmd,unsigned bits) {command(cmd);gpio_regs[1]=5;u64 result=0;for(unsigned i=0;i<bits;i++){bit(0);result|=(u64)((gpio_regs[0]>>1)&1)<<i;}set(0);gpio_regs[1]=7;return result;}
int main(void) {
 setenv("TZ","UTC",1);tzset();gpio_regs[1]=7;
 assert(read(0x63,8)==0x40);
 write(0x62,0x40,8);assert(read(0x63,8)==0x40);
 assert(read(0x65,56)==0x00000001010124ULL);
 write(0x66,0x563412,24);assert(read(0x67,24)==0x563412);
 write(0x64,0x59582302291226ULL,56);assert(read(0x65,56)==0x59582302291226ULL);
 for(unsigned h=0;h<24;h++)for(unsigned status=0;status<=0x40;status+=0x40)
  assert(rtc_decode_hour(rtc_encode_hour(h,status),status)==h);
 u32 durable[3];assert(retro_get_rtc(durable));
 rtc_clock_offset=0;rtc_status=0;retro_set_rtc(durable);
 assert(read(0x65,56)==0x59582302291226ULL);
 rtc_clock_offset=-12345678901LL;rtc_status=0x40;
 assert(retro_get_rtc(durable));rtc_clock_offset=0;retro_set_rtc(durable);
 assert(rtc_clock_offset==-12345678901LL && rtc_status==0x40);
 rtc_enabled=false;assert(!retro_get_rtc(durable));rtc_enabled=true;
 command(0x60);assert(rtc_state==RTC_IDLE && rtc_status==0x40);
 puts("RTC GPIO status, date/time read/write, 12/24-hour BCD and reset: PASS");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'rtc.c').write_text(code)
 subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'gba-core'),str(p/'rtc.c'),'-o',str(p/'rtc')],check=True)
 subprocess.run([str(p/'rtc')],check=True)
