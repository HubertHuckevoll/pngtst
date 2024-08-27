#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define WINDOW_SIZE 32768
#define BUFFER_SIZE 4096
#define OUT_BUFFER_SIZE 4096

unsigned char window[WINDOW_SIZE];
unsigned char in_buffer[BUFFER_SIZE];
unsigned char out_buffer[OUT_BUFFER_SIZE];

unsigned short literal_huffman[288];
unsigned short distance_huffman[32];

unsigned char literal_code_lengths[288];
unsigned char distance_code_lengths[32];

unsigned char code_length_order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

// Funktion zum Lesen von Bits aus dem Stream
int read_bits(FILE *file, int *bitpos, int *buffer, int count)
{
    int result = 0;
    int i = 0;
    for (i = 0; i < count; i++) {
        if (*bitpos == 0)
        {
            *buffer = fgetc(file);
            *bitpos = 8;
        }
        result |= ((*buffer >> 7) & 1) << i;
        *buffer <<= 1;
        (*bitpos)--;
    }
    return result;
}

// Funktion zur Erstellung der Huffman-Tabelle
void generate_huffman_table(unsigned char *code_lengths, int num_codes, unsigned short *huffman_table)
{
    int max_code_length = 0;
    int bl_count[16] = {0};
    int next_code[16] = {0};
    int i = 0;
    int bits = 0;
    int code = 0;

    for (i = 0; i < num_codes; i++)
    {
        bl_count[code_lengths[i]]++;
        if (code_lengths[i] > max_code_length)
        {
            max_code_length = code_lengths[i];
        }
    }

    for (bits = 1; bits <= max_code_length; bits++)
    {
        code = (code + bl_count[bits - 1]) << 1;
        next_code[bits] = code;
    }

    for (i = 0; i < num_codes; i++)
    {
        int len = code_lengths[i];
        if (len != 0)
        {
            huffman_table[i] = next_code[len];
            next_code[len]++;
        }
        else
        {
            huffman_table[i] = 0;
        }
    }
}

// Funktion zum Dekodieren von Huffman-Symbolen
int decode_huffman_symbol(FILE *file, unsigned short *huffman_table, int *bitpos, int *buffer)
{
    int symbol = 0;
    int len = 0;
    int i = 0;

    while (1)
    {
        symbol |= read_bits(file, bitpos, buffer, 1) << len;
        len++;

        for (i = 0; i < 288; i++)
        {
            if (huffman_table[i] == symbol)
            {
                return i;
            }
        }
    }
}

// Funktion zur Erstellung der Huffman-Tabellen aus DEFLATE-Daten
void build_huffman_tables(FILE *file, int *bitpos, int *buffer)
{
    int hlit = read_bits(file, bitpos, buffer, 5) + 257;
    int hdist = read_bits(file, bitpos, buffer, 5) + 1;
    int hclen = read_bits(file, bitpos, buffer, 4) + 4;
    unsigned short code_length_huffman[19];
    unsigned char code_length_code_lengths[19] = {0};
    int i = 0;
    int num_lengths = 0;

    for (i = 0; i < hclen; i++)
    {
        code_length_code_lengths[code_length_order[i]] = read_bits(file, bitpos, buffer, 3);
    }

    generate_huffman_table(code_length_code_lengths, 19, code_length_huffman);

    num_lengths = hlit + hdist;

    while (i < num_lengths)
    {
        int symbol = decode_huffman_symbol(file, code_length_huffman, bitpos, buffer);

        if (symbol < 16) {
            if (i < hlit) {
                literal_code_lengths[i++] = symbol;
            } else {
                distance_code_lengths[i++ - hlit] = symbol;
            }
        } else if (symbol == 16) {
            int repeat = 3 + read_bits(file, bitpos, buffer, 2);
            int prev_length = (i > 0) ? (i < hlit ? literal_code_lengths[i - 1] : distance_code_lengths[i - hlit - 1]) : 0;
            while (repeat-- && i < num_lengths) {
                if (i < hlit) {
                    literal_code_lengths[i++] = prev_length;
                } else {
                    distance_code_lengths[i++ - hlit] = prev_length;
                }
            }
        } else if (symbol == 17) {
            int repeat = 3 + read_bits(file, bitpos, buffer, 3);
            while (repeat-- && i < num_lengths) {
                if (i < hlit) {
                    literal_code_lengths[i++] = 0;
                } else {
                    distance_code_lengths[i++ - hlit] = 0;
                }
            }
        } else if (symbol == 18) {
            int repeat = 11 + read_bits(file, bitpos, buffer, 7);
            while (repeat-- && i < num_lengths) {
                if (i < hlit) {
                    literal_code_lengths[i++] = 0;
                } else {
                    distance_code_lengths[i++ - hlit] = 0;
                }
            }
        }
    }

    generate_huffman_table(literal_code_lengths, hlit, literal_huffman);
    generate_huffman_table(distance_code_lengths, hdist, distance_huffman);
}

// PNG Filter Dekodierungsfunktionen
void unfilter_sub(unsigned char *row, int bytes_per_pixel, int row_length) {
    int i = 0;
    for (i = bytes_per_pixel; i < row_length; i++) {
        row[i] += row[i - bytes_per_pixel];
    }
}

void unfilter_up(unsigned char *row, unsigned char *prev_row, int row_length) {
    int i = 0;
    for (i = 0; i < row_length; i++) {
        row[i] += prev_row[i];
    }
}

void unfilter_average(unsigned char *row, unsigned char *prev_row, int bytes_per_pixel, int row_length) {
    int i = 0;
    for (i = 0; i < bytes_per_pixel; i++) {
        row[i] += prev_row[i] / 2;
    }
    for (i = bytes_per_pixel; i < row_length; i++) {
        row[i] += (row[i - bytes_per_pixel] + prev_row[i]) / 2;
    }
}

void unfilter_paeth(unsigned char *row, unsigned char *prev_row, int bytes_per_pixel, int row_length) {
    int i = 0;
    for (i = 0; i < bytes_per_pixel; i++) {
        row[i] += prev_row[i];
    }
    for (i = bytes_per_pixel; i < row_length; i++) {
        int a = row[i - bytes_per_pixel];
        int b = prev_row[i];
        int c = prev_row[i - bytes_per_pixel];
        int p = a + b - c;
        int pa = abs(p - a);
        int pb = abs(p - b);
        int pc = abs(p - c);
        if (pa <= pb && pa <= pc) {
            row[i] += a;
        } else if (pb <= pc) {
            row[i] += b;
        } else {
            row[i] += c;
        }
    }
}

// Funktion zum Verarbeiten und Dekodieren von IDAT-Chunks
void process_idat_chunks(FILE *file, unsigned char *image_data, int width, int height, int bytes_per_pixel) {
    int bitpos = 0;
    int buffer = 0;
    int row_length = width * bytes_per_pixel;
    unsigned char *prev_row = (unsigned char *)calloc(row_length, sizeof(unsigned char));
    unsigned char *curr_row = (unsigned char *)malloc(row_length * sizeof(unsigned char));
    int y = 0;

    for (y = 0; y < height; y++)
    {
        int filter_type = fgetc(file);
        fread(curr_row, 1, row_length, file);

        switch (filter_type) {
            case 0: break; // None
            case 1: unfilter_sub(curr_row, bytes_per_pixel, row_length); break;
            case 2: unfilter_up(curr_row, prev_row, row_length); break;
            case 3: unfilter_average(curr_row, prev_row, bytes_per_pixel, row_length); break;
            case 4: unfilter_paeth(curr_row, prev_row, bytes_per_pixel, row_length); break;
            default: printf("Unbekannter Filtertyp\n"); exit(1);
        }

        memcpy(image_data + y * row_length, curr_row, row_length);
        memcpy(prev_row, curr_row, row_length);
    }

    free(prev_row);
    free(curr_row);
}

// Hauptfunktion zum Dekodieren von PNG und Rückgabe von RGB-Daten
unsigned char *decode_png(const char *filename, int *width_out, int *height_out)
{
    unsigned char header[8];
    unsigned long width, height, bit_depth, color_type, compression, filter, interlace;
    unsigned char *image_data = NULL;
    int bytes_per_pixel = 3;
    unsigned char buffer[8];

    FILE *file = fopen(filename, "rb");

    if (!file)
    {
        printf("Kann Datei nicht öffnen: %s\n", filename);
        return NULL;
    }

    // PNG-Header lesen
    fread(header, 1, 8, file);
    if (memcmp(header, "\x89PNG\x0D\x0A\x1A\x0A", 8) != 0)
    {
        printf("Keine gültige PNG-Datei.\n");
        fclose(file);
        return NULL;
    }

    // Chunks verarbeiten
    while (fread(buffer, 1, 8, file) == 8) {
        unsigned long length = ((unsigned long)buffer[0] << 24) | ((unsigned long)buffer[1] << 16) | ((unsigned long)buffer[2] << 8) | (unsigned long)buffer[3];
        unsigned long type = ((unsigned long)buffer[4] << 24) | ((unsigned long)buffer[5] << 16) | ((unsigned long)buffer[6] << 8) | (unsigned long)buffer[7];

        if (type == 0x49484452) { // 'IHDR' Chunk
            fread(buffer, 1, length, file);
            width = ((unsigned long)buffer[0] << 24) | ((unsigned long)buffer[1] << 16) | ((unsigned long)buffer[2] << 8) | (unsigned long)buffer[3];
            height = ((unsigned long)buffer[4] << 24) | ((unsigned long)buffer[5] << 16) | ((unsigned long)buffer[6] << 8) | (unsigned long)buffer[7];
            bit_depth = buffer[8];
            color_type = buffer[9];
            compression = buffer[10];
            filter = buffer[11];
            interlace = buffer[12];

            // Bestimmen der Bytes pro Pixel basierend auf Farbtyp
            if (color_type == 2) { // Truecolor
                bytes_per_pixel = 3;
            } else if (color_type == 6) { // Truecolor mit Alpha
                bytes_per_pixel = 4;
            } else {
                printf("Farbtyp nicht unterstützt.\n");
                fclose(file);
                return NULL;
            }

            *width_out = width;
            *height_out = height;
            image_data = (unsigned char *)malloc(width * height * bytes_per_pixel);
        } else if (type == 0x49444154) { // 'IDAT' Chunk
            fseek(file, -8, SEEK_CUR); // Gehe zum Anfang des Chunks zurück
            process_idat_chunks(file, image_data, width, height, bytes_per_pixel);
            fseek(file, length + 4, SEEK_CUR); // Springe zum Ende des Chunks
        } else if (type == 0x49454E44) { // 'IEND' Chunk
            break;
        } else {
            fseek(file, length + 4, SEEK_CUR); // Überspringen des Chunks
        }
    }

    fclose(file);
    return image_data;
}

int main(int argc, char *argv[])
{
    int width, height;
    int x = 0;
    int y = 0;
    unsigned char *image_data = NULL;

    if (argc != 2)
    {
        printf("Verwendung: %s <png-datei>\n", argv[0]);
        return 1;
    }

    image_data = decode_png(argv[1], &width, &height);
    if (!image_data)
    {
        return 1;
    }

    // Ausgabe der RGB-Daten (hier als Beispiel auf der Konsole)
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            unsigned char *pixel = &image_data[(y * width + x) * 3];
            printf("%d, %d, %d\n", pixel[0], pixel[1], pixel[2]);
        }
        printf("\n");
    }

    free(image_data);
    return 0;
}
