#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "ssd1306_i2c.c"

// I2C defines
// This example will use I2C0 on GPIO8 (SDA) and GPIO9 (SCL) running at 400KHz.
// Pins can be changed, see the GPIO function select table in the datasheet for information on GPIO assignments
#define I2C_PORT i2c0
#define I2C_SDA 8
#define I2C_SCL 9

#define TOTAL_RECORDS 6
uint8_t buf[SSD1306_BUF_LEN];
char *records[TOTAL_RECORDS] = {
    "SIN 1000Hz",
    "SIN 2000Hz",
    "SIN 5000Hz",
    "SIN 10000Hz",
    "",
    ""};

int selectedRecord = 0; // Indeks aktualnie wybranego nagrania

struct render_area frame_area = {
    start_col : 0,
    end_col : SSD1306_WIDTH - 1,
    start_page : 0,
    end_page : SSD1306_NUM_PAGES - 1
};

void DisplayRecords(void)
{
    SSD1306_send_cmd(SSD1306_SET_ALL_ON);
    sleep_ms(500);

    SSD1306_send_cmd(SSD1306_SET_ENTIRE_ON); // Set all pixels on
    sleep_ms(500);

    for (int i = 0; i < TOTAL_RECORDS; i++)
    {
        // Dodaj strzałkę przed wybranym nagraniem
        if (i == selectedRecord)
        {
            // ssd1306_SetCursor(0, i * 10); // Pozycja w pionie zależy od wiersza
            WriteChar(buf, 5, i * 8, '1');
        }

        // Wyświetl nazwę nagrania
        // ssd1306_SetCursor(10, i * 10);
        WriteString(buf, 10, i * 8, records[i]);
    }

    render(buf, &frame_area);
    sleep_ms(2000);
}

int main()
{
    stdio_init_all();

    // I2C Initialisation. Using it at 400Khz.
    const uint LED_PIN = 19; // Wbudowana dioda LED na Raspberry Pi Pico

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    i2c_init(I2C_PORT, 400 * 1000);
    SSD1306_init();
    calc_render_area_buflen(&frame_area);
    // maid();
    //  For more examples of I2C use see https://github.com/raspberrypi/pico-examples/tree/master/i2c

    while (true)
    {
        printf("Hello, world!\n");
        sleep_ms(1000);
        DisplayRecords();
        gpio_put(LED_PIN, 1);
        sleep_ms(1000);
        gpio_put(LED_PIN, 0);
    }
}
