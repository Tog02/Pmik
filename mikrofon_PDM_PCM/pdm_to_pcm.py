import numpy as np
import matplotlib.pyplot as plt
from scipy import signal
from scipy.io.wavfile import write

# --- PARAMETRY ---
fs_pdm = 2000000
fs_pcm = 8000
decimation = fs_pdm // fs_pcm

# --- 1. WCZYTANIE uint32 i KONWERSJA NA BITY ---
raw_u32 = np.fromfile("rec.bin", dtype=np.uint32)


# --- 2. ODWRÓCENIE BITÓW W KAŻDYM BAJCIE ---


def reverse_bits(byte):
    return int('{:32b}'.format(byte)[::-1], 2)


reversed_bytes = np.array([reverse_bits(b) for b in raw_u32], dtype=np.uint32)
raw_bytes = reversed_bytes.view(np.uint8)
pdm_bits = np.unpackbits(raw_bytes)

# --- 3. PDM -> float (-1, +1) ---
pdm_float = pdm_bits.astype(np.float32) * 2 - 1

# --- 4. DC BLOCK ---
pdm_float -= np.mean(pdm_float)

# --- 5. FILTRACJA + DECYMACJA ---
fir_taps = signal.firwin(
    numtaps=2055,
    cutoff=0.45 / decimation,
    window=('kaiser', 8.6)
)

pcm = signal.filtfilt(fir_taps, 1.0, pdm_float)[::decimation]

# --- 6. NORMALIZACJA I ZAPIS WAV ---
pcm /= np.max(np.abs(pcm)) + 1e-10
pcm_int16 = (pcm * 32767).astype(np.int16)
write("output_pro_pcm.wav", fs_pcm, pcm_int16)

# --- 7. FFT WIZUALIZACJA (32k próbek) ---
N = 32768
segment = pcm[:N] * np.hamming(N)
fft_result = np.fft.rfft(segment)
fft_freqs = np.fft.rfftfreq(N, 1/fs_pcm)
fft_db = 20 * np.log10(np.abs(fft_result) + 1e-10)

plt.figure(figsize=(10, 4))
plt.plot(fft_freqs, fft_db)
plt.title("Widmo FFT (PCM, Kaiser)")
plt.xlabel("Częstotliwość [Hz]")
plt.ylabel("Amplituda [dB]")
plt.grid()
plt.tight_layout()
plt.savefig("fft_output.png")
plt.show()
