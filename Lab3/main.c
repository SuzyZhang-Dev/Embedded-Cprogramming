#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "pico/util/queue.h"

#define LED0_PIN 22
#define LED1_PIN 21
#define LED2_PIN 20
#define ENCODER_A 10
#define ENCODER_B 11
#define ENCODER_SW 12
#define BRIGHTNESS_MAX 1000

//3 LEDs on/off/dim together. Need to release. Holding doesn't cause keep lighting.
//Rot_sw is main switch. clockwise increase brightness; anti-clockwise decrease brightness.
    //if leds are off, no effect;
//if leds brightness have been changed, it should remain when next time toggle them.
    //leds-ON, brightness=0, rot_sw==0, brightness to 50%.(instead of turn to off)
//divider 125, PWM-frequency 1kHz, Top=999
//no any application logic in ISR
//rot_sw needs unbound

static queue_t events;

static void encoder_handler(uint gpio, uint32_t events_mask) {
    if (gpio == ENCODER_A) {
        int event_value;
        if (gpio_get(ENCODER_B)) {
            event_value = -1;
        } else {
            event_value = 1;
        }
        queue_try_add(&events, &event_value);
    }
}

int main() {
    stdio_init_all();
    gpio_init(ENCODER_A);
    gpio_set_dir(ENCODER_A, GPIO_IN);
    gpio_disable_pulls(ENCODER_A);

    gpio_init(ENCODER_B);
    gpio_set_dir(ENCODER_B, GPIO_IN);
    gpio_disable_pulls(ENCODER_B);

    gpio_init(ENCODER_SW);
    gpio_set_dir(ENCODER_SW, GPIO_IN);
    gpio_pull_up(ENCODER_SW);

    const uint led_pins[3] = {LED0_PIN, LED1_PIN, LED2_PIN};
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv_int(&config, 125);
    pwm_config_set_wrap(&config, BRIGHTNESS_MAX-1);

    for (int i = 0; i < 3; i++) {
        uint slice = pwm_gpio_to_slice_num(led_pins[i]);
        uint channel = pwm_gpio_to_channel(led_pins[i]);
        gpio_set_function(led_pins[i], GPIO_FUNC_PWM);
        pwm_set_enabled(slice, false);
        pwm_init(slice, &config, false);
        pwm_set_chan_level(slice, channel, 0);
        pwm_set_enabled(slice, true);
    }

    //set default state of leds.
    bool leds_on=false;
    int brightness=500;
    int half_brightness = BRIGHTNESS_MAX/2;
    bool sw_released=true;
    int last_brightness = brightness;

    queue_init(&events, sizeof(int), 16);
    gpio_set_irq_enabled_with_callback(ENCODER_A, GPIO_IRQ_EDGE_RISE, true, &encoder_handler);

    while (1) {
        bool sw_pressed=!gpio_get(ENCODER_SW);

        if (sw_pressed&&sw_released) {
            sw_released=false;
            if (leds_on&&brightness==0) {
                brightness=half_brightness;
            }else {
                leds_on=!leds_on;
                //test whether when sw1_press works.
                printf("LEDs toggled\n");
            }
        }else if (!sw_pressed){
            sw_released=true;
        }

        int value=0;
        int received_value;
        while (queue_try_remove(&events, &received_value)) {
            value += received_value;
        }
        // only leds are on could be dimmed.
        if (leds_on) {
            if (value!=0) {
                brightness+=(value*10);
                if (brightness>BRIGHTNESS_MAX) brightness=BRIGHTNESS_MAX;
                if (brightness<0) brightness=0;
            }
        }

        int current_pwm=0;
        if (leds_on) current_pwm=brightness;

        for (int i = 0; i < 3; i++) {
            pwm_set_gpio_level(led_pins[i], current_pwm);
        }
        //print to serial for debugging
        if (brightness != last_brightness) {
            printf("Brightness: %d\n", brightness);
            last_brightness = brightness;
        }
        sleep_ms(10);
    }
};
