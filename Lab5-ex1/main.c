#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "stdio.h"

#define I2C_PORT i2c0
#define SDA_GPIO 16
#define SCL_GPIO 17
#define EEPROM_ADDR 0x50 //because A0,A1 are grounded
#define MAX_EEPROM_ADDR 32*1024

#define SW0_GPIO 9 //control LED0
#define SW1_GPIO 8 //control LED1
#define SW2_GPIO 7 //control LED2

#define LED0 22
#define LED1 21
#define LED2 20

#define LED_BIT_0 (1 << 0)
#define LED_BIT_1 (1 << 1)
#define LED_BIT_2 (1 << 2)
#define DEFAULT_STATE LED_BIT_1

typedef struct ledstate {
    uint8_t state;
    uint8_t not_state;
} ledstate;
// EEPROM has 32768 bytes to store and ledstate takes 2 bytes.
// the last index is 32767,
// so the highest address we can use to store ledstate is 32766.
#define STORE_ADDR (MAX_EEPROM_ADDR-sizeof(ledstate))

static volatile uint8_t current_leds = 0;
static volatile bool state_changed = false;
static volatile uint64_t last_interrupt_time = 0;

void set_led_state(ledstate *ls, uint8_t value)
{
    ls->state = value;
    ls->not_state = ~value;
}

bool led_state_is_valid(ledstate *ls) {
    return ls->state == (uint8_t) ~ls->not_state;
}

void eeprom_write_ledstate(uint16_t addr, ledstate *ls) {
    uint8_t buf[2+sizeof(ledstate)];
    buf[0] = (uint8_t)(addr>>8);
    buf[1] = (uint8_t)(addr & 0xFF);
    buf[2] = ls->state;
    buf[3] = ls->not_state;
    i2c_write_blocking(I2C_PORT, EEPROM_ADDR, buf, sizeof(buf), false);
    sleep_ms(5); // EEPROM write delay
}

void eeprom_read_ledstate(uint16_t addr, ledstate *ls) {
    uint8_t addr_buf[2];
    addr_buf[0] = (uint8_t)(addr >> 8);
    addr_buf[1] = (uint8_t)(addr & 0xFF);

    i2c_write_blocking(I2C_PORT, EEPROM_ADDR, addr_buf, 2, true);
    i2c_read_blocking(I2C_PORT, EEPROM_ADDR, (uint8_t*)ls, sizeof(ledstate), false);
}

void update_leds(uint8_t state) {
    gpio_put(LED0, (state >> 0) &1);
    gpio_put(LED1, (state >> 1) &1);
    gpio_put(LED2, (state >> 2) &1);
}


void gpio_callback(uint gpio, uint32_t events) {
    //static uint64_t last_interrupt_time = 0;
    uint64_t current_time = time_us_64();
    //static uint8_t current_leds = 0;
    //bool state_changed = false;
    if (current_time - last_interrupt_time < 500000) return;
    last_interrupt_time = current_time;

    uint toggle_bit = 0;
    if (gpio==SW0_GPIO) {
        toggle_bit = LED_BIT_0;
    } else if (gpio==SW1_GPIO) {
        toggle_bit = LED_BIT_1;
    } else if (gpio==SW2_GPIO) {
        toggle_bit = LED_BIT_2;
    }
    if (toggle_bit) {
        current_leds ^= toggle_bit;
        state_changed = true;
    }
}

int main() {
    stdio_init_all();
    sleep_ms(1000);
    printf("Ex1: Led State\n");
    i2c_init(I2C_PORT, 9600);
    gpio_set_function(SDA_GPIO, GPIO_FUNC_I2C);
    gpio_set_function(SCL_GPIO, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_GPIO);
    gpio_pull_up(SCL_GPIO);

    gpio_init(SW0_GPIO);
    gpio_set_dir(SW0_GPIO, GPIO_IN);
    gpio_pull_up(SW0_GPIO);
    gpio_init(SW1_GPIO);
    gpio_set_dir(SW1_GPIO, GPIO_IN);
    gpio_pull_up(SW1_GPIO);
    gpio_init(SW2_GPIO);
    gpio_set_dir(SW2_GPIO, GPIO_IN);
    gpio_pull_up(SW2_GPIO);

    gpio_init(LED0);
    gpio_set_dir(LED0, GPIO_OUT);
    gpio_init(LED1);
    gpio_set_dir(LED1, GPIO_OUT);
    gpio_init(LED2);
    gpio_set_dir(LED2, GPIO_OUT);

    gpio_set_irq_enabled_with_callback(SW0_GPIO, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    gpio_set_irq_enabled_with_callback(SW1_GPIO, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    gpio_set_irq_enabled_with_callback(SW2_GPIO, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);

    ledstate ledstate;
    eeprom_write_ledstate(STORE_ADDR, &ledstate);

    if (led_state_is_valid(&ledstate)) {
        current_leds = ledstate.state;
        printf("Loaded led state from EEPROM: 0x%02X\n", current_leds);
    } else {
        current_leds = DEFAULT_STATE;
        printf("Invalid led state in EEPROM. Using default: 0x%02X\n", current_leds);

        set_led_state(&ledstate, current_leds);
        eeprom_write_ledstate(STORE_ADDR, &ledstate);
    }

    update_leds(current_leds);
    uint64_t start_tims_second = time_us_64()/1000000;
    printf("Start tims: %lu\n", start_tims_second);

    while (1) {
        if (state_changed) {
            state_changed = false;
            update_leds(current_leds);

            uint64_t current_time = time_us_64()/1000000;
            printf("Current time: %lu\n", current_time);
            set_led_state(&ledstate, current_leds);
            eeprom_write_ledstate(STORE_ADDR, &ledstate);
            printf("New state to eeprom.\n");
        }
    }








}


/* learing code before the main application.
 * int main() {
    stdio_init_all();
    sleep_ms(1000);
    printf("---Single Byte test----\n");
    i2c_init(I2C_PORT, 9600);
    gpio_set_function(SDA_GPIO, GPIO_FUNC_I2C);
    gpio_set_function(SCL_GPIO, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_GPIO);
    gpio_pull_up(SCL_GPIO);

    printf("Writing to addr 0...\n");
    eeprom_write_byte(0,0xA5);
    printf("Writing to addr 1...\n");
    eeprom_write_byte(1,0xBC);

    printf("Reading...\n");
    uint8_t data0 = eeprom_read_byte(0);
    uint8_t data1 = eeprom_read_byte(1);

    printf("addr 0:",data0);
    printf("addr 1:",data1);

    if (data0==0xA5 && data1==0xBC) {
        printf("Single byte read/write test passed!\n");
    } else {
        printf("Single byte read/write test failed!\n");
    }

    while (1) {
        sleep_ms(1000);
    }
    return 0;
}
}*/