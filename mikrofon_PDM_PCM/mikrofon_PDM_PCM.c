#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/timer.h"
#include "pico/multicore.h"

#include "pdm.pio.h" // Plik PIO do odbioru PDM
#include "ff.h"      // Biblioteka FatFS do obsługi karty SD
#include "hw_config.h"
#include "f_util.h"
#include "OpenPDMFilter.h"
#include "OpenPDMFilter.c"

#define SAMPLE_RATE 16000                 // Częstotliwość próbkowania (Hz)
#define BUFFER_SIZE 512                   // Rozmiar bufora wejściowego
#define PCM_BUFFER_SIZE (BUFFER_SIZE / 4) // Rozmniar bufora wyjściowego

#define CHANNELS 1
#define MICFREQ 2048000
#define DECIMATION 128
#define FREQ_PCM (MICFREQ / (1000 * 128))
#define IN_DATA_LEN (DECIMATION * FREQ_PCM / 8)

#define PDM_CLOCK_DIV 37 // Podział zegara PDM, 2MHz
#define YOUR_PDM_PIN 16
#define YOUR_CLK_PIN 17
#define BUTTON_PIN 15

bool buffer_full = false;
uint16_t pcm_buffer[PCM_BUFFER_SIZE]; // Bufor na dane PCM
uint32_t pdm_buffer[BUFFER_SIZE];     // Bufor przechowujący surowe dane PDM
uint32_t gpio_buffer[BUFFER_SIZE];
uint32_t pdm_buffer2[BUFFER_SIZE]; // Drugi bufor na dane PDM

volatile uint32_t *active_pdm_buffer; // aktualny bufor używany przez DMA
volatile uint32_t *ready_pdm_buffer;  // gotowy bufor do przetwarzania

volatile bool dma_complete = false; // Flaga sygnalizująca zakończenie transferu DMA
int dma_channel;                    // Kanał DMA używany do transferu danych
dma_channel_config config;          // configuracja DMA

// Obsługa przerwania DMA
void dma_handler()
{
    uint32_t ints = dma_hw->ints0; // odczytujemy maskę aktywnych przerwań
    if (ints & (1u << dma_channel))
    {
        dma_hw->ints0 = 1u << dma_channel; // czyścimy flagę
        multicore_fifo_push_blocking(1);   // Wysłanie sygnału do rdzenia 1 //
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

void core1_entry()
{
    int s = 1000;
    FATFS fs;
    FIL file;
    UINT bw;
    FRESULT result;
    f_mount(&fs, "", 1);
    result = f_open(&file, "nagranie.wav", FA_WRITE | FA_CREATE_ALWAYS);
    if (result != FR_OK)

    {
        printf("sd problem");
    }
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
    write_wav_header(&file);
    while (s > 0)
    {

        uint32_t msg = multicore_fifo_pop_blocking(); // Blokujące oczekiwanie na sygnał z rdzenia 0
        if (msg == 1)
        {
            s--;
            dma_complete = false;
            // memcpy(pdm_buffer2, pdm_buffer, sizeof(pdm_buffer));
            // uint8_t *pdm8 = (uint8_t *)pdm_buffer2;
            uint8_t *pdm8 = (uint8_t *)ready_pdm_buffer;
            for (uint8_t i = 0; i < 8; i++)
            {
                uint8_t *chunk = &pdm8[i * IN_DATA_LEN];
                Open_PDM_Filter_128(chunk, (uint16_t *)&pcm_buffer[i * FREQ_PCM], 64, &filter);
            }
            // uint32_t *pdm_buffer2 = (uint32_t *)pdm8;
            result = f_write(&file, pcm_buffer, sizeof(pcm_buffer), &bw);
            if (result != FR_OK)

            {
                printf("sd write problem\n");
            }
        }
        tight_loop_contents();
    }
    finalize_wav_file(&file);
    f_close(&file); // Zamknięcie pliku po zakończeniu nagrywania
    f_unmount("");
    printf("\nnagranie.wav ready\n");
}

// Główna funkcja programu
int main()
{

    stdio_init_all();

    printf("Hello world");
    sleep_ms(5000);
    printf("Hello again");

    pdm_buffer[1] = 12;
    PIO pio = pio0;
    uint sm = 0;

    init_pdm(pio, sm); // Inicjalizacja mikrofonu PDM w PIO
    pio_sm_set_enabled(pio, sm, false);
    active_pdm_buffer = pdm_buffer; // Start z bufora 0
    init_dma(pio, sm);              // Inicjalizacja DMA do przesyłania danych
    pio_sm_set_enabled(pio, sm, true);

    multicore_launch_core1(core1_entry);

    while (1)
    {
        __wfi();
        tight_loop_contents(); // Zapewnienie ciągłej pracy
    }

    return 0;
}
