//Standard C libraries needed
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#include "Si5351A-RevB-Registers.h"   // This one for Lab 4

#define I2C_FNAME   "/dev/i2c-3"   // Lab I2C bus this is bus 3
#define SI5351_ADDR 0x60           // Si5351's I2C slave address

#define XXX_PHASE_VALUE 0x00       // Replace later with real CLK1_PHOFF value

static int i2c_file = -1;

//error helper
static void die(const char *msg)
{
    perror(msg);
    exit(1);
}


//Opens /dev/i2c-3 so program can talk on that bus
static void i2c_init(void)
{
    i2c_file = open(I2C_FNAME, O_RDWR);
    if (i2c_file < 0)
        die("open");

        //Tells Linux which slave on that bus you want to talk to
    if (ioctl(i2c_file, I2C_SLAVE, SI5351_ADDR) < 0)
        die("ioctl");
}

//Close device
static void i2c_close_dev(void)
{
    if (i2c_file >= 0) {
        close(i2c_file);
        i2c_file = -1;
    }
}


//Si5351, write value into register reg... first byte = register address and second byte = value to store there
static void i2c_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2];
    buf[0] = reg;      // Register address
    buf[1] = value;    // Value to write

    if (write(i2c_file, buf, 2) != 2)
        die("write");

    printf("w dev(0x%02x) reg(0x%02x)=0x%02x\n", SI5351_ADDR, reg, value);
}

//safely turns the outputs off before it reconfigures chips
static void si5351_disable_outputs(void)
{
    i2c_write_reg(3, 0xFF);    // Disable all outputs
}


//power downs all output
static void si5351_powerdown_all_outputs(void)
{
    for (uint8_t reg = 16; reg <= 23; reg++) {
        i2c_write_reg(reg, 0x80);   // Power down CLK0..CLK7 drivers
    }
}


//ClockBuilder will give { address, value } and this goes through every entry and writes in chip
static void si5351_write_config_map(void)
{
    for (int i = 0; i < SI5351A_REVB_REG_CONFIG_NUM_REGS; i++) {
        uint8_t reg   = (uint8_t)(si5351a_revb_registers[i].address & 0xFF);
        uint8_t value = si5351a_revb_registers[i].value;
        i2c_write_reg(reg, value);  // Write one exported register setting
    }
}


//writes register 177 (177 is the PLL reset register) with 0xAC (according to AN619 bit 7 = reset PLLB and bit 5 = reset PLLA)
static void si5351_soft_reset_plls(void)
{
    i2c_write_reg(177, 0xAC);   // Reset PLLA and PLLB
}


//This clears register 3, so all CLKx_OEB bits become 0
static void si5351_enable_outputs(void)
{
    i2c_write_reg(3, 0x00);     // Re-enable outputs
}

//run functions above
static void si5351_program_from_header(void)
{
    si5351_disable_outputs();       // Step 1
    si5351_powerdown_all_outputs(); // Step 2
    si5351_write_config_map();      // Step 3/4/5
    si5351_soft_reset_plls();       // Step 6
    si5351_enable_outputs();        // Step 7
}

//AN619 identifies register 166 as CLK1 Initial Phase Offset, CLK1_PHOFF[6:0]. For Q3 part 2
static void si5351_apply_phase_offset_clk1(uint8_t phase_value)
{
    si5351_disable_outputs();             // Turn outputs off first
    i2c_write_reg(0xA6, phase_value);     // CLK1_PHOFF register
    si5351_soft_reset_plls();             // Apply change cleanly
    si5351_enable_outputs();              // Turn outputs back on
}

int main(void)
{
    i2c_init();
    si5351_program_from_header();
    i2c_close_dev();
    return 0;
}
