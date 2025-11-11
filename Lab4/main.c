#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "iuart.h"
#include "ctype.h"

#define STRLEN 80
// We are using pins 0 and 1, but see the GPIO function select table in the
// datasheet for information on which other pins can be used.
#if 0
#define UART_NR 0
#define UART_TX_PIN 0
#define UART_RX_PIN 1
#else
#define UART_NR 1
#define UART_TX_PIN 4
#define UART_RX_PIN 5
#endif

#define BAUD_RATE 9600

#define RX_BUFFER_SIZE 128
#define RESPONSE_TIMEOUT_MS 500 //waits for response for 500 ms
#define MAX_AT_RETRIES 5 //try 5 times and if not, back to state 1
#define DEVEUI_LENGTH 17 //16 characters + '\0'

int read_line_with_timeout(char *buffer,int max_len,uint32_t timeout_ms);
void process_deveui(const char *s);

typedef enum {
    STATE_WAIT_FOR_BUTTON,
    STATE_SEND_AT,
    STATE_WAIT_AT_RESPONSE,
    STATE_SEND_VERSION,
    STATE_WAIT_VERSION_RESPONSE,
    STATE_SEND_DEVEUI,
    STATE_WAIT_DEVEUI_RESPONSE,
    STATE_PROCESS_DEVEUI
}app_state_t; //use app_state_t indicate this is a "type" instead of a variable.

int main() {
    const uint led_pin = 22;
    const uint button = 9;

    // Initialize LED pin
    gpio_init(led_pin);
    gpio_set_dir(led_pin, GPIO_OUT);

    gpio_init(button);
    gpio_set_dir(button, GPIO_IN);
    gpio_pull_up(button);

    // Initialize stdio serial port
    stdio_init_all();

    printf("----Boot----\r\n");

    // set up our own UART
    iuart_setup(UART_NR, UART_TX_PIN, UART_RX_PIN, BAUD_RATE);

    app_state_t state = STATE_WAIT_FOR_BUTTON; //initialize state to wait for press the button
    char rx_line_buffer[RX_BUFFER_SIZE];
    int at_retries = 0;
    int read_len = 0;  // return value from read_line_with_timeout

    while (true) {
        switch (state) {
            case STATE_WAIT_FOR_BUTTON:
                if (!gpio_get(button)) {
                    sleep_ms(50); //debouncing
                    while (!gpio_get(button)) sleep_ms(10);
                    printf("Button pressed\r\n");
                    at_retries = 0; //reset retries counter, keep it as 0 for sending AT commands;
                    state = STATE_SEND_AT;
                }else {
                    sleep_ms(50);
                }
                break;

            case STATE_SEND_AT:
                if (at_retries >= MAX_AT_RETRIES) {
                    printf("module not responding\r\n");
                    state = STATE_WAIT_FOR_BUTTON;
                }else {
                    printf("Sending 'AT',(try %d / %d...)\r\n",at_retries,MAX_AT_RETRIES);
                    iuart_send(UART_NR,"AT\r\n");
                    at_retries++;
                    state = STATE_WAIT_AT_RESPONSE;
                }
                break;

            case STATE_WAIT_AT_RESPONSE:
                read_len = read_line_with_timeout(rx_line_buffer,RX_BUFFER_SIZE,RESPONSE_TIMEOUT_MS);
                    // if function return -1, it means timeout
                if (read_len<0) {
                    printf("Timeout\r\n");
                    state = STATE_SEND_AT; //retry sending AT
                }else {
                    printf("%s\r\n",rx_line_buffer);
                    printf("----------------\r\n");
                    if (strstr(rx_line_buffer,"OK")!=0) {
                        printf("Connected to LoRa module\r\n");
                        printf("----------------\r\n");
                        state = STATE_SEND_VERSION;
                    }
                }
                break;

            case STATE_SEND_VERSION:
                printf("Sending 'AT+VER'.....\r\n");
                printf("----------------\r\n");
                iuart_send(UART_NR,"AT+VER\r\n");
                state = STATE_WAIT_VERSION_RESPONSE;
                break;

            case STATE_WAIT_VERSION_RESPONSE:
                read_len = read_line_with_timeout(rx_line_buffer,RX_BUFFER_SIZE,RESPONSE_TIMEOUT_MS);
                if (read_len<0) {
                    printf("Module stopped responding...\r\n");
                    state = STATE_WAIT_FOR_BUTTON;
                }else {
                    printf("%s\r\n",rx_line_buffer);
                    printf("----------------\r\n");
                    if (strstr(rx_line_buffer,"+VER")!=NULL) {
                        printf("Firmware version %s\r\n",rx_line_buffer);
                        state = STATE_SEND_DEVEUI;
                    }
                }
                break;

            case STATE_SEND_DEVEUI:
                printf("Sending 'AT+ID=DevEui'.....\r\n");
                printf("----------------\r\n");
                iuart_send(UART_NR,"AT+ID=DevEui\r\n");
                state = STATE_WAIT_DEVEUI_RESPONSE;
                break;

            case STATE_WAIT_DEVEUI_RESPONSE:
                read_len = read_line_with_timeout(rx_line_buffer,RX_BUFFER_SIZE,RESPONSE_TIMEOUT_MS);
                if (read_len<0) {
                    printf("Module stopped responding...\r\n");
                    state = STATE_WAIT_FOR_BUTTON;
                }else {
                    //printf("Received response '%s'\r\n",rx_line_buffer);
                    if (strstr(rx_line_buffer,"+ID: DevEui")!=NULL) {
                        printf("%s\r\n",rx_line_buffer);
                        printf("----------------\r\n");
                        state = STATE_PROCESS_DEVEUI;
                    }
                }
                break;

                case STATE_PROCESS_DEVEUI:
                    printf("Processing DevEui....\r\n");
                    process_deveui(rx_line_buffer);
                    state = STATE_WAIT_FOR_BUTTON;
                    break;
        }
    }
}

//uint32_t timeout_ms;  uint64_t timestamp_us;
//in this case timeout_ms is 500
int read_line_with_timeout(char *buffer,int max_len,uint32_t timeout_ms) {
    int pos = 0;
    uint64_t start_time = time_us_64();
    uint64_t timeout_us = timeout_ms * 1000;
    uint8_t c;

    while (time_us_64() - start_time < timeout_us) {
        // check if iuart is readable
        if (iuart_read(UART_NR,&c,1)>0) {
            if (c=='\n') {
                buffer[pos]='\0';

                if (pos>0 && buffer[pos-1]=='\r') {
                    buffer[pos-1]='\0';
                    return pos-1; //return length after removing /r
                }
                return pos;
            }
            if (pos<max_len-1) {
                buffer[pos++]= (char)c;
            }
        }
    }
    buffer[pos]='\0';
    return -1;
}

void process_deveui(const char *s) {
// origin response +ID: DevEui, 2C:F7:F1:20:42:00:7B:92 , 8*2 + '\0' = 17
    const char *start_deveui = strstr(s,",");
    if (start_deveui==NULL) {
        printf("Wrong DevEui format.\n");
        return;
    }

    start_deveui += 2;
    char processed_deveui[DEVEUI_LENGTH];
    int pos = 0;

    for (int i = 0; start_deveui[i]!='\0'&&pos<DEVEUI_LENGTH-1; i++) {
        char c=start_deveui[i];
        if (c!=':') {
            processed_deveui[pos++]=tolower(c);
        }
    }
    processed_deveui[pos]='\0';

    printf("%s\r\n",processed_deveui);
}



