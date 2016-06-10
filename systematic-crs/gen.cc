#include <cassert>
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <cstdio>

#define EC_GF_BITS 8
#define EC_GF_MOD 0x11D
#define EC_GF_SIZE (1 << EC_GF_BITS)

static uint32_t bit[EC_GF_BITS];
static uint32_t GfPow[EC_GF_SIZE << 1];
static uint32_t GfLog[EC_GF_SIZE << 1];

void print_header() {
    const char* header = 
        "#include <string.h>\n"
        "#include \"ec-gf.h\"\n"
        "#include \"ec-method.h\"\n"
        "\n"
        "static void gf8_muladd_00(uint8_t* out, uint8_t* in) {\n"
        "    // this function should never be called\n"
        "    memset(out, 0x00, EC_METHOD_WIDTH * 8 * EC_GF_WORD_SIZE);\n"
        "}\n"
        "\n"
        ;
    printf("%s", header);
}

void print_tailer() {
    const char* tailer = 
       "void (* ec_gf_muladd[])(uint8_t * out, uint8_t * in) = {\n"
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

void tmp_variables(uint32_t* matrix, std::vector< std::vector<int> >& from, std::vector<int>& output_order) {
    using namespace std;

    assert(EC_GF_BITS == 8);
    uint8_t in[8] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };

    vector<uint8_t> tmp_vars;
    tmp_vars.insert(tmp_vars.end(), in, in + 8);
    tmp_vars.insert(tmp_vars.end(), matrix, matrix + 8);

    unordered_map<uint8_t, size_t> value_to_index;

    from.clear();
    for (int i = 0; i < 16; ++i) {
        from.push_back(vector<int>());
        for (int j = 0; j < 8; ++j)
            if (tmp_vars[i] & in[j])
                from[i].emplace_back(j);
        value_to_index[tmp_vars[i]] = i;
    }

    while (true) {
        bool g = false;
        for (size_t i = 8; i < tmp_vars.size(); ++i) 
            for (size_t j = 8; j < tmp_vars.size(); ++j) {
                if (i == j) continue;

                vector<int> common;
                set_intersection(from[i].begin(), from[i].end(), from[j].begin(), from[j].end(), back_inserter(common));
                if (common.size() < 2) continue;

                uint8_t common_var_value = 0;
                for (auto i: common)
                    common_var_value |= tmp_vars[i];

                auto index_ite = value_to_index.find(common_var_value);
                if (index_ite == value_to_index.end() || (index_ite->second < 16 && index_ite->second >= 8)) {
                    value_to_index[common_var_value] = tmp_vars.size();
                    tmp_vars.emplace_back(common_var_value);
                    from.emplace_back(common);
                } 

                vector<int> new_from;
                set_difference(from[i].begin(), from[i].end(), common.begin(), common.end(), back_inserter(new_from));
                new_from.emplace_back(value_to_index[common_var_value]);
                sort(new_from.begin(), new_from.end());
                if (from[i].size() > new_from.size() && 
                        find(new_from.begin(), new_from.end(), i) == new_from.end()) {
                    from[i] = new_from;
                    g = true;
                }

                new_from.clear();
                set_difference(from[j].begin(), from[j].end(), common.begin(), common.end(), back_inserter(new_from));
                new_from.emplace_back(value_to_index[common_var_value]);
                sort(new_from.begin(), new_from.end());
                if (from[j].size() > new_from.size() &&
                        find(new_from.begin(), new_from.end(), j) == new_from.end()) {
                    from[j] = new_from;
                    g = true;
                }
            }
        if (!g) break;
    }

    auto sort_output = [from](vector<int>& output_order) {
        output_order.clear();

        vector<bool> flag(16, 1);
        flag.resize(from.size(), 0);

        int num_not_output = from.size() - 16;
        while (num_not_output > 0) 
            for (size_t i = 16; i < from.size(); ++i) {
                size_t j;
                for (j = 0; j < from[i].size(); ++j)
                    if (!flag[from[i][j]]) break;
                if (j == from[i].size()) 
                    if (!flag[i]) {
                        output_order.emplace_back(i);
                        flag[i] = 1;
                        --num_not_output;
                    }
            }
    };

    sort_output(output_order);
}


void gen(int i) {
    const char* header = 
        "static void gf8_muladd_%02X(uint8_t* out, uint8_t* in) {\n"
        "    size_t i;\n"
        "    encode_t* in_ptr = (encode_t*)in;\n"
        "    encode_t* out_ptr = (encode_t*)out;\n"
        "    for (i = 0; i < EC_METHOD_WIDTH; ++i) {\n"
        "        encode_t in0 = in_ptr[EC_METHOD_WIDTH * 0];\n"
        "        encode_t in1 = in_ptr[EC_METHOD_WIDTH * 1];\n"
        "        encode_t in2 = in_ptr[EC_METHOD_WIDTH * 2];\n"
        "        encode_t in3 = in_ptr[EC_METHOD_WIDTH * 3];\n"
        "        encode_t in4 = in_ptr[EC_METHOD_WIDTH * 4];\n"
        "        encode_t in5 = in_ptr[EC_METHOD_WIDTH * 5];\n"
        "        encode_t in6 = in_ptr[EC_METHOD_WIDTH * 6];\n"
        "        encode_t in7 = in_ptr[EC_METHOD_WIDTH * 7];\n"
        ;
    const char* tailer =
        "        in_ptr++;\n"
        "        out_ptr++;\n"
        "    }\n"
        "}\n"
        "\n"
        ;

    printf(header, GfPow[i]);

    std::vector< std::vector<int> > from;
    std::vector<int> output_order;
    tmp_variables(GfPow + i, from, output_order);

    for (auto i: output_order) {
        printf("        encode_t tmp%d = XOR%lu(", i - 16, from[i].size());
        bool is_first = 1;
        for (auto j: from[i]) {
            if (!is_first) printf(", ");
            if (j < 8) printf("in%d", j);
            else printf("tmp%d", j - 16);
            is_first = 0;
        }
        printf(");\n");
    }
    for (int i = 8; i < 16; ++i) {
        printf("        out_ptr[EC_METHOD_WIDTH * %d] = XOR%lu(out_ptr[EC_METHOD_WIDTH * %d]", i - 8, from[i].size() + 1, i - 8);
        for (auto j: from[i]) {
            if (j < 8) printf(", in%d", j);
            else printf(", tmp%d", j - 16);
        }
        printf(");\n");
    }

    /*
    for (int row = 0; row < EC_GF_BITS; ++row) {
        int pop_count = __builtin_popcount(GfPow[i + row]);
        printf("        out_ptr[EC_METHOD_WIDTH * %d] = XOR%d(out_ptr[EC_METHOD_WIDTH * %d]", row, pop_count + 1, row);
        for (int col = 0; col < EC_GF_BITS; ++col) {
            if (GfPow[i + row] & bit[col]) {
                printf(", ");
                printf("in%d", col);
            }
        }
        printf(");\n");
    }
    */

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
