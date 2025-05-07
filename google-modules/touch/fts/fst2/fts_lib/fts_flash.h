/*
  *
  **************************************************************************
  **                        STMicroelectronics                            **
  **************************************************************************
  *                                                                        *
  *                       FTS API for Flashing the IC                      *
  *                                                                        *
  **************************************************************************
  **************************************************************************
  *
  */

/*!
  * \file ftsFlash.h
  * \brief Contains all the definitions and structs to handle the FW update
  *process
  */

#ifndef FTS_FLASH_H
#define FTS_FLASH_H

/*#define FW_H_FILE*/

#ifdef FW_H_FILE
#define PATH_FILE_FW		"NULL"
#else
#define PATH_FILE_FW		"st_fts.ubin"	/* /< new FW bin file name */
#endif

#ifdef FW_H_FILE
	#define FW_SIZE_NAME	myArray_size /* /< name of the variable in
						 * the FW header file which
						 * specified the dimension of
						 * the FW data array */
	#define FW_ARRAY_NAME	myArray	/* /< name of the variable in the FW
					 * header file which specified the FW
					 * data array */
#endif

#define FLASH_MAX_SECTIONS		10 /* /<maximum flash sections
					   * supported*/

/**
  * Struct which stores the status of FW to be updated in each flash sections
  *during the flash procedure
  */
struct force_update_flag {
	u8 code_update;	/* /< flag to update FW code */
	u8 section_update[FLASH_MAX_SECTIONS];	/* /< flag
					*to update FW sections  */
	u8 panel_init;	/* /< flag to initiate Full panel Init */
};

/**
  * Struct which contains  the header and data of a flash section
  */
struct fw_section {
	u16 sec_id; /* /< FW section ID mapped for
				*each flash sections  */
	u16 sec_ver; /* /< FW section version  */
	u32 sec_size; /* /< FW sectionsize in bytes  */
	u8 *sec_data; /* /< FW section data */
};

/**
  * Struct which contains information and data of the FW file to be burnt
  *into the IC
  */
struct firmware_file {
	u16 fw_ver;	/* /< FW version of the FW file */
	u8 flash_code_pages; /* /<size of fw code in pages*/
	u8 panel_info_pages;	/* /<size of fw sections in pages*/
	u32 fw_code_size;	/* /< size of fw code in bytes*/
	u8 *fw_code_data;  /*/< fw code data*/
	u8 num_code_pages; /*size depending on the FW code size*/
	u8 num_sections; /* fw sections in fw file */
	struct fw_section sections[FLASH_MAX_SECTIONS]; /* /< fw section
						data for each section*/
};

/**
  * Define section ID mapping for flash sections
  */
typedef enum {
	FINGERTIP_FW_CODE		= 0x0000, /* /< FW main code*/
	FINGERTIP_FW_REG		= 0x0001, /* /< FW Reg section */
	FINGERTIP_MUTUAL_CX		= 0x0002, /* /< FW mutual cx */
	FINGERTIP_SELF_CX		= 0x0003, /* /< FW self cx */
} fw_section_t;

#define FLASH_PAGE_SIZE			(4 * 1024) /* /<page size of 4KB*/
#define BIN_HEADER_SIZE			(32 + 4) /* /< fw ubin main header size
						 * including crc */
#define SECTION_HEADER_SIZE		20 /* /< fw ubin section header size */
#define BIN_HEADER			0xBABEFACE /* /< fw ubin main header
						   * identifier constant */
#define SECTION_HEADER			0xB16B00B5 /* /< fw ubin section header
						   * identifier constant */
#define CHIP_ID				0x3652 /* /< chip id of finger tip
					       * device, spruce */
#define NUM_FLASH_PAGES			48 /* /< number of flash pages in
					   * fingertip device, spruce */
#define DMA_CHUNK			32 /* /< Max number of bytes that
						* can be written to the DMA */
#define FLASH_CHUNK			(64 * 1024)/* /< Max number of bytes
							* that the DMA can burn
							* on the flash in one
							* shot in FTI */
#define FLASH_RETRY_COUNT		200 /* /< number of attemps to read the
					    * flash status */
#define FLASH_WAIT_BEFORE_RETRY		50 /* /< time to wait in ms between
					   * status readings */
#define FLASH_ERASE_READY_VAL		0x6A
#define FLASH_PGM_READY_VAL		0x71
#define FLASH_DMA_CODE_VAL		0xC0
#define FLASH_DMA_CONFIG_VAL		0x72
#define REG_CRC_MASK			0x03 /* /< mask to read
					     *fw register status of reg crc*/
#define REG_MISC_MASK			0x0C /* /< mask to read
					     *fw register status of misc crc*/
#define MS_CRC_MASK			0x0F /* /< mask to read
					     *fw register status of ms cx crc*/
#define SS_CRC_MASK			0xF0	/* /< mask to read fw register
						* status of ss cx crc*/
#define IOFF_CRC_MASK				0xFF /* /< mask to read fw
						* register status of ioff crc*/
#define RAWMS_CRC_MASK				0x03 /* /< mask to read
						     *fw register status of
						     * raw frame data crc*/

int read_fw_file(const char *path, struct firmware_file *fw);
int flash_burn(struct firmware_file fw,
		struct force_update_flag *force_update);
int flash_section_burn(struct firmware_file fw, fw_section_t section,
	u8 save_to_flash);
int read_sys_info(void);
int flash_update(struct force_update_flag *force_update);
int configure_spi4(void);
int full_panel_init(struct force_update_flag *force_update);
unsigned int calculate_crc(unsigned char *message, int size);
int get_fw_file_data(const char *path_to_file, u8 **data, int *size);
int flash_update_preset(void);
int wait_for_flash_ready(u8 type);
int flash_erase(int flash_pages);
int start_flash_dma(void);
int flash_dma(u32 address, u8 *data, int size);
int fill_flash(u32 address, u8 *data, int size);
#endif
