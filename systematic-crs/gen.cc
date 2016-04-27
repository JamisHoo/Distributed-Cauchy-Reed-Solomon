#include <iostream>
#include <cstdio>

#include "ec-method.h"

static uint32_t bit[EC_GF_BITS];
static uint32_t GfPow[EC_GF_SIZE << 1];
static uint32_t GfLog[EC_GF_SIZE << 1];

void print_header() {
    const char* header = 
        "#include <string.h>\n"
        "#include \"ec-gf.h\"\n"
        "\n"
        "static void gf8_muladd_00(uint8_t* out, uint8_t* in, size_t width) {\n"
        "    // this function should never be called\n"
        "    memset(out, 0x00, width * 8 * EC_GF_WORD_SIZE);\n"
        "}\n"
        "\n"
        ;
    printf("%s", header);
}

void print_tailer() {
    const char* tailer = 
       "void (* ec_gf_muladd[])(uint8_t * out, uint8_t * in, size_t width) = {\n"
    ;
    printf("%s", tailer);

    for (int i = 0; i < 256; ++i) {
        if (i % 4 == 0) printf("    ");
        printf("gf8_muladd_%02X", i);
        if (i != 255) printf(",");
        if (i % 4 == 3) printf("\n");
        else printf(" ");
    }
    printf("};\n");
}

void ec_method_initialize() {
    uint32_t i;
    uint32_t carry_mask;

    bit[0] = 0x01;
    for (i = 1; i < EC_GF_BITS; ++i)
        bit[i] = bit[i - 1] << 1;
    carry_mask = bit[EC_GF_BITS - 1] << 1;

    GfPow[0] = 1;
    for (i = 1; i < EC_GF_SIZE - 1 + EC_GF_BITS - 1; ++i) {
        GfPow[i] = GfPow[i - 1] << 1;
        if (GfPow[i] & carry_mask)
            GfPow[i] ^= EC_GF_MOD;
    }

    GfLog[0] = -1;
    for (i = 0; i < EC_GF_SIZE - 1; ++i)
        GfLog[GfPow[i]] = i;
}


void gen(int i) {
    const char* header = 
        "static void gf8_muladd_%02X(uint8_t* out, uint8_t* in, size_t width) {\n"
        "    size_t i;\n"
        "    encode_t* in_ptr = (encode_t*)in;\n"
        "    encode_t* out_ptr = (encode_t*)out;\n"
        "    for (i = 0; i < width; ++i) {\n"
        "        encode_t in0 = in_ptr[width * 0];\n"
        "        encode_t in1 = in_ptr[width * 1];\n"
        "        encode_t in2 = in_ptr[width * 2];\n"
        "        encode_t in3 = in_ptr[width * 3];\n"
        "        encode_t in4 = in_ptr[width * 4];\n"
        "        encode_t in5 = in_ptr[width * 5];\n"
        "        encode_t in6 = in_ptr[width * 6];\n"
        "        encode_t in7 = in_ptr[width * 7];\n"
        ;
    const char* tailer =
        "        in_ptr++;\n"
        "        out_ptr++;\n"
        "    }\n"
        "}\n"
        "\n"
        ;

    printf(header, GfPow[i]);

    for (int row = 0; row < EC_GF_BITS; ++row) {
        int pop_count = __builtin_popcount(GfPow[i + row]);
        printf("        out_ptr[width * %d] = XOR%d(out_ptr[width * %d]", row, pop_count + 1, row);
        for (int col = 0; col < EC_GF_BITS; ++col) {
            if (GfPow[i + row] & bit[col]) {
                printf(", ");
                printf("in%d", col);
            }
        }
        printf(");\n");
    }

    printf("%s", tailer);
}

int main() {
    ec_method_initialize();

    print_header();
    // gen(0xF8);
    for (int i = 0; i < 255; ++i)
        gen(i);
    print_tailer();
}
