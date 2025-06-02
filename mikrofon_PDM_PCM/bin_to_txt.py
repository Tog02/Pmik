import numpy as np

# --- PARAMETRY ---
input_file = "rec.bin"   # Nazwa wejściowego pliku binarnego
output_file = "rec.txt"    # Nazwa pliku wynikowego

# --- WCZYTYWANIE DANYCH JAKO uint32 ---
data = np.fromfile(input_file, dtype=np.uint32)

# --- ZAPIS DO TXT ---
with open(output_file, "w") as f:
    for value in data:
        f.write(f"{value}\n")

print(f"Zapisano {len(data)} wartości do pliku: {output_file}")
