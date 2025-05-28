#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/timer.h"
#include "ssd1306_i2c.c"

#define I2C_PORT i2c0
#define I2C_SDA 8
#define I2C_SCL 9
#define BUTTON_PIN 16
#define BUTTON_UP_PIN 17
#define BUTTON_PLAY_PIN 18

#define TOTAL_RECORDS 8
#define VISIBLE_LINES 6
#define DEBOUNCE_TIME_MS 200

/*#define MAX_RECORDS 64
#define MAX_FILENAME_LEN 32
char records[MAX_RECORDS][MAX_FILENAME_LEN];
int totalRecords = 0;
*/
uint8_t buf[SSD1306_BUF_LEN];
int scrollOffset = 0;
volatile int selectedRecord = 0;
bool isRecording = false;

char *records[TOTAL_RECORDS] = {
    "Nagranie 1",
    "Nagranie 2",
    "Nagranie 3",
    "Nagranie 4",
    "Nagranie 5",
    "Nagranie 6",
    "Nagranie 7",
    "Nagranie 8"
};



struct render_area frame_area = {
    .start_col = 0,
    .end_col = SSD1306_WIDTH - 1,
    .start_page = 0,
    .end_page = SSD1306_NUM_PAGES - 1
};

void DisplayRecords(void)
{
    memset(buf, 0, sizeof(buf));

    if (selectedRecord < scrollOffset) {
        scrollOffset = selectedRecord;
    } else if (selectedRecord >= scrollOffset + VISIBLE_LINES) {
        scrollOffset = selectedRecord - VISIBLE_LINES + 1;
    }

    for (int i = 0; i < VISIBLE_LINES; i++) {
        int recordIndex = scrollOffset + i;
        if (recordIndex >= TOTAL_RECORDS) break;

        if (recordIndex == selectedRecord) {
            WriteChar(buf, 5, i * 8, '1');
        }
        WriteString(buf, 10, i * 8, records[recordIndex]);
    }

    render(buf, &frame_area);
}

void startRecording(int recordIndex)
{
    printf(" ROZPOCZYNANIE NAGRANIA DO SLOTU %d (%s)\n", recordIndex, records[recordIndex]);

    // Wyświetl info na OLED
    memset(buf, 0, sizeof(buf));
    WriteString(buf, 0, 0, "Nagrywanie...");
    WriteString(buf, 0, 16, records[recordIndex]);
    render(buf, &frame_area);
}

void stopRecording()
{
    printf("ZATRZYMANO NAGRYWANIE\n");
    DisplayRecords();

    // Wyczyść "REC" napis (nadpisz spacjami)
    WriteString(buf, SSD1306_WIDTH - 28, 0, "    ");
    render(buf, &frame_area);
  /* stop_adc_recording(); // lub inna Twoja funkcja
    LoadRecordingsFromSD();
    DisplayRecords();
    void LoadRecordingsFromSD()
{
    DIR dir;
    FILINFO fno;

    totalRecords = 0;

    if (f_opendir(&dir, "/recordings") == FR_OK)
    {
        while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] && totalRecords < MAX_RECORDS)
        {
            if (!(fno.fattrib & AM_DIR))  // pomiń katalogi
            {
                strncpy(records[totalRecords], fno.fname, MAX_FILENAME_LEN - 1);
                records[totalRecords][MAX_FILENAME_LEN - 1] = '\0';
                totalRecords++;
            }
        }
        f_closedir(&dir);
        
    }
}*/
}

// Debounce timeout callback
int64_t debounce_timeout_callback(alarm_id_t id, void *user_data)
{
    uint gpio = (uint)(uintptr_t)user_data;

    gpio_set_irq_enabled(gpio, GPIO_IRQ_EDGE_FALL, true); // Włącz przerwanie ponownie

    if (isRecording && gpio != BUTTON_PLAY_PIN) {
        // Podczas nagrywania ignoruj inne przyciski
        return 0;
    }

    if (gpio == BUTTON_PIN)
    {
        selectedRecord = (selectedRecord + 1) % TOTAL_RECORDS;
        DisplayRecords();
    }
    else if (gpio == BUTTON_UP_PIN)
    {
        selectedRecord = (selectedRecord == 0) ? (TOTAL_RECORDS - 1) : (selectedRecord - 1);
        DisplayRecords();
    }
    else if (gpio == BUTTON_PLAY_PIN)
    {
        if (isRecording)
        {
            stopRecording();
            isRecording = false;
        }
        else
        {
            startRecording(selectedRecord);
            isRecording = true;
        }
    }

    return 0;
}

// ISR: wyłącza przerwanie i uruchamia debounce
void button_isr(uint gpio, uint32_t events)
{
    gpio_set_irq_enabled(gpio, GPIO_IRQ_EDGE_FALL, false);
    add_alarm_in_ms(DEBOUNCE_TIME_MS, debounce_timeout_callback, (void *)(uintptr_t)gpio, false);
}

int main()
{
    stdio_init_all();

    // OLED i I2C init
    gpio_init(I2C_SDA);
    gpio_init(I2C_SCL);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    i2c_init(I2C_PORT, 400 * 1000);
    SSD1306_init();
    calc_render_area_buflen(&frame_area);

    // GPIO init
    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);

    gpio_init(BUTTON_UP_PIN);
    gpio_set_dir(BUTTON_UP_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_UP_PIN);

    gpio_init(BUTTON_PLAY_PIN);
    gpio_set_dir(BUTTON_PLAY_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PLAY_PIN);

    // ISR z debounce
    gpio_set_irq_enabled_with_callback(BUTTON_PIN, GPIO_IRQ_EDGE_FALL, true, &button_isr);
    gpio_set_irq_enabled(BUTTON_UP_PIN, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(BUTTON_PLAY_PIN, GPIO_IRQ_EDGE_FALL, true);

    DisplayRecords();

    while (true) {
        tight_loop_contents(); // logika działa w ISR
    }
}