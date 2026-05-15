
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <i2c/smbus.h>
#include "Si5351A-RevB-Registers.h" 

#define I2C_FNAME "/dev/i2c-3"
#define SI5351_ADDR 0x60

int i2c_file;

void i2c_init() {
    i2c_file = open(I2C_FNAME, O_RDWR);
    if (i2c_file < 0) {
        perror("I2C: Failed to access the bus");
        exit(1);
    }
    if (ioctl(i2c_file, I2C_SLAVE, SI5351_ADDR) < 0) {
        perror("I2C: Failed to connect to the device");
        exit(1);
    }
}
int i2c_read(unsigned char reg)
{
	if (ioctl(i2c_file, I2C_SLAVE, SI5351_ADDR) < 0) 
    {
        perror(I2C_FNAME);
		exit(1);
    }
	int res;
	/* Using SMBus commands */
	res = i2c_smbus_read_byte_data(i2c_file, reg);
	if (res < 0) 
		exit(1);
	else 
		printf("r dev(0x%x) reg(0x%x)=0x%x (decimal %d)\n", SI5351_ADDR, reg, res, res);
	return res;
}

void i2c_write(unsigned char reg, unsigned char value) {
    if (i2c_smbus_write_byte_data(i2c_file, reg, value) < 0) {
        perror("I2C: Failed to write to the device");
        exit(1);
        usleep(100); // Delay for register write to settle
    }
}

void apply_si5351_config() {

    //Disable Outputs Reg. 3 = 0xFF
    i2c_write(3, 0xFF);

    //Power-down all output drivers Reg. 16, 17, 18, 19, 20, 21, 22, 23 = 0x80
    i2c_write(16, 0x80);
    i2c_write(17, 0x80);
    i2c_write(18, 0x80);
    i2c_write(19, 0x80);
    i2c_write(20, 0x80);
    i2c_write(21, 0x80);
    i2c_write(22, 0x80);
    i2c_write(23, 0x80);
  
    // Write new configuration to device
    for (int i = 0; i < SI5351A_REVB_REG_CONFIG_NUM_REGS; ++i) {
        si5351a_revb_register_t reg = si5351a_revb_registers[i];
        i2c_write(reg.address, reg.value);
        //i2c_read(reg.address);
    }
    
    i2c_read(0x00A5);
    i2c_read(0x00A6);
    //i2c_write(0x00A5, 0x00);
    //i2c_write(0x00A6, 0x82);
    
    // Apply PLLA and PLLB soft reset 
    i2c_write(177, 0xAC);

    // Enable outputs with OEB control in register 3. 
    // Enable clk0 and clk1 output, reg0=0b0000000

    i2c_write(3, 0x00);

    

}

int main() {
    i2c_init();
    apply_si5351_config();   
}
