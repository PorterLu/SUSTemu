#include <am.h>

#define CONFIG_TIMER_MMIO 0xa0000048

static uint64_t boot_time;

static uint64_t get_time(){
	uint64_t time = *(volatile uint32_t *)(CONFIG_TIMER_MMIO);
	time |= ((uint64_t)(*(volatile uint32_t *)(CONFIG_TIMER_MMIO + 4)) << 32);
	return time;
}

void __am_timer_init() {
	boot_time = get_time();
}

void __am_timer_uptime(AM_TIMER_UPTIME_T *uptime) {
	uptime->us = get_time() - boot_time;
}

void __am_timer_rtc(AM_TIMER_RTC_T *rtc) {
  rtc->second = 0;
  rtc->minute = 0;
  rtc->hour   = 0;
  rtc->day    = 0;
  rtc->month  = 0;
  rtc->year   = 1900;
}
