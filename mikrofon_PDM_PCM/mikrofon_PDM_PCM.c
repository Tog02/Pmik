
/******************************************************************************
 * Projekt:        [Dźwiękopułapka]
 * Plik:           main.c
 * Opis:           [Dźwiękopułapka wykrywa dziwne dźwięki, a następnie nagrywa
 *                  je by nie umknęły uwadze użytkownika. Wysyła też powiadomienie
 *                  po zakończeniu nagrywania
 *                  Korzysta z mikrofonu PDM, karty sd, oraz ekranu ssd1306]
 *
 * Autorzy:        Tomasz Głowacki
 *                 Bartłomiej Korzeniewski
 *
 * Data utworzenia:      [2025-02-15]
 * Ostatnia modyfikacja: [2025-05-20]
 *
 * Kompilator:      [np. arm-none-eabi-gcc]
 * Środowisko:      [np. VS Code + PlatformIO]
 * Platforma:       [np. Raspberry Pi Pico 2]
 ******************************************************************************/

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/timer.h"
#include "pico/multicore.h"
#include "btstack.h"
#include "pico/cyw43_arch.h"
#include "pico/btstack_cyw43.h"
#include "hardware/i2c.h"
#include "ssd1306_i2c.c"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pdm.pio.h" // Plik PIO do odbioru PDM
#include "ff.h"      // Biblioteka FatFS do obsługi karty SD
#include "hw_config.h"
#include "f_util.h"
#include "OpenPDMFilter.h"
#include "OpenPDMFilter.c"
#include "server_common.h"
#include "STATES.h"

#define SAMPLE_RATE 16000                 // Częstotliwość próbkowania (Hz)
#define BUFFER_SIZE 512                   // Rozmiar bufora wejściowego
#define PCM_BUFFER_SIZE (BUFFER_SIZE / 4) // Rozmniar bufora wyjściowego
#define I2C_PORT i2c0
#define I2C_SDA 8
#define I2C_SCL 9
#define BUTTON_PIN 19
#define BUTTON_UP_PIN 20
#define BUTTON_PLAY_PIN 21

#define TOTAL_RECORDS 64
#define VISIBLE_LINES 6
#define DEBOUNCE_TIME_MS 200
#define CHANNELS 1
#define MICFREQ 2048000
#define DECIMATION 128
#define FREQ_PCM (MICFREQ / (1000 * 128))
#define IN_DATA_LEN (DECIMATION * FREQ_PCM / 8)

#define PDM_CLOCK_DIV 37 // Podział zegara PDM, 2MHz
#define YOUR_PDM_PIN 16
#define YOUR_CLK_PIN 17

#define MAX_FILES 50
#define MAX_FILENAME_LEN 32 // 8.3 format + null

int val;            // Wartość wyświetlana przez bluetooth
bool was_recording; // Przy zakończeniu nagrywania

bool want_start = false; // flaga rozpocznij nagrywanie
bool blue_start = false;

bool buffer_full = false;
bool recording = false;

uint16_t pcm_buffer[PCM_BUFFER_SIZE]; // Bufor na dane PCM
uint32_t pdm_buffer[BUFFER_SIZE];     // Bufor przechowujący surowe dane PDM
uint32_t pdm_buffer2[BUFFER_SIZE];    // Drugi bufor na dane PDM
uint8_t buf[SSD1306_BUF_LEN];         // Bufor dla ekranu

int scrollOffset = 0;
volatile int selectedRecord = 0;
bool isRecording = false;

volatile uint32_t *active_pdm_buffer; // aktualny bufor używany przez DMA
volatile uint32_t *ready_pdm_buffer;  // gotowy bufor do przetwarzania

volatile bool dma_complete = false; // Flaga sygnalizująca zakończenie transferu DMA
int dma_channel;                    // Kanał DMA używany do transferu danych
dma_channel_config config;          // configuracja DMA

#define HEARTBEAT_PERIOD_MS 1000

static btstack_timer_source_t heartbeat;
static btstack_packet_callback_registration_t hci_event_callback_registration;

char file_names[MAX_FILES][MAX_FILENAME_LEN]; // Tablica nazw plików
int file_count = 0;

static void heartbeat_handler(struct btstack_timer_source *ts)
{
    static uint32_t counter = 0;
    counter++;

    // Invert the led
    static int led_on = true;
    led_on = !led_on;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);

    // Restart timer
    btstack_run_loop_set_timer(ts, HEARTBEAT_PERIOD_MS);
    btstack_run_loop_add_timer(ts);
}
struct render_area frame_area = {
    .start_col = 0,
    .end_col = SSD1306_WIDTH - 1,
    .start_page = 0,
    .end_page = SSD1306_NUM_PAGES - 1};

void DisplayRecords(void)
{
    memset(buf, 0, sizeof(buf));

    if (selectedRecord < scrollOffset)
    {
        scrollOffset = selectedRecord;
    }
    else if (selectedRecord >= scrollOffset + VISIBLE_LINES)
    {
        scrollOffset = selectedRecord - VISIBLE_LINES + 1;
    }

    for (int i = 0; i < VISIBLE_LINES; i++)
    {
        int recordIndex = scrollOffset + i;
        if (recordIndex >= TOTAL_RECORDS)
            break;

        if (recordIndex == selectedRecord)
        {
            WriteChar(buf, 5, i * 8, '1');
        }
        WriteString(buf, 10, i * 8, file_names[recordIndex]);
    }

    render(buf, &frame_area);
}

void startRecording(int recordIndex)
{
    printf(" ROZPOCZYNANIE NAGRANIA DO SLOTU %d (%s)\n", recordIndex, file_names[recordIndex]);
    want_start = true;
    // Wyświetl info na OLED
    memset(buf, 0, sizeof(buf));
    WriteString(buf, 0, 0, "Nagrywanie...");
    WriteString(buf, 0, 16, file_names[recordIndex]);

    render(buf, &frame_area);
}

void stopRecording()
{
    DisplayRecords();
    recording = 0;
    // Wyczyść "REC" napis (nadpisz spacjami)
    // WriteString(buf, SSD1306_WIDTH - 28, 0, "    ");
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

    if (isRecording && gpio != BUTTON_PLAY_PIN)
    {
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

// Obsługa przerwania DMA
void dma_handler()
{
    uint32_t ints = dma_hw->ints0; // odczytujemy maskę aktywnych przerwań
    if (ints & (1u << dma_channel))
    {
        dma_hw->ints0 = 1u << dma_channel; // czyścimy flagę
        if (recording)
        {
            was_recording = true;
            multicore_fifo_push_blocking(BUFFER_READY); // Wysłanie sygnału do rdzenia 1 //
        }
        else
        {
            multicore_fifo_push_blocking(BUFFER_READY_NOT_RECORDING); // Wysłanie sygnału do
        }

        // dma_complete = true;

        // Bufor właśnie został wypełniony przez DMA, sygnalizuj do rdzenia 1
        ready_pdm_buffer = active_pdm_buffer;

        // Przełącz na drugi bufor
        if (active_pdm_buffer == pdm_buffer)
            active_pdm_buffer = pdm_buffer2;
        else
            active_pdm_buffer = pdm_buffer;

        dma_channel_configure(
            dma_channel,
            &config,                   // NULL -> używa poprzedniej konfiguracji
            (void *)active_pdm_buffer, // Nowy bufor docelowy (lub ten sam, jeśli nadpisujesz)
            &pio0->rxf[0],             // Źródło (FIFO z PIO)
            BUFFER_SIZE,               // Ilość próbek
            true                       // Start natychmiastowy
        );

        if ((!recording) && was_recording)
        {
            was_recording = false;
            multicore_fifo_push_blocking(3);
            stopRecording();
        }

        //**************************** */ to throw away

        //**************************** */
    }
}

// Inicjalizacja DMA do pobierania danych z PIO
void init_dma(PIO pio, uint sm)
{
    dma_channel = 10;                                     // dma_claim_unused_channel(true);                            // Przydzielenie dostępnego kanału DMA
    config = dma_channel_get_default_config(dma_channel); // Pobranie domyślnej konfiguracji kanału DMA

    // channel_config_set_transfer_data_size(&config, DMA_SIZE_32);    // Konfiguracja rozmiaru przesyłanych danych (32 bity)
    channel_config_set_read_increment(&config, false);              // Brak inkrementacji adresu źródłowego (odczyt z jednego rejestru FIFO PIO)
    channel_config_set_write_increment(&config, true);              // Inkrementacja adresu docelowego (zapisywanie kolejnych wartości w buforze)
    channel_config_set_dreq(&config, pio_get_dreq(pio, sm, false)); // Ustawienie żądania transferu (DREQ) od PIO

    dma_channel_configure(
        dma_channel, &config,
        &pdm_buffer,   // Bufor docelowy dla DMA (miejsce, gdzie będą zapisywane dane PDM)
        &pio->rxf[sm], // Adres źródłowy - FIFO odbiorcze PIO
        BUFFER_SIZE,   // Liczba transferów (ilość próbek do pobrania)
        true           // Natychmiastowe uruchomienie transferu DMA
    );

    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler); // Przypisanie funkcji obsługi przerwania DMA
    irq_set_enabled(DMA_IRQ_0, true);                  // Włączenie obsługi przerwań DMA
    dma_channel_set_irq0_enabled(dma_channel, true);   // Aktywacja przerwań dla wybranego kanału DMA
    printf("DMA config ready");
}

// Inicjalizacja interfejsu PDM w PIO
void init_pdm(PIO pio, uint sm)
{
    uint offset = pio_add_program(pio, &pdm_microphone_program);
    pdm_microphone_program_init(pio, sm, offset, YOUR_PDM_PIN, YOUR_CLK_PIN);
    pio_sm_set_clkdiv(pio, sm, PDM_CLOCK_DIV);
}

// Przetwarzanie danych PDM na PCM i zapis do pliku
void init_audio_filter(TPDMFilter_InitStruct filter)
{
    filter.LP_HZ = 8000;
    filter.HP_HZ = 10;
    filter.Fs = FREQ_PCM * 1000;
    filter.In_MicChannels = 1;
    filter.Out_MicChannels = CHANNELS;
    filter.Decimation = DECIMATION;
    filter.MaxVolume = 64;
    Open_PDM_Filter_Init(&filter);
}

// Tworzenie nagłówka pliku WAV
int write_wav_header(FIL *file)
{
    UINT bw;
    uint32_t sample_rate = SAMPLE_RATE;
    uint16_t bits_per_sample = 16;
    uint16_t num_channels = 1;
    uint32_t byte_rate = sample_rate * num_channels * (bits_per_sample / 8);
    uint16_t block_align = num_channels * (bits_per_sample / 8);
    uint32_t data_chunk_size = 0;              // Tymczasowo 0, zostanie uzupełniony później
    uint32_t file_size = 36 + data_chunk_size; // Również tymczasowy

    // Bufor nagłówka WAV
    BYTE header[44];

    // RIFF chunk descriptor
    memcpy(&header[0], "RIFF", 4);
    memcpy(&header[4], &file_size, 4);
    memcpy(&header[8], "WAVE", 4);

    // fmt subchunk
    memcpy(&header[12], "fmt ", 4);
    uint32_t subchunk1_size = 16; // PCM
    uint16_t audio_format = 1;    // PCM = 1
    memcpy(&header[16], &subchunk1_size, 4);
    memcpy(&header[20], &audio_format, 2);
    memcpy(&header[22], &num_channels, 2);
    memcpy(&header[24], &sample_rate, 4);
    memcpy(&header[28], &byte_rate, 4);
    memcpy(&header[32], &block_align, 2);
    memcpy(&header[34], &bits_per_sample, 2);

    // data subchunk
    memcpy(&header[36], "data", 4);
    memcpy(&header[40], &data_chunk_size, 4);

    FRESULT result = f_write(file, header, 44, &bw);
    if (result == FR_OK)
    {
        return 1;
    }
    else
        return 0;
}

void finalize_wav_file(FIL *file)
{
    // Oblicz aktualny rozmiar pliku
    DWORD file_size = f_size(file);
    DWORD data_chunk_size = file_size - 44; // Rozmiar danych = cały plik - nagłówek WAV

    UINT bw;

    // Przesuń wskaźnik pliku i zapisz rozmiar całego pliku - 8 bajtów (standard WAV)
    f_lseek(file, 4);
    DWORD riff_chunk_size = file_size - 8;
    f_write(file, &riff_chunk_size, 4, &bw);

    // Przesuń wskaźnik do pola "data chunk size" i zapisz rzeczywisty rozmiar danych
    f_lseek(file, 40);
    f_write(file, &data_chunk_size, 4, &bw);
}

void do_filter(TPDMFilter_InitStruct *filter)
{
    uint8_t *pdm8 = (uint8_t *)ready_pdm_buffer;
    for (uint8_t i = 0; i < 8; i++)
    {
        uint8_t *chunk = &pdm8[i * IN_DATA_LEN];
        Open_PDM_Filter_128(chunk, (uint16_t *)&pcm_buffer[i * FREQ_PCM], 64, filter);
    }
}

void write_buffer(FIL *file)
{
    UINT bw;
    FRESULT result = f_write(file, pcm_buffer, sizeof(pcm_buffer), &bw); //
    if (result != FR_OK)
    {
        printf("sd write problem\n");
    }
}

int list_files(const char *path)
{
    // FATFS fs;
    // FRESULT res = f_mount(&fs, "", 1); // "" = domyślny napęd, 1 = mount now
    DIR dir;
    FILINFO fno;
    int file_count = 0;

    FRESULT res = f_opendir(&dir, path);
    if (res != FR_OK)
    {
        printf("Failed to open directory: %d\n", res);
        return -1;
    }

    while (1)
    {
        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == 0)
            break; // Błąd lub koniec
        if (fno.fattrib & AM_DIR)
            continue; // Pomijaj katalogi

        if (file_count < MAX_FILES)
        {
            strncpy(file_names[file_count], fno.fname, MAX_FILENAME_LEN - 1);
            file_names[file_count][MAX_FILENAME_LEN - 1] = '\0'; // Na wszelki wypadek
            file_count++;
        }
        else
        {
            break; // Osiągnięto limit
        }
    }

    f_closedir(&dir);
    val = file_count + 1;
    // f_unmount("");
    return file_count;
}

bool is_filename_used(int number, char file_names[][MAX_FILENAME_LEN], int file_count)
{
    char expected_name[MAX_FILENAME_LEN];
    snprintf(expected_name, MAX_FILENAME_LEN, "nagranie%d.wav", number);

    for (int i = 0; i < file_count; i++)
    {
        if (strcmp(file_names[i], expected_name) == 0)
        {
            return true;
        }
    }
    return false;
}

void generate_unique_filename_from_list(char *out_name, size_t max_len,
                                        char file_names[][MAX_FILENAME_LEN], int file_count)
{
    int index = 1;
    while (index < 10000)
    { // zabezpieczenie
        if (!is_filename_used(index, file_names, file_count))
        {
            snprintf(out_name, max_len, "nagranie%d.wav", index);
            return;
        }
        index++;
    }

    // Jeśli wszystkie zajęte (bardzo mało prawdopodobne)
    snprintf(out_name, max_len, "nagranie9999.wav");
}

void core1_entry()
{
    char new_name[MAX_FILENAME_LEN];
    FATFS fs;
    FIL file;
    UINT bw;
    FRESULT result;
    f_mount(&fs, "", 1);
    list_files("/");

    TPDMFilter_InitStruct filter;
    filter.LP_HZ = 8000;
    filter.HP_HZ = 10;
    filter.Fs = FREQ_PCM * 1000;
    filter.In_MicChannels = 1;
    filter.Out_MicChannels = CHANNELS;
    filter.Decimation = DECIMATION;
    filter.MaxVolume = 64;
    Open_PDM_Filter_Init(&filter);
    // init_audio_filter(filter);

    while (1)
    {

        uint32_t msg = multicore_fifo_pop_blocking(); // Blokujące oczekiwanie na sygnał z rdzenia 0

        switch (msg)
        {
        case NEW_RECORD:
        {
            printf("\nstarted recording\n");
            file_count = list_files("/");

            generate_unique_filename_from_list(new_name, sizeof(new_name), file_names, file_count);
            result = f_open(&file, new_name, FA_WRITE | FA_CREATE_ALWAYS);
            if (result != FR_OK)

            {
                printf("sd problem");
            }
            write_wav_header(&file);
        }
        break;

        case BUFFER_READY:
        {
            gpio_put(15, 1);
            do_filter(&filter);

            write_buffer(&file);
            gpio_put(15, 0);
        }
        break;
        case END_RECORD:
        {
            finalize_wav_file(&file);
            f_close(&file); // Zamknięcie pliku po zakończeniu nagrywania
            // f_unmount("");
            printf("\nnagranie.wav ready\n");
        }
        break;

        case BUFFER_READY_NOT_RECORDING:
        {
            do_filter(&filter);
        }
        break;

        default:
            break;
        }
    }
}

void start_listenig(PIO pio, uint sm)
{

    //
    init_dma(pio, sm); // Inicjalizacja DMA do przesyłania danych
    pio_sm_set_enabled(pio, sm, true);
}
void start_recording()
{
    multicore_fifo_push_blocking(1);
    recording = true;
}
// Główna funkcja programu
int main()
{

    stdio_init_all();

    cyw43_arch_init();

    l2cap_init();
    sm_init();

    stdio_init_all();

    gpio_init(15);
    gpio_set_dir(15, GPIO_OUT);

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

    att_server_init(profile_data, att_read_callback, att_write_callback);

    // inform about BTstack state
    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    // register for ATT event
    att_server_register_packet_handler(packet_handler);

    // set one-shot btstack timer
    heartbeat.process = &heartbeat_handler;
    btstack_run_loop_set_timer(&heartbeat, HEARTBEAT_PERIOD_MS);
    btstack_run_loop_add_timer(&heartbeat);

    // turn on bluetooth!
    hci_power_control(HCI_POWER_ON);

    printf("Hello world");
    sleep_ms(1000);
    printf("Hello again");

    PIO pio = pio0;
    uint sm = 0;

    multicore_launch_core1(core1_entry);
    was_recording = false;
    init_pdm(pio, sm); // Inicjalizacja mikrofonu PDM w PIO
    pio_sm_set_enabled(pio, sm, false);
    active_pdm_buffer = pdm_buffer; // Start z bufora 0
    // start_recording(pio, sm);
    sleep_ms(1000);
    DisplayRecords();
    start_listenig(pio, sm);
    while (1)
    {

        // printf("temp: %d\n", current_temp);
        if (blue_start)
        {
            startRecording(selectedRecord);
            blue_start = 0;
        }

        if (want_start)
        {
            start_recording();
            want_start = false;
            if (le_notification_enabled)
            {

                att_server_request_can_send_now_event(con_handle);
                // current_temp = current_temp + 200;
            }
        }
        sleep_ms(1000); // Sprawdzić wfi();
        // tight_loop_contents(); // Zapewnienie ciągłej pracy
    }

    return 0;
}
