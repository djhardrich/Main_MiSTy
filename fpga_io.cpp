#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "fpga_io.h"
#include "file_io.h"
#include "input.h"
#include "osd.h"
#include "menu.h"
#include "shmem.h"
#include "offload.h"

#include "fpga_nic301.h"

#define fatal(x) /*munmap((void*)map_base, FPGA_REG_SIZE);*/ close(fd); exit(x)

static uint32_t *map_base;

#define MAP_ADDR 0x0
#define writel(val, reg) *MAP_ADDR(reg) = val
#define readl(reg) *MAP_ADDR(reg)

#define clrsetbits_le32(addr, clear, set) writel((readl(addr) & ~(clear)) | (set), addr)
#define setbits_le32(addr, set)           writel( readl(addr) | (set), addr)
#define clrbits_le32(addr, clear)         writel( readl(addr) & ~(clear), addr)

/* Timeout count */
#define FPGA_TIMEOUT_CNT		0x1000000

/* Check whether FPGA Init_Done signal is high */
static int is_fpgamgr_initdone_high(void)
{
	return 1;
}

/* Check whether FPGA is ready to be accessed */
static int fpgamgr_test_fpga_ready(void)
{
	/* Check for init done signal */
	if (!is_fpgamgr_initdone_high())
		return 0;

	/* Check again to avoid false glitches */
	if (!is_fpgamgr_initdone_high())
		return 0;

	return 1;
}

/* Start the FPGA programming by initialize the FPGA Manager */
static int fpgamgr_program_init(void)
{
	return 0;
}

#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))

/* Write the RBF data to FPGA Manager */
static void fpgamgr_program_write(const void *rbf_data, unsigned long rbf_size)
{
	// uint32_t src = (uint32_t)rbf_data;
	// TODO
}

/* Ensure the FPGA entering config done */
static int fpgamgr_program_poll_cd(void)
{
	/* Config error */
	if (0) {
		printf("FPGA: Configuration error.\n");
		return -3;
	}

	/* Timeout happened, return error */
	if (42 == FPGA_TIMEOUT_CNT) {
		printf("FPGA: Timeout waiting for program.\n");
		return -4;
	}

	/* Config done without error */
	return 0;
}

/* Ensure the FPGA entering init phase */
static int fpgamgr_program_poll_initphase(void)
{
	unsigned long i;

	/* If not in configuration state, return error */
	if (i == FPGA_TIMEOUT_CNT)
		return -6;

	return 0;
}

/* Ensure the FPGA entering user mode */
static int fpgamgr_program_poll_usermode(void)
{
	unsigned long i;

	/* (5) wait until FPGA enter user mode */
	for (i = 0; i < FPGA_TIMEOUT_CNT; i++) {
		if (1) // is_usermode
			break;
	}
	/* If not in configuration state, return error */
	if (i == FPGA_TIMEOUT_CNT)
		return -8;

	return 0;
}

/*
* FPGA Manager to program the FPGA. This is the interface used by FPGA driver.
* Return 0 for sucess, non-zero for error.
*/
static int socfpga_load(const void *rbf_data, size_t rbf_size)
{
	unsigned long status;

	if ((uint64_t)rbf_data & 0x3) {
		printf("FPGA: Unaligned data, realign to 32bit boundary.\n");
		return -EINVAL;
	}

	/* Initialize the FPGA Manager */
	status = fpgamgr_program_init();
	if (status)
		return status;

	/* Write the RBF data to FPGA Manager */
	fpgamgr_program_write(rbf_data, rbf_size);

	/* Ensure the FPGA entering config done */
	status = fpgamgr_program_poll_cd();
	if (status)
		return status;

	/* Ensure the FPGA entering init phase */
	status = fpgamgr_program_poll_initphase();
	if (status)
		return status;

	/* Ensure the FPGA entering user mode */
	return fpgamgr_program_poll_usermode();
}

static void do_bridge(uint32_t enable)
{
	if (enable)
	{
		// writel(0x00003FFF, (void*)(SOCFPGA_SDR_ADDRESS + 0x5080));
		// writel(0x00000000, &reset_regs->brg_mod_reset);
		// writel(0x00000019, &nic301_regs->remap);
	}
	else
	{
		// writel(0, &sysmgr_regs->fpgaintfgrp_module);
		// writel(0, (void*)(SOCFPGA_SDR_ADDRESS + 0x5080));
		// writel(7, &reset_regs->brg_mod_reset);
		// writel(1, &nic301_regs->remap);
	}
}

static int make_env(const char *name, const char *cfg)
{
	printf("make_env\n");
	void* buf = 0; //shmem_map(0x1FFFF000, 0x1000);
	if (!buf) return -1;

	volatile char* str = (volatile char*)buf;
	memset((void*)str, 0, 0xF00);

	*str++ = 0x21;
	*str++ = 0x43;
	*str++ = 0x65;
	*str++ = 0x87;
	*str++ = 'c';
	*str++ = 'o';
	*str++ = 'r';
	*str++ = 'e';
	*str++ = '=';
	*str++ = '"';

	for (uint32_t i = 0; i < strlen(name); i++)
	{
		*str++ = name[i];
	}

	*str++ = '"';
	*str++ = '\n';
	FileLoad(cfg, (void*)str, 0);
	// shmem_unmap(buf, 0x1000);
	return 0;
}

int fpga_load_rbf(const char *name, const char *cfg, const char *xml)
{
	OsdDisable();
	static char path[1024];
	int ret = 0;

	if(cfg)
	{
		fpga_core_reset(1);
		make_env(name, cfg);
		do_bridge(0);
		reboot(0);
	}

	printf("Loading RBF: %s\n", name);

	if(name[0] == '/') strcpy(path, name);
	else sprintf(path, "%s/%s", !strcasecmp(name, "menu.rbf") ? getStorageDir(0) : getRootDir(), name);

	int rbf = open(path, O_RDONLY);
	if (rbf < 0)
	{
		char error[4096];
		snprintf(error,4096,"%s\nNot Found", name);
		printf("Couldn't open file %s\n", path);
		Info(error,5000);
		return -1;
	}
	else
	{
		struct stat64 st;
		if (fstat64(rbf, &st)<0)
		{
			printf("Couldn't get info of file %s\n", path);
			ret = -1;
		}
		else
		{
			printf("Bitstream size: %lld bytes\n", st.st_size);

			void *buf = malloc(st.st_size);
			if (!buf)
			{
				printf("Couldn't allocate %llu bytes.\n", st.st_size);
				ret = -1;
			}
			else
			{
				fpga_core_reset(1);
				if (read(rbf, buf, st.st_size)<st.st_size)
				{
					printf("Couldn't read file %s\n", name);
					ret = -1;
				}
				else
				{
					void *p = buf;
					__off64_t sz = st.st_size;
					if (!memcmp(buf, "MiSTer", 6))
					{
						sz = *(uint32_t*)(((uint8_t*)buf) + 12);
						p = (void*)(((uint8_t*)buf) + 16);
					}
					do_bridge(0);
					ret = socfpga_load(p, sz);
					if (ret)
					{
						printf("Error %d while loading %s\n", ret, path);
					}
					else
					{
						do_bridge(1);
					}
				}
				free(buf);
			}
		}
	}
	close(rbf);

	app_restart(!strcasecmp(name, "menu.rbf") ? "menu.rbf" : path, xml);
	return ret;
}

static uint32_t gpo_copy = 0;
void inline fpga_gpo_write(uint32_t value)
{
	gpo_copy = value;
	// /writel(value, (void*)(SOCFPGA_MGR_ADDRESS + 0x10));
}

void fpga_core_write(uint32_t offset, uint32_t value)
{
	//if (offset <= 0x1FFFFF) writel(value, (void*)(SOCFPGA_LWFPGASLAVES_ADDRESS + (offset & ~3)));
}

uint32_t fpga_core_read(uint32_t offset)
{
	//if (offset <= 0x1FFFFF) return readl((void*)(SOCFPGA_LWFPGASLAVES_ADDRESS + (offset & ~3)));
	return 0;
}

int fpga_io_init()
{
	//map_base = (uint32_t*)shmem_map(FPGA_REG_BASE, FPGA_REG_SIZE);
	if (!map_base) return -1;

	fpga_gpo_write(0);
	return 0;
}

int fpga_core_id()
{
	return 0;
}

int fpga_get_fio_size()
{
	return 0;
}

int fpga_get_io_version()
{
	return 0;
}

void fpga_set_led(uint32_t on)
{
}

int fpga_get_buttons()
{
	return 0;
}

int fpga_get_io_type()
{
	return 0;
}

void reboot(int cold)
{
	sync();
	fpga_core_reset(1);

	usleep(500000);

	void* buf = shmem_map(0x1FFFF000, 0x1000);
	if (buf)
	{
		volatile uint32_t* flg = (volatile uint32_t*)buf;
		//flg += 0xF08/4;
		//*flg = cold ? 0 : 0xBEEFB001;
		shmem_unmap(buf, 0x1000);
	}

	//writel(1, &reset_regs->ctrl);
	while (1) sleep(1);
}

char *getappname()
{
	static char dest[PATH_MAX];
	memset(dest, 0, sizeof(dest));

	char path[64];
	sprintf(path, "/proc/%d/exe", getpid());
	readlink(path, dest, PATH_MAX);

	return dest;
}

void app_restart(const char *path, const char *xml)
{
	sync();
	fpga_core_reset(1);

	input_switch(0);
	input_uinp_destroy();

	offload_stop();

	char *appname = getappname();
	printf("restarting the %s\n", appname);
	execl(appname, appname, path, xml, NULL);

	printf("Something went wrong. Rebooting...\n");
	reboot(1);
}

void fpga_core_reset(int reset)
{
	printf("fpga_core_reset(%d)\n", reset);
}

int is_fpga_ready(int quick)
{
	return fpgamgr_test_fpga_ready();
}

#define SSPI_STROBE  (1<<17)
#define SSPI_ACK     SSPI_STROBE

void fpga_spi_en(uint32_t mask, uint32_t en)
{
	printf("fpga_spi_en(%08x, %08x)\n", mask, en);
	//uint32_t gpo = fpga_gpo_read() | 0x80000000;
	//fpga_gpo_write(en ? gpo | mask : gpo & ~mask);
}

void fpga_wait_to_reset()
{
	printf("FPGA is not ready. JTAG uploading?\n");
	printf("Waiting for FPGA to be ready...\n");

	fpga_core_reset(1);

	while (!is_fpga_ready(0))
	{
		sleep(1);
	}
	reboot(0);
}

uint16_t fpga_spi(uint16_t word)
{
	printf("fpga_spi(%04x)\n", word);
	return 0;
}

uint16_t fpga_spi_fast(uint16_t word)
{
	printf("fpga_spi_fast(%04x)\n", word);
	return 0;
}

void fpga_spi_fast_block_write(const uint16_t *buf, uint32_t length)
{
}

void fpga_spi_fast_block_read(uint16_t *buf, uint32_t length)
{
}

void fpga_spi_fast_block_write_8(const uint8_t *buf, uint32_t length)
{
}

void fpga_spi_fast_block_read_8(uint8_t *buf, uint32_t length)
{
}

void fpga_spi_fast_block_write_be(const uint16_t *buf, uint32_t length)
{
}

void fpga_spi_fast_block_read_be(uint16_t *buf, uint32_t length)
{
}
