#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/timer.h"
#include "pico/cyw43_arch.h"

#include "pdm.pio.h" // Plik PIO do odbioru PDM
#include "ff.h"      // Biblioteka FatFS do obsługi karty SD
#include "hw_config.h"
#include "f_util.h"

#define SAMPLE_RATE 8000  // Częstotliwość próbkowania (Hz)
#define BUFFER_SIZE 512   // Rozmiar bufora audio
#define PDM_CLOCK_DIV 125 // Podział zegara PDM, dostosowany do częstotliwości 8 kHz
#define YOUR_PDM_PIN 16
#define YOUR_CLK_PIN 15

uint32_t pdm_buffer[BUFFER_SIZE];   // Bufor przechowujący surowe dane PDM
uint8_t pcm_buffer[BUFFER_SIZE];    // Bufor do przechowywania przekonwertowanych próbek PCM
volatile bool dma_complete = false; // Flaga sygnalizująca zakończenie transferu DMA
int dma_channel;                    // Kanał DMA używany do transferu danych

// Obsługa przerwania DMA
void dma_handler()
{
    dma_complete = true;
    dma_hw->ints0 = 1u << dma_channel; // Czyścimy flagę przerwania dla konkretnego kanału DMA
}

// Inicjalizacja DMA do pobierania danych z PIO
void init_dma(PIO pio, uint sm)
{
    dma_channel = dma_claim_unused_channel(true);                            // Przydzielenie dostępnego kanału DMA
    dma_channel_config config = dma_channel_get_default_config(dma_channel); // Pobranie domyślnej konfiguracji kanału DMA

    channel_config_set_transfer_data_size(&config, DMA_SIZE_32);    // Konfiguracja rozmiaru przesyłanych danych (32 bity)
    channel_config_set_read_increment(&config, false);              // Brak inkrementacji adresu źródłowego (odczyt z jednego rejestru FIFO PIO)
    channel_config_set_write_increment(&config, true);              // Inkrementacja adresu docelowego (zapisywanie kolejnych wartości w buforze)
    channel_config_set_dreq(&config, pio_get_dreq(pio, sm, false)); // Ustawienie żądania transferu (DREQ) od PIO

    dma_channel_configure(
        dma_channel, &config,
        pdm_buffer,    // Bufor docelowy dla DMA (miejsce, gdzie będą zapisywane dane PDM)
        &pio->rxf[sm], // Adres źródłowy - FIFO odbiorcze PIO
        BUFFER_SIZE,   // Liczba transferów (ilość próbek do pobrania)
        true           // Natychmiastowe uruchomienie transferu DMA
    );

    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler); // Przypisanie funkcji obsługi przerwania DMA
    irq_set_enabled(DMA_IRQ_0, true);                  // Włączenie obsługi przerwań DMA
    dma_channel_set_irq0_enabled(dma_channel, true);   // Aktywacja przerwań dla wybranego kanału DMA
}

// Inicjalizacja interfejsu PDM w PIO
void init_pdm(PIO pio, uint sm)
{
    uint offset = pio_add_program(pio, &pdm_microphone_program);
    pdm_microphone_program_init(pio, sm, offset, YOUR_PDM_PIN, YOUR_CLK_PIN);
    pio_sm_set_clkdiv(pio, sm, PDM_CLOCK_DIV);
}

// Przetwarzanie danych PDM na PCM i zapis do pliku
void process_audio(FIL *file)
{
    for (int i = 0; i < BUFFER_SIZE; i++)
    {
        int32_t sum = 0;
        uint32_t pdm_sample = pdm_buffer[i];

        for (int j = 0; j < 32; j++)
        {
            sum += (pdm_sample & (1 << j)) ? 1 : -1; // Konwersja PDM na PCM metodą uśredniania
        }
        pcm_buffer[i] = (sum / 32) + 128; // Normalizacja wartości do formatu 8-bitowego PCM
    }
    UINT bw;
    f_write(file, pcm_buffer, BUFFER_SIZE, &bw); // Zapis danych PCM do pliku WAV
}

// Tworzenie nagłówka pliku WAV
void write_wav_header(FIL *file)
{
    UINT bw;
    uint32_t file_size = 0;
    uint32_t sample_rate = SAMPLE_RATE;
    uint16_t bits_per_sample = 8;
    uint16_t num_channels = 1;
    uint32_t byte_rate = sample_rate * num_channels * (bits_per_sample / 8);
    uint16_t block_align = num_channels * (bits_per_sample / 8);

    BYTE header[44] = {
        'R', 'I', 'F', 'F', file_size, 0, 0, 0, 'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, num_channels, 0,
        sample_rate, 0, 0, 0, byte_rate, 0, 0, 0, block_align, 0,
        bits_per_sample, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0};
    f_write(file, header, 44, &bw);
}

// Główna funkcja programu
int main()
{
    stdio_init_all();
    PIO pio = pio0;
    uint sm = 0;
    init_pdm(pio, sm); // Inicjalizacja mikrofonu PDM w PIO
    init_dma(pio, sm); // Inicjalizacja DMA do przesyłania danych

    FATFS fs;
    FIL file;
    f_mount(&fs, "", 1);
    f_open(&file, "record.wav", FA_WRITE | FA_CREATE_ALWAYS);
    write_wav_header(&file); // Zapis nagłówka WAV

    while (1)
    {
        if (dma_complete)
        {
            dma_complete = false;
            process_audio(&file);                                                       // Przetwarzanie i zapis danych
            dma_channel_transfer_from_buffer_now(dma_channel, pdm_buffer, BUFFER_SIZE); // Ponowne uruchomienie DMA
        }
        tight_loop_contents(); // Zapewnienie ciągłej pracy
    }

    f_close(&file); // Zamknięcie pliku po zakończeniu nagrywania
    return 0;
}
