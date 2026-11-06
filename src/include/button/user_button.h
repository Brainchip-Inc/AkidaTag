#ifndef USER_BUTTON_H
#define USER_BUTTON_H
/* ---------- Timing config ---------- */
#define HOLD_TIME_MS_SHORT 3000   /* 3-5s long press */
#define HOLD_TIME_MS_LONG  8000   /* 8-10s long press */
#define DOUBLE_CLICK_MS 500

int user_button_init(void);

#endif