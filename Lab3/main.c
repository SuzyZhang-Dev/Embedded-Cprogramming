#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#define SENSOR_PIN 28
#define MOTOR_PINS {2,3,6,13}
#define STEP_DELAY_MS 5

const bool half_step_sequence[8][4]={
    {1,0,0,0},
    {1,1,0,0},
    {0,1,0,0},
    {0,1,1,0},
    {0,0,1,0},
    {0,0,1,1},
    {0,0,0,1},
    {1,0,0,1}
};

bool is_calibrated=false;
float step_per_revolution=4096;

void set_step_pins(const bool seq[4],const uint pins[4]) {
    for(int i=0;i<4;i++) {
        gpio_put(pins[i],seq[i]);
    }
}

void run_one_step(const uint pins[4],int direction) {
    static int step_index = 0;
    step_index=(step_index+direction+8)%8;
    set_step_pins(half_step_sequence[step_index],pins);
    sleep_ms(STEP_DELAY_MS);
}

// to find the starting point when the signal shows 1->0
uint run_until_falling_edge(int pin,const uint pins[4],int direction) {
    int steps_taken=0;
    int previous_state;
    int current_state=gpio_get(pin);
    do {
        previous_state=current_state;
        run_one_step(pins,direction);
        steps_taken++;
        current_state=gpio_get(pin);
    } while(!(previous_state == 1 && current_state == 0));

    return steps_taken;
}

void do_calibration(const uint pins[4]) {
    printf("Starting calibration\n");
    uint step_count_per_revolution[3]; //count 3 laps and do average
    float sum_steps=0;
    int direction=1; //clock-wise

    run_until_falling_edge(SENSOR_PIN,pins,direction);
    printf("Start point found. Continuing calibration...\n");

    for (int i=0;i<3;i++) {
        printf("Starting round %d\n",i+1);
        uint steps_this_rev=run_until_falling_edge(SENSOR_PIN,pins,direction);
        step_count_per_revolution[i]=steps_this_rev;
        sum_steps+=steps_this_rev;
        printf("Round %d completed, steps: %d.\n",i+1,steps_this_rev);
    }

    step_per_revolution=sum_steps/3.0f;
    is_calibrated = true;
    printf("Calibrated\n");
    printf("Steps of each round: %d,%d,%d.\n",step_count_per_revolution[0],
        step_count_per_revolution[1],step_count_per_revolution[2]);
    printf("Steps per round is %.2f.\n",step_per_revolution);
}

void show_status() {
    if (is_calibrated) {
        printf("Calibrated\n");
        printf("Steps per round is %.2f.\n",step_per_revolution);
    }else {
        printf("Uncalibrated\n");
        printf("Steps per round is not available.\n");
    }
}

void do_run(const uint pins[4],int scanned_input,int n) {
    if (!is_calibrated) {
        printf("No calibration found\n");
        return;
    }
    if (scanned_input == 1) n=8;

    float total_steps=n/8.0f * step_per_revolution;

    for (long i=0;i<total_steps;i++) {
        run_one_step(pins,1);
    }

    set_step_pins((bool[4]){0,0,0,0},pins);
    printf("Run %d Completed.\n",n);
}

void init_gpio(uint pins[4]) {
    for (int i=0;i<4;i++) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i],GPIO_OUT);
    }
    gpio_init(SENSOR_PIN);
    gpio_set_dir(SENSOR_PIN,GPIO_IN);
    gpio_pull_up(SENSOR_PIN);
}

int main() {
    stdio_init_all();
    const uint motor_pins[4]=MOTOR_PINS;
    init_gpio(motor_pins);
    sleep_ms(STEP_DELAY_MS);

    char command_buffer[100];
    char command[10];
    int n_value; //read for do_run()

    while (true) {
        printf("> ");
        if (fgets(command_buffer, sizeof(command_buffer), stdin)==NULL) {
            continue;
        }
        int command_parsed_count=sscanf(command_buffer,"%s %d",command,&n_value);
        if (command_parsed_count<=0) {
            continue;
        }

        if (strcmp(command,"status")==0) {
            show_status();
        }
        else if (strcmp(command,"calib")==0) {
            do_calibration(motor_pins);
        }
        else if (strcmp(command,"run")==0) {
            do_run(motor_pins,command_parsed_count,n_value);
        }else {
            printf("Invalid command!\n");
        }
    }
}