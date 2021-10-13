f = open("speaker_output.csv", "r")

# skip header
f.readline()

bank_0 = []
bank_1 = []

for line in f:
    line = line.strip()
    cols = line.split(",")

    # skip reset register
    if cols[2] == "S/W RESET":
        continue

    # skip reserved registers
    if cols[2] == "RESERVED":
        continue

    # skip page select register
    if cols[-2] == "0x00":
        continue
    
    # skip weird register
    if cols[2] == "\"PLL PROG REG A: P VAL":
        continue

    if cols[0] == "0":
        bank_0.append((cols[-2], cols[-1], cols[2]))

    if cols[0] == "1":
        bank_1.append((cols[-2], cols[-1], cols[2]))

f.close()
f = open("speaker_output.h", "w")

f.write("""#include <stdint.h>

typedef struct {
    uint8_t addr;
    uint8_t val;
} tlv320_reg_config_t;

""")

for i, bank in enumerate((bank_0, bank_1)):
    f.write("tlv320_reg_config_t tlv_reg_config_bank_{}[] = {{\n".format(i))

    for reg_addr, value, description in bank:
        f.write("    {")
        f.write(reg_addr)
        f.write(", ")
        f.write(value)
        f.write("},\t// ")
        f.write(description)
        f.write("\n")
    
    f.write("};\n\n")
