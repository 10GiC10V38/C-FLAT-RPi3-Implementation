/*
 * test_syringe.c — Portable syringe pump benchmark for C-FLAT validation
 *
 * Adapted from: reference/c-flat/samples/syringe/syringePump.c
 * (Open Syringe Pump - https://github.com/naroom/OpenSyringePump)
 *
 * All hardware dependencies are stubbed. Serial input is simulated
 * from a command table. After each command, cflat_finalize_and_print()
 * is called so we can compare loop counts/iterations against the
 * reference C-FLAT attestation output (syringe-auth.txt).
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../../instrumentation/runtime/libcflat.h"

/* ===================================================================
 * Hardware stubs
 * =================================================================== */

/* GPIO */
enum { INPUT_MODE, OUTPUT_MODE };
enum { HIGH_VAL = 0, LOW_VAL = 1 };

void pinMode(int pin, int mode)       { (void)pin; (void)mode; }
void digitalWrite(int pin, int value) { (void)pin; (void)value; }
int  digitalRead(int pin)             { (void)pin; return HIGH_VAL; }
int  analogRead(int pin)              { (void)pin; return 999; } /* KEY_NONE */

/* LED */
void led_on(void)  {}
void led_off(void) {}

/* Timing */
static unsigned long fake_millis = 0;
unsigned long millis(void) { return fake_millis++; }
void delayMicroseconds(float usecs) { (void)usecs; }

/* LCD */
typedef struct { unsigned int id; } LiquidCrystal;
void lcd_begin(LiquidCrystal *lcd, unsigned int cols, unsigned int rows) {
    (void)lcd; (void)cols; (void)rows;
}
void lcd_clear(LiquidCrystal *lcd) { (void)lcd; }
void lcd_print(LiquidCrystal *lcd, char *output, int len) {
    (void)lcd;
    printf("%.*s\n", len, output);
}
void lcd_setCursor(LiquidCrystal *lcd, int x, int y) {
    (void)lcd; (void)x; (void)y;
}

/* Serial — simulated from command buffer */
static const char *serial_buf = NULL;
static int serial_pos = 0;
static int serial_len = 0;

void Serial_begin(int baud) { (void)baud; }

int Serial_available(void) {
    return (serial_buf != NULL && serial_pos < serial_len);
}

int Serial_read(void) {
    if (serial_buf && serial_pos < serial_len) {
        return (int)(unsigned char)serial_buf[serial_pos++];
    }
    return -1;
}

int Serial_write(char *output, int len) {
    printf("%.*s", len, output);
    return len;
}

/* Feed a command string into the simulated serial buffer */
static void feed_serial(const char *cmd) {
    serial_buf = cmd;
    serial_pos = 0;
    serial_len = (int)strlen(cmd);
}

/* Utility */
int toUInt(char *input, int len) {
    int result = 0;
    for (int i = 0; i < len; i++) {
        if (input[i] >= '0' && input[i] <= '9') {
            result = result * 10 + (input[i] - '0');
        } else {
            return 0;
        }
    }
    return result;
}

/* ===================================================================
 * Syringe pump logic (from reference syringePump.c)
 * =================================================================== */

#define SYRINGE_VOLUME_ML 30.0
#define SYRINGE_BARREL_LENGTH_MM 80.0
#define THREADED_ROD_PITCH 1.25
#define STEPS_PER_REVOLUTION 200.0
#define MICROSTEPS_PER_STEP 16.0
#define SPEED_MICROSECONDS_DELAY 100

#define false 0
#define true  1
#define boolean _Bool
#define three_dec_places(x) ((int)((x*1e3)+0.5 - (((int)x)*1e3)))

long ustepsPerMM = MICROSTEPS_PER_STEP * STEPS_PER_REVOLUTION / THREADED_ROD_PITCH;
long ustepsPerML = (MICROSTEPS_PER_STEP * STEPS_PER_REVOLUTION * SYRINGE_BARREL_LENGTH_MM)
                   / (SYRINGE_VOLUME_ML * THREADED_ROD_PITCH);

/* Pin definitions */
int motorDirPin = 2;
int motorStepPin = 3;
int triggerPin = 0;
int bigTriggerPin = 0;

/* Keypad */
int adc_key_val[5] = {30, 150, 360, 535, 760};
enum { KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_SELECT, KEY_NONE };
int NUM_KEYS = 5;
int adc_key_in;
int key = KEY_NONE;

/* Enums */
enum { PUSH, PULL };
enum { MAIN, BOLUS_MENU };

const int mLBolusStepsLength = 9;
float mLBolusSteps[9] = {0.001, 0.005, 0.010, 0.050, 0.100, 0.500, 1.000, 5.000, 10.000};

/* State */
float mLBolus = 0.500;
float mLBigBolus = 1.000;
float mLUsed = 0.0;
int mLBolusStepIdx = 3;
float mLBolusStep = 0.050;

long stepperPos = 0;
char charBuf[16];

long lastKeyRepeatAt = 0;
long keyRepeatDelay = 400;
long keyDebounce = 125;
int prevKey = KEY_NONE;

int uiState = MAIN;

int prevBigTrigger = HIGH_VAL;
int prevTrigger = HIGH_VAL;

char serialStr[80] = "";
boolean serialStrReady = false;
int serialStrLen = 0;

LiquidCrystal lcd;

/* Forward declarations */
void checkTriggers(void);
void readSerial(void);
void processSerial(void);
void bolus(int direction);
void readKey(void);
void doKeyAction(unsigned int key);
void updateScreen(void);
int get_key(unsigned int input);

void setup(void) {
    lcd_begin(&lcd, 16, 2);
    lcd_clear(&lcd);
    lcd_print(&lcd, "SyringePump v2.0", 16);

    pinMode(triggerPin, INPUT_MODE);
    pinMode(bigTriggerPin, INPUT_MODE);
    digitalWrite(triggerPin, HIGH_VAL);
    digitalWrite(bigTriggerPin, HIGH_VAL);

    pinMode(motorDirPin, OUTPUT_MODE);
    pinMode(motorStepPin, OUTPUT_MODE);

    Serial_begin(57600);
}

void checkTriggers(void) {
    int pushTriggerValue = digitalRead(triggerPin);
    if (pushTriggerValue == HIGH_VAL && prevTrigger == LOW_VAL) {
        bolus(PUSH);
        updateScreen();
    }
    prevTrigger = pushTriggerValue;

    int bigTriggerValue = digitalRead(bigTriggerPin);
    if (bigTriggerValue == HIGH_VAL && prevBigTrigger == LOW_VAL) {
        float mLBolusTemp = mLBolus;
        mLBolus = mLBigBolus;
        bolus(PUSH);
        mLBolus = mLBolusTemp;
        updateScreen();
    }
    prevBigTrigger = bigTriggerValue;
}

void readSerial(void) {
    while (Serial_available()) {
        char inChar = (char)Serial_read();
        if (inChar < 0x20) {
            serialStrReady = true;
        } else {
            serialStr[serialStrLen] = inChar;
            serialStrLen++;
        }
    }
}

void processSerial(void) {
    if (serialStr[0] == '+') {
        bolus(PUSH);
        updateScreen();
    }
    else if (serialStr[0] == '-') {
        bolus(PULL);
        updateScreen();
    }
    else if (toUInt(serialStr, serialStrLen) != 0) {
        int uLbolus = toUInt(serialStr, serialStrLen);
        mLBolus = (float)uLbolus / 1000.0;
        updateScreen();
    }
    else {
        Serial_write("Invalid command: [", 18);
        Serial_write(serialStr, serialStrLen);
        Serial_write("]\n", 2);
    }
    serialStrReady = false;
    serialStrLen = 0;
}

void bolus(int direction) {
    long steps = (long)(mLBolus * ustepsPerML);
    if (direction == PUSH) {
        led_on();
        digitalWrite(motorDirPin, HIGH_VAL);
        steps = (long)(mLBolus * ustepsPerML);
        mLUsed += mLBolus;
    }
    else if (direction == PULL) {
        led_off();
        digitalWrite(motorDirPin, LOW_VAL);
        if ((mLUsed - mLBolus) > 0) {
            mLUsed -= mLBolus;
        } else {
            mLUsed = 0;
        }
    }

    float usDelay = SPEED_MICROSECONDS_DELAY;

    for (long i = 0; i < steps; i++) {
        digitalWrite(motorStepPin, HIGH_VAL);
        delayMicroseconds(usDelay);
        digitalWrite(motorStepPin, LOW_VAL);
        delayMicroseconds(usDelay);
    }
}

void readKey(void) {
    adc_key_in = analogRead(0);
    key = get_key(adc_key_in);

    long currentTime = millis();
    long timeSinceLastPress = (currentTime - lastKeyRepeatAt);

    boolean processThisKey = false;
    if (prevKey == key && timeSinceLastPress > keyRepeatDelay) {
        processThisKey = true;
    }
    if (prevKey == KEY_NONE && timeSinceLastPress > keyDebounce) {
        processThisKey = true;
    }
    if (key == KEY_NONE) {
        processThisKey = false;
    }

    prevKey = key;

    if (processThisKey) {
        doKeyAction(key);
        lastKeyRepeatAt = currentTime;
    }
}

void doKeyAction(unsigned int key) {
    if (key == KEY_NONE) return;

    if (key == KEY_SELECT) {
        if (uiState == MAIN) {
            uiState = BOLUS_MENU;
        }
        else if (uiState == BOLUS_MENU) {
            uiState = MAIN;
        }
    }

    if (uiState == MAIN) {
        if (key == KEY_LEFT)  bolus(PULL);
        if (key == KEY_RIGHT) bolus(PUSH);
        if (key == KEY_UP)    mLBolus += mLBolusStep;
        if (key == KEY_DOWN) {
            if ((mLBolus - mLBolusStep) > 0)
                mLBolus -= mLBolusStep;
            else
                mLBolus = 0;
        }
    }
    else if (uiState == BOLUS_MENU) {
        if (key == KEY_UP) {
            if (mLBolusStepIdx < mLBolusStepsLength - 1) {
                mLBolusStepIdx++;
                mLBolusStep = mLBolusSteps[mLBolusStepIdx];
            }
        }
        if (key == KEY_DOWN) {
            if (mLBolusStepIdx > 0) {
                mLBolusStepIdx -= 1;
                mLBolusStep = mLBolusSteps[mLBolusStepIdx];
            }
        }
    }

    updateScreen();
}

void updateScreen(void) {
    char s1[80];
    char s2[80];

    if (uiState == MAIN) {
        sprintf(s1, "Used %d.%d mL", (int)mLUsed, three_dec_places(mLUsed));
        sprintf(s2, "Bolus %d.%d mL", (int)mLBolus, three_dec_places(mLBolus));
    }
    else if (uiState == BOLUS_MENU) {
        sprintf(s1, "Menu> BolusStep");
        sprintf(s2, "%d.%d", (int)mLBolusStep, three_dec_places(mLBolusStep));
    }

    lcd_clear(&lcd);
    lcd_setCursor(&lcd, 0, 0);
    lcd_print(&lcd, s1, (int)strlen(s1));
    lcd_setCursor(&lcd, 0, 1);
    lcd_print(&lcd, s2, (int)strlen(s2));
}

int get_key(unsigned int input) {
    int k;
    for (k = 0; k < NUM_KEYS; k++) {
        if (input < (unsigned int)adc_key_val[k]) {
            return k;
        }
    }
    if (k >= NUM_KEYS) {
        k = KEY_NONE;
    }
    return k;
}

/* ===================================================================
 * Test harness
 * =================================================================== */

/* Run one iteration of the syringe pump main loop */
static void run_one_loop(void) {
    readKey();
    checkTriggers();
    readSerial();
    if (serialStrReady) {
        processSerial();
    }
}

/* Process a single command: feed it, run the loop, print attestation */
static void process_command(const char *cmd) {
    int display_len = (int)strlen(cmd);
    if (display_len > 0 && cmd[display_len - 1] == '\n') display_len--;
    printf("\n--- Command: \"%.*s\" ---\n", display_len, cmd);

    feed_serial(cmd);
    run_one_loop();
    cflat_finalize_and_print();
}

int main(void) {
    printf("=== C-FLAT Syringe Pump Benchmark ===\n");
    printf("ustepsPerML = %ld\n\n", ustepsPerML);

    setup();

    /* Unrolled command sequence (no for-loop) to avoid an instrumented
       main loop that wraps cflat_finalize_and_print() calls. */
    process_command("10\n");   /* Set bolus to 10 uL = 0.010 mL */
    process_command("+\n");    /* Push bolus — expect 68 motor steps */
    process_command("20\n");   /* Set bolus to 20 uL = 0.020 mL */
    process_command("+\n");    /* Push bolus — expect 136 motor steps */

    return 0;
}
