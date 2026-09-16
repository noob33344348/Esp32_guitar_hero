#define N_LED 5
struct LedStripe
{
    uint8_t led_gpios[N_LED];
    uint8_t button;
    uint64_t last_clicked;
    bool led_status[N_LED];
};
