/* main.c
Copyright 2021 Carl John Kugler III

Licensed under the Apache License, Version 2.0 (the License); you may not use
this file except in compliance with the License. You may obtain a copy of the
License at

   http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an AS IS BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied. See the License for the
specific language governing permissions and limitations under the License.
*/

#include <stdio.h>
//
#include "stdlib.h"
//
#include "hw_config.h"
#include "f_util.h"
#include "ff.h"

/**
 * @file main.c
 * @brief Minimal example of writing to a file on SD card
 * @details
 * This program demonstrates the following:
 * - Initialization of the stdio
 * - Mounting and unmounting the SD card
 * - Opening a file and writing to it
 * - Closing a file and unmounting the SD card
 */
void polinii();
void wwr()
{
    FATFS fs;
    FRESULT fr = f_mount(&fs, "./", 1);
    if (FR_OK != fr)
    {
        panic("f_mount error: %s (%d)\n", FRESULT_str(fr), fr);
    }

    // Open a file and write to it
    FIL fil;
    const char *const filename = "filename.txt";
    fr = f_open(&fil, filename, FA_OPEN_APPEND | FA_WRITE);
    if (FR_OK != fr && FR_EXIST != fr)
    {
        panic("f_open(%s) error: %s (%d)\n", filename, FRESULT_str(fr), fr);
    }
    if (f_printf(&fil, "Hello, world!\n") < 0)
    {
        printf("f_printf failed\n");
    }

    if (fr == FR_OK)
    {
        gpio_put(16, 1);
    }
    else
    {
        gpio_put(16, 0);
    }

    // Close the file
    fr = f_close(&fil);
    if (FR_OK != fr)
    {
        printf("f_close error: %s (%d)\n", FRESULT_str(fr), fr);
    }
    if (fr == FR_OK)
    {
        gpio_put(16, 1);
    }
    else
    {
        gpio_put(16, 0);
    }
    // Unmount the SD card
    f_unmount("");
}

int main()
{
    // Initialize stdio
    stdio_init_all();
    // gpio_init(16);              // Inicjalizacja pinu GPIO 16
    // gpio_set_dir(16, GPIO_OUT); // Ustawienie jako wyjście
    printf("Hello, world!");

    // See FatFs - Generic FAT Filesystem Module, "Application Interface",
    // http://elm-chan.org/fsw/ff/00index_e.html
    FATFS fs;
    FRESULT fr = f_mount(&fs, "./", 1);
    if (FR_OK != fr)
    {
        printf("not OK");
    }

    // Open a file and write to it
    FIL fil;
    const char *const filename = "filename.txt";
    fr = f_open(&fil, filename, FA_OPEN_APPEND | FA_WRITE);
    if (FR_OK != fr && FR_EXIST != fr)
    {
        printf("still not OK");
    }
    if (f_printf(&fil, "Hello, world!\n") < 0)
    {
        printf("f_printf failed\n");
    }

    // Close the file
    fr = f_close(&fil);
    if (FR_OK != fr)
    {
        printf("f_close error: %s (%d)\n", FRESULT_str(fr), fr);
    }

    UINT bw, br;
    char readBuffer[50];

    if (f_open(&fil, "www.txt.txt", FA_READ) == FR_OK)
    {
        f_read(&fil, readBuffer, sizeof(readBuffer) - 1, &br);
        readBuffer[br] = '\0'; // Dodanie końca stringa

        f_close(&fil);
    }
    if (f_open(&fil, "www.txt.txt", FA_OPEN_APPEND | FA_WRITE) == FR_OK)
    {
        f_write(&fil, readBuffer, br + 1, &bw);
        f_close(&fil);
    }
    // Unmount the SD card
    f_unmount("");

    printf("Goodbye, world!");
    while (1)
    {
        // wwr();
        printf("witam\n");
        printf("Odczytane dane: %s\n", readBuffer);
        sleep_ms(1000);
        // gpio_put(16, 0);
        sleep_ms(300);
        polinii();
    }
}

void polinii()
{
    FATFS fs;
    FIL file;
    char line[100]; // Bufor na linię
    // Montowanie systemu plików
    f_mount(&fs, "", 0);

    // Otwieranie pliku do odczytu
    if (f_open(&file, "test.txt", FA_READ) == FR_OK)
    {
        DWORD fileSize = file.obj.objsize;
        printf("Rozmiar pliku: %lu bajtów\n", fileSize);
        // Czytanie linii po linii
        while (f_gets(line, sizeof(line), &file))
        {
            printf("Odczytana linia: %s", line); // Drukujemy linię
        }
        f_close(&file);
    }
    else
    {
        printf("Nie można otworzyć pliku!\n");
    }

    // Odmontowanie systemu plików
    f_mount(NULL, "", 0);
}