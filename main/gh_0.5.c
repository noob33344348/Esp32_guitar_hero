#include <stdint.h>
#include <stdio.h>
#include "esp_timer.h"
#include "led_stripe.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "ssd1306.h"
#include "font8x8_basic.h"

// LED
#define N_STRIPE 2
static uint8_t led_type[] = {0, 0, 1, 2, 1}; // 0 = Red, 1 = Yellow, 2 = Green

// Stripe 0
#define LED00_GPIO 2
#define LED01_GPIO 4
#define LED02_GPIO 5
#define LED03_GPIO 18
#define LED04_GPIO 19
#define BUTTON0_GPIO 32

// Stripe 1
#define LED10_GPIO 27
#define LED11_GPIO 26
#define LED12_GPIO 25
#define LED13_GPIO 16
#define LED14_GPIO 17
#define BUTTON1_GPIO 13

// Struct of led stripes
struct LedStripe stripe[N_STRIPE] = {
    [0] = {
        .led_gpios = {LED00_GPIO, LED01_GPIO, LED02_GPIO, LED03_GPIO, LED04_GPIO},
        .button = BUTTON0_GPIO,
        .last_clicked = 0,
        .led_status = {false, false, false, false, false}
    },
    [1] = {
        .led_gpios = {LED10_GPIO, LED11_GPIO, LED12_GPIO, LED13_GPIO, LED14_GPIO},
        .button = BUTTON1_GPIO,
        .last_clicked = 0,
        .led_status = {false, false, false, false, false}
    }
};

// Score
#define CORRECT_SCORE 10
#define ALMOST_SCORE 4
#define WRONG_SCORE 12
SSD1306_t dev;
int16_t score = -1;
int16_t new_score = 0;
char score_buffer[7];

// BUTTON
#define MICROS_BOUNCE 200000
void button_isr(void* args);

// SONG
#define BUZZER_GPIO 15
#define N_NOTE 24
#define SPEED 1000000
uint16_t counter=0;
static uint32_t song_high[N_NOTE] = {0, 0, 0, 1, 0, 1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 1, 1, 0, 0};
static bool new_nota[N_STRIPE][N_NOTE] = { // 1 = New note, 0 = No note
    {1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    {1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0}
};
void timer_callback(void *param);

void app_main(void)
{
    // Initialize global ISR handler
    gpio_install_isr_service(0);

    // Initialize output GPIOs
    gpio_reset_pin(BUZZER_GPIO);
    gpio_set_direction(BUZZER_GPIO, GPIO_MODE_OUTPUT);
    for(uint8_t j=0; j<N_STRIPE; j++)
    {
        // Inizialize LEDs
        for(uint8_t i=0; i<N_LED; i++)
        {
            gpio_reset_pin(stripe[j].led_gpios[i]);
            gpio_set_direction(stripe[j].led_gpios[i], GPIO_MODE_OUTPUT);
            gpio_set_level(stripe[j].led_gpios[i], stripe[j].led_status[i]);
        }

        // Inizialize button
        gpio_reset_pin(stripe[j].button);
        gpio_pullup_en(stripe[j].button);
        gpio_set_intr_type(stripe[j].button, GPIO_INTR_NEGEDGE);
        gpio_set_direction(stripe[j].button, GPIO_MODE_INPUT);
        gpio_isr_handler_add(stripe[j].button, button_isr, (void*)(uintptr_t)j);
        gpio_intr_enable(stripe[j].button);
    }

    // Initialize i2c interface

    i2c_master_init(&dev, CONFIG_SDA_GPIO, CONFIG_SCL_GPIO, -1);
    ssd1306_init(&dev, 128, 64);
    ssd1306_clear_screen(&dev, false);
    ssd1306_contrast(&dev, 0xff);
    ssd1306_display_text(&dev, 0, "Score:", 5, false);

    //Create timer
    const esp_timer_create_args_t my_timer_args = {
        .callback = &timer_callback,
        .name = "ShiftTimer"};
    esp_timer_handle_t timer_handler;
    ESP_ERROR_CHECK(esp_timer_create(&my_timer_args, &timer_handler));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer_handler, SPEED));
}

int digits(int16_t num)
{
    // Num is null
    if(num==0)
        return 1;

    // Num is negative
    bool neg = num<0;
    if(neg)
        num*=-1;

    // Num is positive
    int8_t i = 0;
    while(num > 0)
    {
        num /= 10;
        i++;
    }
    return (neg? i+1:i);
}

void timer_callback(void *param)
{
    for(uint8_t j=0; j<N_STRIPE; j++)
    {
        // Missed note
        if(stripe[j].led_status[N_LED-1] == 1)
            new_score = score-WRONG_SCORE;

        // Shift notes
        for(uint8_t i=N_LED-1; i>0; i--)
        {
            stripe[j].led_status[i] = stripe[j].led_status[i-1];
            gpio_set_level(stripe[j].led_gpios[i], stripe[j].led_status[i]);
        }
        // New note
        stripe[j].led_status[0] = new_nota[j][counter];
        gpio_set_level(stripe[j].led_gpios[0], stripe[j].led_status[0]);
    }

    // Score update
    if(new_score != score)
    {
        score = new_score;
        snprintf(score_buffer, sizeof(score_buffer), "%i", score);
        ssd1306_clear_line(&dev, 5, false);
        ssd1306_clear_line(&dev, 6, false);
        ssd1306_clear_line(&dev, 7, false);
        ssd1306_clear_line(&dev, 8, false);
        ssd1306_display_text_x3(&dev, 5, score_buffer, digits(score), false);
    }


    //Play sound
    gpio_set_level(BUZZER_GPIO, song_high[counter]);

    // Restart notes once ended
    if(counter==N_NOTE-1)
        counter=0;

    counter++;
}

void button_isr(void* args)
{
    // Bounds check
    uint8_t stripe_index = (uintptr_t) args;
    if(stripe_index >= N_STRIPE) {
        return;
    }

    // Debouncing
    uint64_t now = esp_timer_get_time();
    if(now - stripe[stripe_index].last_clicked < MICROS_BOUNCE)
    {
        return;
    }
    // Update the time
    stripe[stripe_index].last_clicked = now;

    // Disable interrupt
    gpio_intr_disable(stripe[stripe_index].button);

    // ISR body

    int8_t last_led;
    for(last_led=N_LED-1; last_led >= 0 && stripe[stripe_index].led_status[last_led]==0; last_led--);

    if(last_led>=0) //If at least one led is on
    {
        //Turn off the last led of the stripe
        gpio_set_level(stripe[stripe_index].led_gpios[last_led], 0);
        stripe[stripe_index].led_status[last_led] = 0;

        // Score calculation
        if(led_type[last_led] == 2)
            new_score = score+CORRECT_SCORE;
        else if(led_type[last_led] == 1)
            new_score = score+ALMOST_SCORE;
        else
            new_score = score-WRONG_SCORE;
    }

    // Re-enable interrupt
    gpio_intr_enable(stripe[stripe_index].button);
}
